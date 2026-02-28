/*
 * CellSim Card — Cell Control HAL
 *
 * Hardware abstraction for per-cell voltage/current control.
 *
 * Per-cell topology (isolated side):
 *   I2C path (behind TCA9548A mux + ISO1640 isolator):
 *     MCP4725 (0x61) — Buck converter DAC setpoint (A0=VIN)
 *     MCP4725 (0x60) — LDO fine-tune DAC setpoint (A0=GND)
 *     TCA6408 (0x20) — GPIO expander (relay, enable, cal, load, 4wire)
 *     TMP117  (0x49) — Temperature sensor (ADDR=VDD)
 *
 *   SPI path (behind ISO7741 isolator, per-cell CS):
 *     ADS131M04 — 24-bit 4-channel simultaneous ADC (Vbuck, Vldo, Isense, Vsense)
 *
 * Bus architecture:
 *   I2C1 → TCA9548A → Cells 0–3 (DACs + GPIO + temp)
 *   I2C2 → TCA9548A → Cells 4–7 (DACs + GPIO + temp)
 *   SPI1 → ISO7741 ×4 → Cells 0–3 ADC (MISO via CD74HC4052 mux)
 *   SPI2 → ISO7741 ×4 → Cells 4–7 ADC (MISO via CD74HC4052 mux)
 */

#ifndef CELLSIM_CELL_H
#define CELLSIM_CELL_H

#include <stdbool.h>
#include <stdint.h>

#define CELL_COUNT          8
#define CELLS_PER_BUS       4

/* ── TCA6408 GPIO bit assignments (spec §2.5) ─────────────────── */
#define GPIO_DMM_CAL        BIT(0)  /* DMM calibration relay */
/* bit 1 spare */
#define GPIO_BUCK_EN        BIT(2)  /* Buck converter enable */
#define GPIO_LDO_EN         BIT(3)  /* LDO enable */
#define GPIO_LOAD_SWITCH    BIT(4)  /* Internal load switch (discharge) */
#define GPIO_OUTPUT_RELAY   BIT(5)  /* Output DPDT relay */
#define GPIO_EXT_LOAD_SW    BIT(6)  /* External load switch */
#define GPIO_FOUR_WIRE      BIT(7)  /* 2-wire/4-wire mode select */

/* Convenience: all power-stage enables */
#define GPIO_ALL_ENABLES    (GPIO_BUCK_EN | GPIO_LDO_EN | GPIO_OUTPUT_RELAY | \
                             GPIO_LOAD_SWITCH | GPIO_EXT_LOAD_SW)

/* ── ADS131M04 channel assignments (spec §2.5, §4.2.8) ────────── */
#define ADC_CH_VBUCK        0   /* Buck output voltage (1:5 divider) */
#define ADC_CH_VLDO         1   /* LDO output voltage (1:5 divider) */
#define ADC_CH_ISENSE       2   /* Current sense amp output (2:5 divider) */
#define ADC_CH_VSENSE       3   /* Cell output voltage / Kelvin sense (1:5 divider) */

/* ── ADC scaling ───────────────────────────────────────────────── */
/*
 * ADS131M04: 24-bit delta-sigma, internal 1.2V reference, gain=1
 * Single-ended usable range: 0 to +1.2V
 * LSB = 1.2V / 2^23 = 143 nV
 *
 * Voltage dividers scale cell-domain signals into 0–1.2V:
 *   CH0/1/3 (voltage): 1:5 divider → 5V signal → 1.0V at ADC
 *   CH2 (current):     INA185 50× gain, 50mΩ shunt → 2:5 divider
 */
#define ADC_VREF_MV         1200    /* Internal reference 1.2V */
#define ADC_FULL_SCALE      8388608 /* 2^23 (24-bit, positive half) */

/* Voltage divider ratios (signal → ADC input) */
#define ADC_VDIV_VOLTAGE    5.0f    /* 1:5 for buck/LDO/sense channels */
#define ADC_VDIV_CURRENT    2.5f    /* 2:5 for current sense channel */

/* Current sensing */
#define ADC_IOUT_SHUNT_R    0.05f   /* 50 mΩ shunt resistor */
#define ADC_IOUT_INA_GAIN   50.0f   /* INA185A2 gain */

/* ── Protection thresholds ─────────────────────────────────────── */
#define OVERCURRENT_UA      1500000 /* 1.5 A — trip threshold */
#define OVERTEMP_C10        850     /* 85.0 °C */

/* ── Measurement flags (matches Python CellFlags in protocol.py) ─ */
#define CELL_FLAG_OUTPUT_EN     0x01
#define CELL_FLAG_RELAY_CLOSED  0x02
#define CELL_FLAG_LOAD_SW_ON    0x04
#define CELL_FLAG_FOUR_WIRE     0x08
#define CELL_FLAG_SELFTEST_PASS 0x10
#define CELL_FLAG_OVER_CURRENT  0x20
#define CELL_FLAG_OVER_TEMP     0x40

/* ── Per-cell runtime state ────────────────────────────────────── */
struct cell_state {
    /* Setpoints (written by CM5) */
    uint16_t setpoint_mv;       /* Target voltage in mV */
    bool     output_enabled;    /* Output relay + buck/LDO */
    bool     four_wire;         /* Kelvin mode */

    /* Measurements (read from SPI ADC) */
    uint16_t voltage_mv;        /* Measured output voltage (sense line) */
    uint32_t current_ua;        /* Measured output current */
    uint16_t buck_mv;           /* Buck converter output */
    uint16_t ldo_mv;            /* LDO output */
    int16_t  temp_c10;          /* Temperature × 10 (e.g., 251 = 25.1°C) */

    /* Status flags (CellFlags bitfield) */
    uint8_t  flags;

    /* Self-test result */
    bool     selftest_passed;
};

/**
 * Initialize cell control hardware.
 * Must be called after I2C and SPI buses are ready.
 *
 * @return 0 on success, negative errno on error
 */
int cell_init(void);

/**
 * Set the output voltage setpoint for a cell.
 * Writes to both buck DAC (tracking) and LDO DAC.
 *
 * @param cell_id  0–7
 * @param mv       Voltage in millivolts (0–5000)
 * @return 0 on success, negative errno on error
 */
int cell_set_voltage(uint8_t cell_id, uint16_t mv);

/**
 * Enable or disable cell output (relay + power stages).
 *
 * @param cell_id  0–7
 * @param enable   true = close relay + enable buck/LDO
 * @return 0 on success, negative errno on error
 */
int cell_set_output(uint8_t cell_id, bool enable);

/**
 * Set Kelvin sensing mode (2-wire or 4-wire).
 *
 * @param cell_id   0–7
 * @param four_wire true = 4-wire Kelvin mode
 * @return 0 on success, negative errno on error
 */
int cell_set_mode(uint8_t cell_id, bool four_wire);

/**
 * Read all measurements for a single cell.
 * Reads SPI ADC (4 channels) + I2C temp sensor.
 *
 * @param cell_id  0–7
 * @param state    Output: updated with latest measurements
 * @return 0 on success, negative errno on error
 */
int cell_read_measurements(uint8_t cell_id, struct cell_state *state);

/**
 * Get the current state of a cell (cached from last read).
 *
 * @param cell_id  0–7
 * @return Pointer to cell state, or NULL if invalid
 */
const struct cell_state *cell_get_state(uint8_t cell_id);

/**
 * Enter safe state: zero all DACs, open all relays for all cells.
 */
void cell_safe_state_all(void);

#endif /* CELLSIM_CELL_H */
