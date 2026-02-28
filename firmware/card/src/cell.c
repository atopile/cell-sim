/*
 * CellSim Card — Cell Control HAL Implementation
 *
 * Controls per-cell voltage/current through:
 *   I2C path: MCP4725 DACs (buck + LDO setpoints), TCA6408 GPIO, TMP117 temp
 *   SPI path: ADS131M04 24-bit ADC (4 simultaneous channels)
 *
 * Bus architecture:
 *   I2C1 → TCA9548A → Cells 0–3  |  SPI1 → ISO7741 ×4 → Cells 0–3 ADC
 *   I2C2 → TCA9548A → Cells 4–7  |  SPI2 → ISO7741 ×4 → Cells 4–7 ADC
 */

#include "cell.h"
#include "self_test.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(cell, LOG_LEVEL_INF);

/* ── I2C addresses (isolated side, behind TCA9548A) ───────────── */
#define TCA9548A_ADDR       0x70
#define MCP4725_BUCK_ADDR   0x61  /* A0=VIN (spec §4.2.2) */
#define MCP4725_LDO_ADDR    0x60  /* A0=GND (spec §4.2.3) */
#define TCA6408_ADDR        0x20
#define TMP117_ADDR         0x49  /* ADDR=VDD */

/* ── TCA6408 registers ────────────────────────────────────────── */
#define TCA6408_OUTPUT      0x01
#define TCA6408_CONFIG      0x03  /* 0 = output, 1 = input */

/* ── ADS131M04 SPI protocol ───────────────────────────────────── */
/*
 * Frame format (24-bit word size, default):
 *   TX: [CMD] [REG_DATA/0] [0] [0] [0] [CRC/0]
 *   RX: [STATUS]  [CH0]  [CH1]  [CH2]  [CH3]  [CRC]
 *
 * 6 words × 3 bytes = 18 bytes per frame.
 */
#define ADS131_FRAME_WORDS  6
#define ADS131_WORD_BYTES   3
#define ADS131_FRAME_BYTES  (ADS131_FRAME_WORDS * ADS131_WORD_BYTES)

/* Commands (upper byte of 16-bit command, zero-padded to 24 bits) */
#define ADS131_CMD_NULL     0x0000
#define ADS131_CMD_RESET    0x0011
#define ADS131_CMD_STANDBY  0x0022
#define ADS131_CMD_WAKEUP   0x0033
#define ADS131_CMD_RREG(a)  (0xA000 | (((a) & 0x3F) << 7))
#define ADS131_CMD_WREG(a)  (0x6000 | (((a) & 0x3F) << 7))

/* Registers */
#define ADS131_REG_ID       0x00
#define ADS131_REG_STATUS   0x01
#define ADS131_REG_MODE     0x02
#define ADS131_REG_CLOCK    0x03
#define ADS131_REG_CFG      0x06

/* MODE register bits */
#define ADS131_MODE_CRC_DIS 0x000000  /* CRC disabled (bits 5:4 = 00) */

/* CLOCK register: enable all 4 channels, OSR = 4096 (4 kSPS) */
#define ADS131_CLOCK_DEFAULT  0x0F0E
/*   bits [3:0] = 0xF: CH0-CH3 enabled
 *   bits [4]   = 0:   internal osc
 *   bits [7:5] = 0:   no divide
 *   bits [11:8] = 0xF: OSR = 4096 → ~4 kSPS
 *   (firmware reads at 100–1000 Hz, well below conversion rate)
 */

/* ── MCP4725 DAC ──────────────────────────────────────────────── */
#define DAC_MAX_CODE        4095
#define DAC_MAX_OUTPUT_MV   5000

/* ── Buck converter parameters ────────────────────────────────── */
#define BUCK_MAX_MV         5500  /* Max buck output (spec §4.2.2) */
#define LDO_HEADROOM_MV     500   /* Buck tracks LDO output + headroom (spec §4.2.3) */

/* ── TMP117 ───────────────────────────────────────────────────── */
#define TMP117_REG_TEMP     0x00

/* ── SPI ADC GPIO assignments ─────────────────────────────────── */
/*
 * TODO: Finalize pin assignments when PCB layout is done.
 * These placeholders match the overlay comments (GPIOE pins).
 *
 * SPI1 (cells 0-3): CS on PE0-PE3, mux on PE4/PE5
 * SPI2 (cells 4-7): CS on PE6-PE9, mux on PE10/PE11
 */
