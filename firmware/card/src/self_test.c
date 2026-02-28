/*
 * CellSim Card — Self-Test (BIST) Implementation
 *
 * Per-peripheral built-in self-test run at boot and on demand.
 * Tests: I2C bus scan, per-cell I2C device verification through TCA9548A,
 * SPI ADC probe (ADS131M04 ID register read), temp sensor validation.
 */

#include "self_test.h"
#include "cell.h"
#include "health.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(self_test, LOG_LEVEL_INF);

/* I2C addresses (must match cell.c) */
#define TCA9548A_ADDR       0x70
#define MCP4725_BUCK_ADDR   0x61  /* A0=VIN */
#define MCP4725_LDO_ADDR    0x60  /* A0=GND */
#define TCA6408_ADDR        0x20
#define TMP117_ADDR         0x49

/* ADS131M04 SPI probe */
#define ADS131_FRAME_BYTES  18  /* 6 words × 3 bytes */
#define ADS131_CMD_RREG_ID  0xA000  /* RREG(0x00) → read ID register */
#define ADS131_EXPECTED_ID  0x2X    /* ID[15:8] = 0x20 for ADS131M04 */

/* Temperature plausibility bounds (°C) */
#define TEMP_MIN_PLAUSIBLE  (-20.0f)
#define TEMP_MAX_PLAUSIBLE  (85.0f)

/* I2C bus device references */
static const struct device *i2c_bus[CELLSIM_NUM_I2C_BUSES];

/* SPI bus + GPIO references (for ADC probe) */
static const struct device *spi_bus[2];
static struct spi_config spi_cfg[2];
static const struct device *adc_gpio_port;

/* GPIO pin tables — must match cell.c */
static const gpio_pin_t adc_cs_pins[CELLSIM_NUM_CELLS] = {
    0, 1, 2, 3, 6, 7, 8, 9,
};
static const gpio_pin_t mux_a0_pins[2] = { 4, 10 };
static const gpio_pin_t mux_a1_pins[2] = { 5, 11 };

static int init_i2c_refs(void)
{
    i2c_bus[0] = DEVICE_DT_GET(DT_ALIAS(i2c_cells_a));
    i2c_bus[1] = DEVICE_DT_GET(DT_ALIAS(i2c_cells_b));

    for (int i = 0; i < CELLSIM_NUM_I2C_BUSES; i++) {
        if (!device_is_ready(i2c_bus[i])) {
            LOG_ERR("I2C bus %d not ready", i);
            return -ENODEV;
        }
    }
    return 0;
}

static int init_spi_refs(void)
{
    spi_bus[0] = DEVICE_DT_GET(DT_ALIAS(spi_cells_a));
    spi_bus[1] = DEVICE_DT_GET(DT_ALIAS(spi_cells_b));

    for (int i = 0; i < 2; i++) {
        if (!device_is_ready(spi_bus[i])) {
            LOG_ERR("SPI bus %d not ready", i);
            return -ENODEV;
        }
        spi_cfg[i].frequency = 10000000;
        spi_cfg[i].operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
                               SPI_MODE_CPHA;
        spi_cfg[i].cs = NULL;
    }

    adc_gpio_port = DEVICE_DT_GET(DT_NODELABEL(gpioe));
    return device_is_ready(adc_gpio_port) ? 0 : -ENODEV;
}

static int tca9548a_select(const struct device *bus, uint8_t channel)
{
    uint8_t mask = (channel < 8) ? (1 << channel) : 0x00;
    return i2c_write(bus, &mask, 1, TCA9548A_ADDR);
}

static bool i2c_probe(const struct device *bus, uint8_t addr)
{
    uint8_t dummy;
    return (i2c_read(bus, &dummy, 1, addr) == 0);
}

/*
 * Probe ADS131M04 by reading ID register via SPI.
 * Returns true if a valid ID response is received.
 */
