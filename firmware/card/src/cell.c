/*
 * CellSim Card — Cell Control HAL Implementation
 *
 * Controls per-cell voltage/current through I2C devices behind TCA9548A mux.
 * Each cell has: 2× MCP4725 DAC, ADS1115 ADC, TCA6408 GPIO, TMP117 temp.
 */

#include "cell.h"
#include "self_test.h"  /* for CELLSIM_NUM_I2C_BUSES, bus aliases */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(cell, LOG_LEVEL_INF);

/* I2C addresses (isolated side, behind TCA9548A) */
#define TCA9548A_ADDR       0x70
#define MCP4725_BUCK_ADDR   0x60
#define MCP4725_LDO_ADDR    0x61
#define ADS1115_ADDR        0x48
#define TCA6408_ADDR        0x20
#define TMP117_ADDR         0x49  /* ADDR pin pulled to VDD to avoid ADS1115 conflict */

/* TCA6408 registers */
#define TCA6408_INPUT       0x00
#define TCA6408_OUTPUT      0x01
#define TCA6408_POLARITY    0x02
#define TCA6408_CONFIG      0x03  /* 0 = output, 1 = input */

/* ADS1115 registers */
#define ADS1115_REG_CONV    0x00
#define ADS1115_REG_CONFIG  0x01

/* ADS1115 config bits: single-shot, ±4.096V, 128 SPS */
#define ADS1115_CFG_OS      (1 << 15)  /* Start conversion */
#define ADS1115_CFG_PGA_4V  (1 << 9)   /* ±4.096V (gain = 1) */
#define ADS1115_CFG_MODE_SS (1 << 8)   /* Single-shot */
#define ADS1115_CFG_DR_128  (4 << 5)   /* 128 SPS */

/* MCP4725 write command (fast mode, write DAC only) */
#define MCP4725_CMD_WRITE_DAC  0x40

/* TMP117 registers */
#define TMP117_REG_TEMP     0x00

/* Buck converter parameters */
#define BUCK_MAX_MV         5500  /* Buck can overshoot to allow LDO headroom */
#define LDO_HEADROOM_MV     300   /* LDO needs at least 300 mV above output */

/* DAC parameters: MCP4725 is 12-bit, Vref = 3.3V
 * DAC output scales the buck/LDO setpoint via feedback network.
 * Mapping: 0 mV output = DAC code 0, 5000 mV output = DAC code 4095 */
#define DAC_MAX_CODE        4095
#define DAC_MAX_OUTPUT_MV   5000

/* I2C bus device references */
static const struct device *i2c_bus[CELLSIM_NUM_I2C_BUSES];

/* Per-cell state */
static struct cell_state cells[CELL_COUNT];

/* Per-cell TCA6408 output register shadow (avoid read-modify-write) */
static uint8_t gpio_shadow[CELL_COUNT];

/* Mutex for I2C bus access (one per bus) */
static struct k_mutex bus_lock[CELLSIM_NUM_I2C_BUSES];

/* ── Helpers ─────────────────────────────────────────────────────── */

static inline int bus_index(uint8_t cell_id)
{
    return cell_id / CELLS_PER_BUS;
}

static inline int mux_channel(uint8_t cell_id)
{
    return cell_id % CELLS_PER_BUS;
}

static const struct device *cell_bus(uint8_t cell_id)
{
    return i2c_bus[bus_index(cell_id)];
}

static int tca9548a_select(const struct device *bus, uint8_t channel)
{
    uint8_t mask = (channel < 8) ? (1 << channel) : 0x00;
    return i2c_write(bus, &mask, 1, TCA9548A_ADDR);
}

static int mux_select(uint8_t cell_id)
{
    return tca9548a_select(cell_bus(cell_id), mux_channel(cell_id));
}

static int mux_deselect(uint8_t cell_id)
{
    return tca9548a_select(cell_bus(cell_id), 0xFF);
}

/* ── MCP4725 DAC ─────────────────────────────────────────────────── */

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

/* ── ADS1115 ADC ─────────────────────────────────────────────────── */