static const gpio_pin_t adc_cs_pins[CELL_COUNT] = {
    0, 1, 2, 3,    /* SPI1: cells 0-3 on GPIOE */
    6, 7, 8, 9,    /* SPI2: cells 4-7 on GPIOE */
};

static const gpio_pin_t mux_a0_pins[2] = { 4, 10 };  /* per SPI bus */
static const gpio_pin_t mux_a1_pins[2] = { 5, 11 };

/* ── Bus state ────────────────────────────────────────────────── */

/* I2C buses (cells 0-3 and 4-7 control path) */
static const struct device *i2c_bus[CELLSIM_NUM_I2C_BUSES];
static struct k_mutex i2c_lock[CELLSIM_NUM_I2C_BUSES];

/* SPI buses (cells 0-3 and 4-7 measurement path) */
static const struct device *spi_bus[2];
static struct spi_config spi_cfg[2];
static struct k_mutex spi_lock[2];

/* GPIO port for ADC CS and mux select (all on GPIOE — TODO: update if changed) */
static const struct device *adc_gpio_port;

/* Per-cell state */
static struct cell_state cells[CELL_COUNT];

/* Per-cell TCA6408 output register shadow */
static uint8_t gpio_shadow[CELL_COUNT];

/* ── Helpers ──────────────────────────────────────────────────── */

static inline int i2c_idx(uint8_t cell_id)   { return cell_id / CELLS_PER_BUS; }
static inline int spi_idx(uint8_t cell_id)   { return cell_id / CELLS_PER_BUS; }
static inline int mux_channel(uint8_t cell_id) { return cell_id % CELLS_PER_BUS; }

static const struct device *cell_i2c(uint8_t cell_id)
{
    return i2c_bus[i2c_idx(cell_id)];
}

/* ── TCA9548A I2C Mux ────────────────────────────────────────── */

static int tca9548a_select(const struct device *bus, uint8_t channel)
{
    uint8_t mask = (channel < 8) ? (1 << channel) : 0x00;
    return i2c_write(bus, &mask, 1, TCA9548A_ADDR);
}

static int i2c_mux_select(uint8_t cell_id)
{
    return tca9548a_select(cell_i2c(cell_id), mux_channel(cell_id));
}

static int i2c_mux_deselect(uint8_t cell_id)
{
    return tca9548a_select(cell_i2c(cell_id), 0xFF);
}

/* ── MCP4725 DAC ──────────────────────────────────────────────── */

static int mcp4725_write(const struct device *bus, uint8_t addr, uint16_t code)
{
    if (code > DAC_MAX_CODE) {
        code = DAC_MAX_CODE;
    }
    /* Fast write: [0 C1 C0 PD1 PD0 D11..D8] [D7..D0] */
    uint8_t buf[2] = {
        (uint8_t)(code >> 8) & 0x0F,
        (uint8_t)(code & 0xFF),
    };
    return i2c_write(bus, buf, 2, addr);
}

static uint16_t mv_to_dac(uint16_t mv)
{
    if (mv > DAC_MAX_OUTPUT_MV) {
        mv = DAC_MAX_OUTPUT_MV;
    }
    return (uint16_t)(((uint32_t)mv * DAC_MAX_CODE) / DAC_MAX_OUTPUT_MV);
}

/* ── TCA6408 GPIO Expander ────────────────────────────────────── */

static int tca6408_write_output(const struct device *bus, uint8_t value)
{
    uint8_t buf[2] = { TCA6408_OUTPUT, value };
    return i2c_write(bus, buf, sizeof(buf), TCA6408_ADDR);
}

static int tca6408_init_cell(const struct device *bus)
{
    /* All controllable bits as outputs (0 = output) */
    uint8_t output_mask = GPIO_DMM_CAL | GPIO_BUCK_EN | GPIO_LDO_EN |
                          GPIO_LOAD_SWITCH | GPIO_OUTPUT_RELAY |
                          GPIO_EXT_LOAD_SW | GPIO_FOUR_WIRE;
    uint8_t config = (uint8_t)~output_mask;
    uint8_t buf[2] = { TCA6408_CONFIG, config };
    int ret = i2c_write(bus, buf, sizeof(buf), TCA6408_ADDR);
    if (ret != 0) {
        return ret;
    }
    return tca6408_write_output(bus, 0x00);
}