static bool ads131_spi_probe(uint8_t cell_index)
{
    int bidx = cell_index / CELLSIM_CELLS_PER_BUS;
    int ch = cell_index % CELLSIM_CELLS_PER_BUS;

    uint8_t tx[ADS131_FRAME_BYTES] = {0};
    uint8_t rx[ADS131_FRAME_BYTES] = {0};

    /* Send RREG(ID) command */
    tx[0] = (uint8_t)(ADS131_CMD_RREG_ID >> 8);
    tx[1] = (uint8_t)(ADS131_CMD_RREG_ID);

    struct spi_buf tx_buf = { .buf = tx, .len = ADS131_FRAME_BYTES };
    struct spi_buf rx_buf = { .buf = rx, .len = ADS131_FRAME_BYTES };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    /* Set MISO mux */
    gpio_pin_set(adc_gpio_port, mux_a0_pins[bidx], ch & 0x01);
    gpio_pin_set(adc_gpio_port, mux_a1_pins[bidx], (ch >> 1) & 0x01);

    /* Assert CS, transfer, deassert */
    gpio_pin_set(adc_gpio_port, adc_cs_pins[cell_index], 0);
    int ret = spi_transceive(spi_bus[bidx], &spi_cfg[bidx], &tx_set, &rx_set);
    gpio_pin_set(adc_gpio_port, adc_cs_pins[cell_index], 1);

    if (ret != 0) {
        return false;
    }

    /* Need a second frame to get the RREG response */
    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));

    gpio_pin_set(adc_gpio_port, adc_cs_pins[cell_index], 0);
    ret = spi_transceive(spi_bus[bidx], &spi_cfg[bidx], &tx_set, &rx_set);
    gpio_pin_set(adc_gpio_port, adc_cs_pins[cell_index], 1);

    if (ret != 0) {
        return false;
    }

    /* ID register data is in word 1 (bytes 3-5) of the response frame.
     * ADS131M04 ID bits [15:8] = 0x24 (channel count varies in lower bits). */
    uint8_t id_hi = rx[3];
    bool valid = ((id_hi & 0xF0) == 0x20);  /* ADS131Mxx family */

    if (valid) {
        LOG_DBG("Cell %d: ADS131M04 ID = 0x%02X%02X", cell_index, rx[3], rx[4]);
    }

    return valid;
}