static int ads1115_read_channel(const struct device *bus, uint8_t channel,
                                 int16_t *raw_out)
{
    /* Build config word: single-shot, MUX for single-ended channel, ±4.096V */
    uint16_t config = ADS1115_CFG_OS
                    | ((0x04 | (channel & 0x03)) << 12)  /* MUX: AINx vs GND */
                    | ADS1115_CFG_PGA_4V
                    | ADS1115_CFG_MODE_SS
                    | ADS1115_CFG_DR_128
                    | 0x03;  /* Disable comparator */

    uint8_t cfg_buf[3] = {
        ADS1115_REG_CONFIG,
        (uint8_t)(config >> 8),
        (uint8_t)(config & 0xFF),
    };

    int ret = i2c_write(bus, cfg_buf, sizeof(cfg_buf), ADS1115_ADDR);
    if (ret != 0) {
        return ret;
    }

    /* Wait for conversion (~8 ms at 128 SPS) */
    k_sleep(K_MSEC(9));

    /* Read conversion result */
    uint8_t reg = ADS1115_REG_CONV;
    uint8_t data[2];
    ret = i2c_write_read(bus, ADS1115_ADDR, &reg, 1, data, 2);
    if (ret != 0) {
        return ret;
    }

    *raw_out = (int16_t)((data[0] << 8) | data[1]);
    return 0;
}

/* Convert ADS1115 raw to millivolts (±4.096V range, 1 LSB = 0.125 mV) */
static uint16_t adc_raw_to_mv(int16_t raw)
{
    if (raw < 0) {
        return 0;
    }
    /* 4096 mV / 32768 counts = 0.125 mV/count */
    return (uint16_t)(((uint32_t)raw * 4096) / 32768);
}

/* ── TCA6408 GPIO Expander ───────────────────────────────────────── */

static int tca6408_write_output(const struct device *bus, uint8_t value)
{
    uint8_t buf[2] = { TCA6408_OUTPUT, value };
    return i2c_write(bus, buf, sizeof(buf), TCA6408_ADDR);
}

static int tca6408_init_cell(const struct device *bus)
{
    /* Configure all used bits as outputs (0 = output) */
    uint8_t config_mask = (uint8_t)~(GPIO_BUCK_EN | GPIO_LDO_EN |
                                      GPIO_OUTPUT_RELAY | GPIO_LOAD_SWITCH |
                                      GPIO_FOUR_WIRE);
    uint8_t buf[2] = { TCA6408_CONFIG, config_mask };
    int ret = i2c_write(bus, buf, sizeof(buf), TCA6408_ADDR);
    if (ret != 0) {
        return ret;
    }

    /* All outputs off initially */
    return tca6408_write_output(bus, 0x00);
}

/* ── TMP117 Temperature Sensor ───────────────────────────────────── */