/* ── TMP117 Temperature Sensor ────────────────────────────────── */

static int tmp117_read(const struct device *bus, int16_t *temp_c10)
{
    uint8_t reg = TMP117_REG_TEMP;
    uint8_t data[2];
    int ret = i2c_write_read(bus, TMP117_ADDR, &reg, 1, data, 2);
    if (ret != 0) {
        return ret;
    }
    /* TMP117: 7.8125 m°C/LSB → °C×10: raw * 78125 / 1000000 */
    int16_t raw = (int16_t)((data[0] << 8) | data[1]);
    *temp_c10 = (int16_t)((int32_t)raw * 78125 / 1000000);
    return 0;
}

/* ── ADS131M04 SPI ADC ────────────────────────────────────────── */

/* Pack a 24-bit word into 3 bytes (MSB first) */
static void ads131_pack_word(uint8_t *buf, uint32_t word)
{
    buf[0] = (uint8_t)(word >> 16);
    buf[1] = (uint8_t)(word >> 8);
    buf[2] = (uint8_t)(word);
}

/* Unpack 3 bytes (MSB first) into a 24-bit value, sign-extended to int32 */
static int32_t ads131_unpack_word(const uint8_t *buf)
{
    int32_t val = ((int32_t)buf[0] << 16) | (buf[1] << 8) | buf[2];
    /* Sign-extend from 24-bit */
    if (val & 0x800000) {
        val |= (int32_t)0xFF000000;
    }
    return val;
}

/* Assert/deassert CS for a specific cell */
static void adc_cs_assert(uint8_t cell_id)
{
    gpio_pin_set(adc_gpio_port, adc_cs_pins[cell_id], 0);  /* Active low */
}

static void adc_cs_deassert(uint8_t cell_id)
{
    gpio_pin_set(adc_gpio_port, adc_cs_pins[cell_id], 1);
}

/* Set MISO mux to select a cell's return channel */
static void adc_mux_select(uint8_t cell_id)
{
    int bidx = spi_idx(cell_id);
    int ch = mux_channel(cell_id);  /* 0-3 → 2-bit address */
    gpio_pin_set(adc_gpio_port, mux_a0_pins[bidx], ch & 0x01);
    gpio_pin_set(adc_gpio_port, mux_a1_pins[bidx], (ch >> 1) & 0x01);
}

/*
 * Perform one ADS131M04 SPI frame exchange.
 *   cmd:     16-bit command (zero-padded to 24 bits in frame)
 *   reg_data: optional register data word (for WREG commands, else 0)
 *   status:  output: response status word (may be NULL)
 *   ch_data: output: 4-channel data array (may be NULL)
 *
 * Returns 0 on success, negative errno on SPI error.
 */
static int ads131_transfer(uint8_t cell_id, uint16_t cmd, uint32_t reg_data,
                           uint32_t *status, int32_t ch_data[4])
{
    int bidx = spi_idx(cell_id);
    uint8_t tx[ADS131_FRAME_BYTES] = {0};
    uint8_t rx[ADS131_FRAME_BYTES] = {0};

    /* Build TX frame */
    ads131_pack_word(&tx[0], (uint32_t)cmd << 8);  /* CMD in word 0 */
    if (reg_data != 0) {
        ads131_pack_word(&tx[3], reg_data);  /* REG data in word 1 */
    }
    /* Words 2-5 are zero (padding + CRC placeholder) */

    struct spi_buf tx_buf = { .buf = tx, .len = ADS131_FRAME_BYTES };
    struct spi_buf rx_buf = { .buf = rx, .len = ADS131_FRAME_BYTES };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    /* Set MISO mux, assert CS */
    adc_mux_select(cell_id);
    adc_cs_assert(cell_id);

    int ret = spi_transceive(spi_bus[bidx], &spi_cfg[bidx], &tx_set, &rx_set);

    adc_cs_deassert(cell_id);

    if (ret != 0) {
        return ret;
    }

    /* Parse RX frame */
    if (status != NULL) {
        *status = ((uint32_t)rx[0] << 16) | (rx[1] << 8) | rx[2];
    }
    if (ch_data != NULL) {
        for (int i = 0; i < 4; i++) {
            ch_data[i] = ads131_unpack_word(&rx[3 + i * ADS131_WORD_BYTES]);
        }
    }

    return 0;
}

