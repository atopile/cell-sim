/*
 * CellSim Card — Cell Control HAL
 *
 * Hardware abstraction for per-cell voltage/current control.
 *
 * Per-cell I2C topology (isolated side, behind TCA9548A mux):
 *   MCP4725 (0x60) — Buck converter DAC setpoint
 *   MCP4725 (0x61) — LDO fine-tune DAC setpoint
 *   ADS1115 (0x48) — 4-channel ADC (V_out, I_out, V_buck, V_ldo)
 *   TCA6408 (0x20) — GPIO expander (relay, buck_en, ldo_en, load_sw, 4wire)
 *   TMP117  (0x49) — Temperature sensor (ADDR pin to VDD)
 *
 * Cell addressing:
 *   Cells 0–3: I2C1 → TCA9548A channel 0–3
 *   Cells 4–7: I2C2 → TCA9548A channel 0–3
 */

#ifndef CELLSIM_CELL_H
#define CELLSIM_CELL_H

#include <stdbool.h>
#include <stdint.h>

#define CELL_COUNT          8
#define CELLS_PER_BUS       4

/* TCA6408 GPIO bit assignments */
#define GPIO_BUCK_EN        BIT(0)
#define GPIO_LDO_EN         BIT(1)
#define GPIO_OUTPUT_RELAY   BIT(2)
#define GPIO_LOAD_SWITCH    BIT(3)
#define GPIO_FOUR_WIRE      BIT(4)
/* bits 5–7 reserved */

/* ADS1115 channel assignments */
#define ADC_CH_VOUT         0   /* Output voltage (after relay) */
#define ADC_CH_IOUT         1   /* Output current (shunt voltage) */
#define ADC_CH_VBUCK        2   /* Buck output voltage */
#define ADC_CH_VLDO         3   /* LDO output voltage */

/* ADC scaling */
#define ADC_VOUT_SCALE      1.0f    /* Direct measurement, 0–5V range */
#define ADC_IOUT_SHUNT_R    0.1f    /* 100 mΩ shunt, I = V_shunt / R */
#define ADC_FULL_SCALE_MV   4096    /* ADS1115 ±4.096V range → 1 mV/LSB */

/* Protection thresholds */
#define OVERCURRENT_UA      5000000 /* 5 A — per-cell max before flag set */
#define OVERTEMP_C10        850     /* 85.0 °C */

/* Measurement flags (matches Python CellFlags) */
#define CELL_FLAG_OUTPUT_EN     0x01
#define CELL_FLAG_RELAY_CLOSED  0x02
#define CELL_FLAG_LOAD_SW_ON    0x04
#define CELL_FLAG_FOUR_WIRE     0x08
#define CELL_FLAG_SELFTEST_PASS 0x10
#define CELL_FLAG_OVER_CURRENT  0x20
#define CELL_FLAG_OVER_TEMP     0x40

/* Per-cell runtime state */
struct cell_state {
    /* Setpoints (written by CM5) */
    uint16_t setpoint_mv;       /* Target voltage in mV */
    bool     output_enabled;    /* Output relay + buck/LDO */
    bool     four_wire;         /* Kelvin mode */

    /* Measurements (read from ADC) */
    uint16_t voltage_mv;        /* Measured output voltage */
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
 * Must be called after I2C buses are ready.
 *
 * @return 0 on success, negative errno on error
 */
int cell_init(void);

/**
 * Set the output voltage setpoint for a cell.
 * Writes to both buck DAC and LDO DAC.
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
 * Selects mux channel, reads ADC channels + temp sensor, deselects.
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
