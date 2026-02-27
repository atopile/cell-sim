/*
 * CellSim Card — Self-Test (BIST) Implementation
 */

#include "self_test.h"
#include "health.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(self_test, LOG_LEVEL_INF);

/* I2C addresses for per-cell devices (on isolated side, behind TCA9548A) */
#define TCA9548A_ADDR       0x70
#define MCP4725_BUCK_ADDR   0x60  /* A0=0 */
#define MCP4725_LDO_ADDR    0x61  /* A0=1 */
#define ADS1115_ADDR        0x48
#define TCA6408_ADDR        0x20
#define TMP117_ADDR         0x49  /* ADDR pin pulled to VDD to avoid ADS1115 conflict */

/* Temperature plausibility bounds (°C) */
#define TEMP_MIN_PLAUSIBLE  (-20.0f)
#define TEMP_MAX_PLAUSIBLE  (85.0f)

/* I2C bus device references */
static const struct device *i2c_bus[CELLSIM_NUM_I2C_BUSES];

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

/**
 * Select a TCA9548A mux channel.
 * channel 0-7, or 0xFF to disable all channels.
 */
static int tca9548a_select(const struct device *bus, uint8_t channel)
{
    uint8_t mask = (channel < 8) ? (1 << channel) : 0x00;
    return i2c_write(bus, &mask, 1, TCA9548A_ADDR);
}

/**
 * Probe an I2C address (single-byte read attempt).
 */
static bool i2c_probe(const struct device *bus, uint8_t addr)
{
    uint8_t dummy;
    return (i2c_read(bus, &dummy, 1, addr) == 0);
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

    /* Select mux channel for this cell */
    int ret = tca9548a_select(bus, mux_ch);
    if (ret != 0) {
        LOG_ERR("Cell %d: TCA9548A select ch%d failed: %d", cell_index, mux_ch, ret);
        return ret;
    }

    /* Probe each device on the isolated I2C bus */
    result->i2c_isolator_ok = true; /* If mux select worked, isolator is passing traffic */

    result->dac_buck_ok = i2c_probe(bus, MCP4725_BUCK_ADDR);
    if (!result->dac_buck_ok) {
        LOG_WRN("Cell %d: Buck DAC (0x%02X) not found", cell_index, MCP4725_BUCK_ADDR);
    }

    result->dac_ldo_ok = i2c_probe(bus, MCP4725_LDO_ADDR);
    if (!result->dac_ldo_ok) {
        LOG_WRN("Cell %d: LDO DAC (0x%02X) not found", cell_index, MCP4725_LDO_ADDR);
    }

    result->adc_ok = i2c_probe(bus, ADS1115_ADDR);
    if (!result->adc_ok) {
        LOG_WRN("Cell %d: ADC (0x%02X) not found", cell_index, ADS1115_ADDR);
    }

    result->gpio_ok = i2c_probe(bus, TCA6408_ADDR);
    if (!result->gpio_ok) {
        LOG_WRN("Cell %d: GPIO expander (0x%02X) not found", cell_index, TCA6408_ADDR);
    }

    /* Temperature sensor: read 2-byte register 0x00 (temperature) */
    uint8_t temp_reg = 0x00;
    uint8_t temp_data[2];
    ret = i2c_write_read(bus, TMP117_ADDR, &temp_reg, 1, temp_data, 2);
    if (ret == 0) {
        int16_t raw = (temp_data[0] << 8) | temp_data[1];
        result->temp_celsius = raw * 0.0078125f; /* TMP117: 7.8125 m°C/LSB */
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

    /* Relay test: toggle relay via GPIO expander, check ADC for voltage change */
    /* TODO: implement relay toggle test (requires cell to be powered) */
    result->relay_ok = result->gpio_ok; /* Placeholder: pass if GPIO found */

    /* Deselect mux */
    tca9548a_select(bus, 0xFF);

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

    /* Initialize I2C bus references */
    int ret = init_i2c_refs();
    if (ret != 0) {
        result->all_passed = false;
        return ret;
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

    /* Ethernet PHY check — link layer up means PHY + MAC are working */
    struct net_if *iface = net_if_get_default();
    result->phy_ok = (iface != NULL && net_if_is_up(iface));

    /* MCU health readings */
    result->mcu_temp_celsius = health_get_mcu_temp();
    result->input_voltage = health_get_input_voltage();

    /* Test each cell */
    for (int i = 0; i < CELLSIM_NUM_CELLS; i++) {
        ret = self_test_cell(i, &result->cells[i]);
        if (ret != 0) {
            result->all_passed = false;
            continue;
        }

        /* Check if any device in this cell failed */
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