/* Write an ADS131M04 register (takes effect on next frame) */
static int ads131_write_reg(uint8_t cell_id, uint8_t reg, uint32_t data)
{
    return ads131_transfer(cell_id, ADS131_CMD_WREG(reg), data, NULL, NULL);
}

/* Initialize one ADS131M04 (reset + configure) */
static int ads131_init_cell(uint8_t cell_id)
{
    int ret;

    /* Software reset */
    ret = ads131_transfer(cell_id, ADS131_CMD_RESET, 0, NULL, NULL);
    if (ret != 0) {
        return ret;
    }
    k_sleep(K_MSEC(5));  /* Wait for reset (tPOR) */

    /* Disable CRC for simpler framing */
    ret = ads131_write_reg(cell_id, ADS131_REG_MODE, ADS131_MODE_CRC_DIS);
    if (ret != 0) {
        return ret;
    }

    /* Send NULL to clock in the WREG response */
    ret = ads131_transfer(cell_id, ADS131_CMD_NULL, 0, NULL, NULL);
    if (ret != 0) {
        return ret;
    }

    /* Configure CLOCK: all 4 channels enabled, OSR=4096 (~4 kSPS) */
    ret = ads131_write_reg(cell_id, ADS131_REG_CLOCK, ADS131_CLOCK_DEFAULT);
    if (ret != 0) {
        return ret;
    }

    /* Clock in the WREG response */
    return ads131_transfer(cell_id, ADS131_CMD_NULL, 0, NULL, NULL);
}

/* Read all 4 channels from one ADS131M04. Returns raw 24-bit signed values. */
static int ads131_read_channels(uint8_t cell_id, int32_t ch_data[4])
{
    return ads131_transfer(cell_id, ADS131_CMD_NULL, 0, NULL, ch_data);
}

/* ── ADC Scaling ──────────────────────────────────────────────── */

/*
 * Convert 24-bit signed ADC raw to millivolts at the signal source,
 * applying the voltage divider ratio.
 *
 * V_adc = raw × Vref / 2^23
 * V_signal = V_adc × divider_ratio
 */
static uint16_t adc_raw_to_signal_mv(int32_t raw, float divider)
{
    if (raw < 0) {
        return 0;
    }
    float v_adc_mv = (float)raw * (float)ADC_VREF_MV / (float)ADC_FULL_SCALE;
    float v_signal = v_adc_mv * divider;
    if (v_signal > 65535.0f) {
        return 65535;
    }
    return (uint16_t)v_signal;
}

/*
 * Convert current-sense ADC raw to microamps.
 *
 * V_adc = raw × Vref / 2^23
 * V_ina = V_adc × ADC_VDIV_CURRENT    (undo 2:5 divider)
 * I = V_ina / (R_shunt × Gain)
 */
static uint32_t adc_raw_to_current_ua(int32_t raw)
{
    if (raw < 0) {
        return 0;
    }
    float v_adc_mv = (float)raw * (float)ADC_VREF_MV / (float)ADC_FULL_SCALE;
    float v_ina_mv = v_adc_mv * ADC_VDIV_CURRENT;
    float i_ma = v_ina_mv / (ADC_IOUT_SHUNT_R * ADC_IOUT_INA_GAIN * 1000.0f);
    return (uint32_t)(i_ma * 1000.0f);  /* mA → µA */
}

/* ── SPI GPIO Init ────────────────────────────────────────────── */

static int adc_gpio_init(void)
{
    adc_gpio_port = DEVICE_DT_GET(DT_NODELABEL(gpioe));
    if (!device_is_ready(adc_gpio_port)) {
        LOG_ERR("ADC GPIO port (GPIOE) not ready");
        return -ENODEV;
    }

    /* Configure CS pins (active low, default high = deasserted) */
    for (int i = 0; i < CELL_COUNT; i++) {
        int ret = gpio_pin_configure(adc_gpio_port, adc_cs_pins[i],
                                     GPIO_OUTPUT_HIGH);
        if (ret != 0) {
            LOG_ERR("Cell %d: CS GPIO config failed: %d", i, ret);
            return ret;
        }
    }

    /* Configure MISO mux select pins (output, default 0) */
    for (int b = 0; b < 2; b++) {
        gpio_pin_configure(adc_gpio_port, mux_a0_pins[b], GPIO_OUTPUT_LOW);
        gpio_pin_configure(adc_gpio_port, mux_a1_pins[b], GPIO_OUTPUT_LOW);
    }

    return 0;
}