int self_test_cell(uint8_t cell_index, struct cell_test_result *result)
{
    if (cell_index >= CELLSIM_NUM_CELLS || result == NULL) {
        return -EINVAL;
    }

    memset(result, 0, sizeof(*result));

    int bus_idx = cell_index / CELLSIM_CELLS_PER_BUS;
    int mux_ch = cell_index % CELLSIM_CELLS_PER_BUS;
    const struct device *bus = i2c_bus[bus_idx];

    /* I2C: select mux channel for this cell */
    int ret = tca9548a_select(bus, mux_ch);
    if (ret != 0) {
        LOG_ERR("Cell %d: TCA9548A select ch%d failed: %d", cell_index, mux_ch, ret);
        return ret;
    }

    result->i2c_isolator_ok = true;

    result->dac_buck_ok = i2c_probe(bus, MCP4725_BUCK_ADDR);
    if (!result->dac_buck_ok) {
        LOG_WRN("Cell %d: Buck DAC (0x%02X) not found", cell_index, MCP4725_BUCK_ADDR);
    }

    result->dac_ldo_ok = i2c_probe(bus, MCP4725_LDO_ADDR);
    if (!result->dac_ldo_ok) {
        LOG_WRN("Cell %d: LDO DAC (0x%02X) not found", cell_index, MCP4725_LDO_ADDR);
    }

    result->gpio_ok = i2c_probe(bus, TCA6408_ADDR);
    if (!result->gpio_ok) {
        LOG_WRN("Cell %d: GPIO expander (0x%02X) not found", cell_index, TCA6408_ADDR);
    }

    /* Temperature sensor */
    uint8_t temp_reg = 0x00;
    uint8_t temp_data[2];
    ret = i2c_write_read(bus, TMP117_ADDR, &temp_reg, 1, temp_data, 2);
    if (ret == 0) {
        int16_t raw = (temp_data[0] << 8) | temp_data[1];
        result->temp_celsius = raw * 0.0078125f;
        result->temp_ok = (result->temp_celsius >= TEMP_MIN_PLAUSIBLE &&
                           result->temp_celsius <= TEMP_MAX_PLAUSIBLE);
        if (!result->temp_ok) {
            LOG_WRN("Cell %d: temp %.1f°C out of plausible range",
                     cell_index, (double)result->temp_celsius);
        }
    } else {
        LOG_WRN("Cell %d: TMP117 read failed: %d", cell_index, ret);
        result->temp_ok = false;
    }

    /* Deselect I2C mux */
    tca9548a_select(bus, 0xFF);

    /* SPI: probe ADS131M04 ADC */
    result->adc_ok = ads131_spi_probe(cell_index);
    if (!result->adc_ok) {
        LOG_WRN("Cell %d: ADS131M04 SPI probe failed", cell_index);
    }

    /* Relay test placeholder */
    result->relay_ok = result->gpio_ok;

    LOG_INF("Cell %d: iso=%d buck=%d ldo=%d adc=%d gpio=%d temp=%d(%.1f°C) relay=%d",
            cell_index,
            result->i2c_isolator_ok, result->dac_buck_ok, result->dac_ldo_ok,
            result->adc_ok, result->gpio_ok, result->temp_ok,
            (double)result->temp_celsius, result->relay_ok);

    return 0;
}

int self_test_run(struct self_test_result *result)
{
    if (result == NULL) {
        return -EINVAL;
    }

    memset(result, 0, sizeof(*result));
    result->all_passed = true;

    /* Initialize bus references */
    int ret = init_i2c_refs();
    if (ret != 0) {
        result->all_passed = false;
        return ret;
    }

    ret = init_spi_refs();
    if (ret != 0) {
        LOG_WRN("SPI init failed: %d — ADC tests will fail", ret);
    }

    /* Test I2C buses and TCA9548A muxes */
    for (int i = 0; i < CELLSIM_NUM_I2C_BUSES; i++) {
        result->i2c_bus_ok[i] = device_is_ready(i2c_bus[i]);
        result->tca9548a_ok[i] = i2c_probe(i2c_bus[i], TCA9548A_ADDR);

        if (!result->i2c_bus_ok[i] || !result->tca9548a_ok[i]) {
            LOG_ERR("I2C bus %d: bus_ok=%d, mux_ok=%d",
                    i, result->i2c_bus_ok[i], result->tca9548a_ok[i]);
            result->all_passed = false;
        }
    }

    /* Ethernet PHY check */
    struct net_if *iface = net_if_get_default();
    result->phy_ok = (iface != NULL && net_if_is_up(iface));

    /* MCU health */
    result->mcu_temp_celsius = health_get_mcu_temp();
    result->input_voltage = health_get_input_voltage();

    /* Test each cell */
    for (int i = 0; i < CELLSIM_NUM_CELLS; i++) {
        ret = self_test_cell(i, &result->cells[i]);
        if (ret != 0) {
            result->all_passed = false;
            continue;
        }

        struct cell_test_result *c = &result->cells[i];
        if (!c->i2c_isolator_ok || !c->dac_buck_ok || !c->dac_ldo_ok ||
            !c->adc_ok || !c->gpio_ok || !c->temp_ok || !c->relay_ok) {
            result->all_passed = false;
        }
    }

    LOG_INF("Self-test %s: MCU %.1f°C, Vin %.1fV",
            result->all_passed ? "PASSED" : "FAILED",
            (double)result->mcu_temp_celsius,
            (double)result->input_voltage);

    return 0;
}