static int tmp117_read(const struct device *bus, int16_t *temp_c10)
{
    uint8_t reg = TMP117_REG_TEMP;
    uint8_t data[2];
    int ret = i2c_write_read(bus, TMP117_ADDR, &reg, 1, data, 2);
    if (ret != 0) {
        return ret;
    }
    /* TMP117: 7.8125 m°C/LSB. To get °C×10: raw * 78125 / 1000000 */
    int16_t raw = (int16_t)((data[0] << 8) | data[1]);
    *temp_c10 = (int16_t)((int32_t)raw * 78125 / 1000000);
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────── */

int cell_init(void)
{
    /* Get I2C bus device references */
    i2c_bus[0] = DEVICE_DT_GET(DT_ALIAS(i2c_cells_a));
    i2c_bus[1] = DEVICE_DT_GET(DT_ALIAS(i2c_cells_b));

    for (int i = 0; i < CELLSIM_NUM_I2C_BUSES; i++) {
        if (!device_is_ready(i2c_bus[i])) {
            LOG_ERR("I2C bus %d not ready", i);
            return -ENODEV;
        }
        k_mutex_init(&bus_lock[i]);
    }

    /* Initialize each cell: configure GPIO expander, zero DACs */
    for (uint8_t id = 0; id < CELL_COUNT; id++) {
        int bidx = bus_index(id);
        const struct device *bus = i2c_bus[bidx];

        k_mutex_lock(&bus_lock[bidx], K_FOREVER);

        int ret = mux_select(id);
        if (ret != 0) {
            LOG_ERR("Cell %d: mux select failed: %d", id, ret);
            k_mutex_unlock(&bus_lock[bidx]);
            continue;  /* Best-effort: try remaining cells */
        }

        /* Configure GPIO expander (all outputs low = safe) */
        ret = tca6408_init_cell(bus);
        if (ret != 0) {
            LOG_WRN("Cell %d: GPIO init failed: %d", id, ret);
        }
        gpio_shadow[id] = 0x00;

        /* Zero both DACs */
        mcp4725_write(bus, MCP4725_BUCK_ADDR, 0);
        mcp4725_write(bus, MCP4725_LDO_ADDR, 0);

        mux_deselect(id);
        k_mutex_unlock(&bus_lock[bidx]);

        memset(&cells[id], 0, sizeof(cells[id]));
    }

    LOG_INF("Cell HAL initialized (%d cells)", CELL_COUNT);
    return 0;
}

int cell_set_voltage(uint8_t cell_id, uint16_t mv)
{
    if (cell_id >= CELL_COUNT || mv > DAC_MAX_OUTPUT_MV) {
        return -EINVAL;
    }

    int bidx = bus_index(cell_id);
    const struct device *bus = i2c_bus[bidx];

    /* Buck setpoint: output voltage + LDO headroom */
    uint16_t buck_mv = mv + LDO_HEADROOM_MV;
    if (buck_mv > BUCK_MAX_MV) {
        buck_mv = BUCK_MAX_MV;
    }

    uint16_t buck_code = mv_to_dac(buck_mv);
    uint16_t ldo_code = mv_to_dac(mv);

    k_mutex_lock(&bus_lock[bidx], K_FOREVER);

    int ret = mux_select(cell_id);
    if (ret != 0) {
        k_mutex_unlock(&bus_lock[bidx]);
        return ret;
    }

    ret = mcp4725_write(bus, MCP4725_BUCK_ADDR, buck_code);
    if (ret != 0) {
        LOG_ERR("Cell %d: buck DAC write failed: %d", cell_id, ret);
        mux_deselect(cell_id);
        k_mutex_unlock(&bus_lock[bidx]);
        return ret;
    }

    ret = mcp4725_write(bus, MCP4725_LDO_ADDR, ldo_code);
    if (ret != 0) {
        LOG_ERR("Cell %d: LDO DAC write failed: %d", cell_id, ret);
    }

    mux_deselect(cell_id);
    k_mutex_unlock(&bus_lock[bidx]);

    cells[cell_id].setpoint_mv = mv;
    return ret;
}

int cell_set_output(uint8_t cell_id, bool enable)
{
    if (cell_id >= CELL_COUNT) {
        return -EINVAL;
    }

    int bidx = bus_index(cell_id);
    const struct device *bus = i2c_bus[bidx];

    uint8_t gpio = gpio_shadow[cell_id];

    if (enable) {
        /* Power-on sequence: buck → LDO → relay */
        gpio |= GPIO_BUCK_EN | GPIO_LDO_EN;
    } else {
        /* Power-off sequence: relay first → LDO → buck */
        gpio &= ~(GPIO_OUTPUT_RELAY | GPIO_LOAD_SWITCH);
    }

    k_mutex_lock(&bus_lock[bidx], K_FOREVER);
    int ret = mux_select(cell_id);
    if (ret != 0) {
        k_mutex_unlock(&bus_lock[bidx]);
        return ret;
    }

    ret = tca6408_write_output(bus, gpio);
    if (ret != 0) {
        LOG_ERR("Cell %d: GPIO write failed: %d", cell_id, ret);
        mux_deselect(cell_id);
        k_mutex_unlock(&bus_lock[bidx]);
        return ret;
    }
    gpio_shadow[cell_id] = gpio;

    if (enable) {
        /* Short delay for regulators to stabilize, then close relay */
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

    mux_deselect(cell_id);
    k_mutex_unlock(&bus_lock[bidx]);

    cells[cell_id].output_enabled = enable;

    /* Update flags */
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

    int bidx = bus_index(cell_id);
    const struct device *bus = i2c_bus[bidx];

    uint8_t gpio = gpio_shadow[cell_id];
    if (four_wire) {
        gpio |= GPIO_FOUR_WIRE;
    } else {
        gpio &= ~GPIO_FOUR_WIRE;
    }

    k_mutex_lock(&bus_lock[bidx], K_FOREVER);
    int ret = mux_select(cell_id);
    if (ret != 0) {
        k_mutex_unlock(&bus_lock[bidx]);
        return ret;
    }

    ret = tca6408_write_output(bus, gpio);

    mux_deselect(cell_id);
    k_mutex_unlock(&bus_lock[bidx]);

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

    int bidx = bus_index(cell_id);
    const struct device *bus = i2c_bus[bidx];
    int16_t raw;
    int ret;

    k_mutex_lock(&bus_lock[bidx], K_FOREVER);

    ret = mux_select(cell_id);
    if (ret != 0) {
        k_mutex_unlock(&bus_lock[bidx]);
        return ret;
    }

    /* Read 4 ADC channels */
    ret = ads1115_read_channel(bus, ADC_CH_VOUT, &raw);
    if (ret == 0) {
        state->voltage_mv = adc_raw_to_mv(raw);
    }

    ret = ads1115_read_channel(bus, ADC_CH_IOUT, &raw);
    if (ret == 0) {
        /* I = V/R; shunt_mv / R [Ω] = mA; × 1000 = µA */
        uint16_t shunt_mv = adc_raw_to_mv(raw);
        state->current_ua = (uint32_t)((float)shunt_mv / ADC_IOUT_SHUNT_R * 1000.0f);
    }

    ret = ads1115_read_channel(bus, ADC_CH_VBUCK, &raw);
    if (ret == 0) {
        state->buck_mv = adc_raw_to_mv(raw);
    }

    ret = ads1115_read_channel(bus, ADC_CH_VLDO, &raw);
    if (ret == 0) {
        state->ldo_mv = adc_raw_to_mv(raw);
    }

    /* Over-current check */
    if (state->current_ua > OVERCURRENT_UA) {
        state->flags |= CELL_FLAG_OVER_CURRENT;
    } else {
        state->flags &= ~CELL_FLAG_OVER_CURRENT;
    }

    /* Read temperature */
    int16_t temp_c10;
    ret = tmp117_read(bus, &temp_c10);
    if (ret == 0) {
        state->temp_c10 = temp_c10;
        if (temp_c10 > OVERTEMP_C10) {
            state->flags |= CELL_FLAG_OVER_TEMP;
        } else {
            state->flags &= ~CELL_FLAG_OVER_TEMP;
        }
    }

    mux_deselect(cell_id);
    k_mutex_unlock(&bus_lock[bidx]);

    /* Copy setpoint-side fields from cached state */
    state->setpoint_mv = cells[cell_id].setpoint_mv;
    state->output_enabled = cells[cell_id].output_enabled;
    state->four_wire = cells[cell_id].four_wire;
    state->flags = (state->flags & (CELL_FLAG_OVER_TEMP | CELL_FLAG_OVER_CURRENT)) |
                   (cells[cell_id].flags & ~(CELL_FLAG_OVER_TEMP | CELL_FLAG_OVER_CURRENT));
    state->selftest_passed = cells[cell_id].selftest_passed;

    /* Cache the measurements */
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
        int bidx = bus_index(id);
        const struct device *bus = i2c_bus[bidx];

        k_mutex_lock(&bus_lock[bidx], K_FOREVER);

        if (mux_select(id) == 0) {
            /* Open relay first, then disable regulators */
            tca6408_write_output(bus, 0x00);
            gpio_shadow[id] = 0x00;

            /* Zero both DACs */
            mcp4725_write(bus, MCP4725_BUCK_ADDR, 0);
            mcp4725_write(bus, MCP4725_LDO_ADDR, 0);

            mux_deselect(id);
        }

        k_mutex_unlock(&bus_lock[bidx]);

        /* Update state */
        cells[id].setpoint_mv = 0;
        cells[id].output_enabled = false;
        cells[id].flags &= ~(CELL_FLAG_OUTPUT_EN | CELL_FLAG_RELAY_CLOSED |
                              CELL_FLAG_LOAD_SW_ON);
    }
}