/* ── Public API ───────────────────────────────────────────────── */

int cell_init(void)
{
    int ret;

    /* I2C bus init */
    i2c_bus[0] = DEVICE_DT_GET(DT_ALIAS(i2c_cells_a));
    i2c_bus[1] = DEVICE_DT_GET(DT_ALIAS(i2c_cells_b));

    for (int i = 0; i < CELLSIM_NUM_I2C_BUSES; i++) {
        if (!device_is_ready(i2c_bus[i])) {
            LOG_ERR("I2C bus %d not ready", i);
            return -ENODEV;
        }
        k_mutex_init(&i2c_lock[i]);
    }

    /* SPI bus init */
    spi_bus[0] = DEVICE_DT_GET(DT_ALIAS(spi_cells_a));
    spi_bus[1] = DEVICE_DT_GET(DT_ALIAS(spi_cells_b));

    for (int i = 0; i < 2; i++) {
        if (!device_is_ready(spi_bus[i])) {
            LOG_ERR("SPI bus %d not ready", i);
            return -ENODEV;
        }
        k_mutex_init(&spi_lock[i]);

        /* SPI config: mode 1 (CPOL=0, CPHA=1), 10 MHz, 8-bit, MSB first */
        spi_cfg[i].frequency = 10000000;
        spi_cfg[i].operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
                               SPI_MODE_CPHA;
        spi_cfg[i].cs = NULL;  /* CS managed by software GPIO */
    }

    /* ADC GPIO init (CS lines + MISO mux select) */
    ret = adc_gpio_init();
    if (ret != 0) {
        return ret;
    }

    /* Initialize each cell */
    for (uint8_t id = 0; id < CELL_COUNT; id++) {
        int bidx = i2c_idx(id);
        const struct device *bus = i2c_bus[bidx];

        /* I2C: configure GPIO expander + zero DACs */
        k_mutex_lock(&i2c_lock[bidx], K_FOREVER);

        ret = i2c_mux_select(id);
        if (ret != 0) {
            LOG_ERR("Cell %d: I2C mux select failed: %d", id, ret);
            k_mutex_unlock(&i2c_lock[bidx]);
            continue;
        }

        ret = tca6408_init_cell(bus);
        if (ret != 0) {
            LOG_WRN("Cell %d: GPIO init failed: %d", id, ret);
        }
        gpio_shadow[id] = 0x00;

        mcp4725_write(bus, MCP4725_BUCK_ADDR, 0);
        mcp4725_write(bus, MCP4725_LDO_ADDR, 0);

        i2c_mux_deselect(id);
        k_mutex_unlock(&i2c_lock[bidx]);

        /* SPI: initialize ADS131M04 (reset + configure) */
        int sidx = spi_idx(id);
        k_mutex_lock(&spi_lock[sidx], K_FOREVER);

        ret = ads131_init_cell(id);
        if (ret != 0) {
            LOG_WRN("Cell %d: ADC init failed: %d", id, ret);
        }

        k_mutex_unlock(&spi_lock[sidx]);

        memset(&cells[id], 0, sizeof(cells[id]));
    }

    LOG_INF("Cell HAL initialized (%d cells, I2C+SPI)", CELL_COUNT);
    return 0;
}

int cell_set_voltage(uint8_t cell_id, uint16_t mv)
{
    if (cell_id >= CELL_COUNT || mv > DAC_MAX_OUTPUT_MV) {
        return -EINVAL;
    }

    int bidx = i2c_idx(cell_id);
    const struct device *bus = i2c_bus[bidx];

    /* Buck tracks LDO output + headroom (spec §4.2.3) */
    uint16_t buck_mv = mv + LDO_HEADROOM_MV;
    if (buck_mv > BUCK_MAX_MV) {
        buck_mv = BUCK_MAX_MV;
    }

    uint16_t buck_code = mv_to_dac(buck_mv);
    uint16_t ldo_code = mv_to_dac(mv);

    k_mutex_lock(&i2c_lock[bidx], K_FOREVER);

    int ret = i2c_mux_select(cell_id);
    if (ret != 0) {
        k_mutex_unlock(&i2c_lock[bidx]);
        return ret;
    }

    ret = mcp4725_write(bus, MCP4725_BUCK_ADDR, buck_code);
    if (ret != 0) {
        LOG_ERR("Cell %d: buck DAC write failed: %d", cell_id, ret);
        i2c_mux_deselect(cell_id);
        k_mutex_unlock(&i2c_lock[bidx]);
        return ret;
    }

    ret = mcp4725_write(bus, MCP4725_LDO_ADDR, ldo_code);
    if (ret != 0) {
        LOG_ERR("Cell %d: LDO DAC write failed: %d", cell_id, ret);
    }

    i2c_mux_deselect(cell_id);
    k_mutex_unlock(&i2c_lock[bidx]);

    cells[cell_id].setpoint_mv = mv;
    return ret;
}

int cell_set_output(uint8_t cell_id, bool enable)
{
    if (cell_id >= CELL_COUNT) {
        return -EINVAL;
    }

    int bidx = i2c_idx(cell_id);
    const struct device *bus = i2c_bus[bidx];
    uint8_t gpio = gpio_shadow[cell_id];

    if (enable) {
        /* Power-on sequence: buck → LDO → relay */
        gpio |= GPIO_BUCK_EN | GPIO_LDO_EN;
    } else {
        /* Power-off: open relay + load switch first */
        gpio &= ~(GPIO_OUTPUT_RELAY | GPIO_LOAD_SWITCH | GPIO_EXT_LOAD_SW);
    }

    k_mutex_lock(&i2c_lock[bidx], K_FOREVER);

    int ret = i2c_mux_select(cell_id);
    if (ret != 0) {
        k_mutex_unlock(&i2c_lock[bidx]);
        return ret;
    }

    ret = tca6408_write_output(bus, gpio);
    if (ret != 0) {
        LOG_ERR("Cell %d: GPIO write failed: %d", cell_id, ret);
        i2c_mux_deselect(cell_id);
        k_mutex_unlock(&i2c_lock[bidx]);
        return ret;
    }
    gpio_shadow[cell_id] = gpio;

    if (enable) {
        /* Wait for regulators to stabilize, then close relay */
        k_sleep(K_MSEC(5));
        gpio |= GPIO_OUTPUT_RELAY;
        ret = tca6408_write_output(bus, gpio);
        gpio_shadow[cell_id] = gpio;
    } else {
        /* Disable regulators after relay opened */
        k_sleep(K_MSEC(2));
        gpio &= ~(GPIO_BUCK_EN | GPIO_LDO_EN);
        ret = tca6408_write_output(bus, gpio);
        gpio_shadow[cell_id] = gpio;
    }

    i2c_mux_deselect(cell_id);
    k_mutex_unlock(&i2c_lock[bidx]);

    cells[cell_id].output_enabled = enable;
    if (enable) {
        cells[cell_id].flags |= CELL_FLAG_OUTPUT_EN | CELL_FLAG_RELAY_CLOSED;
    } else {
        cells[cell_id].flags &= ~(CELL_FLAG_OUTPUT_EN | CELL_FLAG_RELAY_CLOSED);
    }

    return ret;
}

int cell_set_mode(uint8_t cell_id, bool four_wire)
{
    if (cell_id >= CELL_COUNT) {
        return -EINVAL;
    }

    int bidx = i2c_idx(cell_id);
    const struct device *bus = i2c_bus[bidx];
    uint8_t gpio = gpio_shadow[cell_id];

    if (four_wire) {
        gpio |= GPIO_FOUR_WIRE;
    } else {
        gpio &= ~GPIO_FOUR_WIRE;
    }

    k_mutex_lock(&i2c_lock[bidx], K_FOREVER);

    int ret = i2c_mux_select(cell_id);
    if (ret != 0) {
        k_mutex_unlock(&i2c_lock[bidx]);
        return ret;
    }

    ret = tca6408_write_output(bus, gpio);

    i2c_mux_deselect(cell_id);
    k_mutex_unlock(&i2c_lock[bidx]);

    if (ret == 0) {
        gpio_shadow[cell_id] = gpio;
        cells[cell_id].four_wire = four_wire;
        if (four_wire) {
            cells[cell_id].flags |= CELL_FLAG_FOUR_WIRE;
        } else {
            cells[cell_id].flags &= ~CELL_FLAG_FOUR_WIRE;
        }
    }

    return ret;
}

int cell_read_measurements(uint8_t cell_id, struct cell_state *state)
{
    if (cell_id >= CELL_COUNT || state == NULL) {
        return -EINVAL;
    }

    int ret;
    int32_t ch_data[4];

    /* SPI: read all 4 ADC channels (simultaneous conversion) */
    int sidx = spi_idx(cell_id);
    k_mutex_lock(&spi_lock[sidx], K_FOREVER);

    ret = ads131_read_channels(cell_id, ch_data);

    k_mutex_unlock(&spi_lock[sidx]);

    if (ret == 0) {
        state->buck_mv    = adc_raw_to_signal_mv(ch_data[ADC_CH_VBUCK], ADC_VDIV_VOLTAGE);
        state->ldo_mv     = adc_raw_to_signal_mv(ch_data[ADC_CH_VLDO],  ADC_VDIV_VOLTAGE);
        state->current_ua = adc_raw_to_current_ua(ch_data[ADC_CH_ISENSE]);
        state->voltage_mv = adc_raw_to_signal_mv(ch_data[ADC_CH_VSENSE], ADC_VDIV_VOLTAGE);

        /* Over-current check */
        if (state->current_ua > OVERCURRENT_UA) {
            state->flags |= CELL_FLAG_OVER_CURRENT;
        } else {
            state->flags &= ~CELL_FLAG_OVER_CURRENT;
        }
    }

    /* I2C: read temperature sensor */
    int bidx = i2c_idx(cell_id);
    k_mutex_lock(&i2c_lock[bidx], K_FOREVER);

    ret = i2c_mux_select(cell_id);
    if (ret == 0) {
        int16_t temp_c10;
        ret = tmp117_read(cell_i2c(cell_id), &temp_c10);
        if (ret == 0) {
            state->temp_c10 = temp_c10;
            if (temp_c10 > OVERTEMP_C10) {
                state->flags |= CELL_FLAG_OVER_TEMP;
            } else {
                state->flags &= ~CELL_FLAG_OVER_TEMP;
            }
        }
        i2c_mux_deselect(cell_id);
    }

    k_mutex_unlock(&i2c_lock[bidx]);

    /* Merge setpoint-side fields from cached state */
    state->setpoint_mv = cells[cell_id].setpoint_mv;
    state->output_enabled = cells[cell_id].output_enabled;
    state->four_wire = cells[cell_id].four_wire;
    state->flags = (state->flags & (CELL_FLAG_OVER_TEMP | CELL_FLAG_OVER_CURRENT)) |
                   (cells[cell_id].flags & ~(CELL_FLAG_OVER_TEMP | CELL_FLAG_OVER_CURRENT));
    state->selftest_passed = cells[cell_id].selftest_passed;

    /* Cache measurements */
    cells[cell_id].voltage_mv = state->voltage_mv;
    cells[cell_id].current_ua = state->current_ua;
    cells[cell_id].buck_mv = state->buck_mv;
    cells[cell_id].ldo_mv = state->ldo_mv;
    cells[cell_id].temp_c10 = state->temp_c10;

    return 0;
}

const struct cell_state *cell_get_state(uint8_t cell_id)
{
    if (cell_id >= CELL_COUNT) {
        return NULL;
    }
    return &cells[cell_id];
}

void cell_safe_state_all(void)
{
    LOG_WRN("Entering safe state: all cells OFF");

    for (uint8_t id = 0; id < CELL_COUNT; id++) {
        int bidx = i2c_idx(id);
        const struct device *bus = i2c_bus[bidx];

        k_mutex_lock(&i2c_lock[bidx], K_FOREVER);

        if (i2c_mux_select(id) == 0) {
            tca6408_write_output(bus, 0x00);
            gpio_shadow[id] = 0x00;

            mcp4725_write(bus, MCP4725_BUCK_ADDR, 0);
            mcp4725_write(bus, MCP4725_LDO_ADDR, 0);

            i2c_mux_deselect(id);
        }

        k_mutex_unlock(&i2c_lock[bidx]);

        cells[id].setpoint_mv = 0;
        cells[id].output_enabled = false;
        cells[id].flags &= ~(CELL_FLAG_OUTPUT_EN | CELL_FLAG_RELAY_CLOSED |
                              CELL_FLAG_LOAD_SW_ON);
    }
}
