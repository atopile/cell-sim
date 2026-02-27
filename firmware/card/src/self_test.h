/*
 * CellSim Card — Self-Test (BIST)
 *
 * Per-peripheral built-in self-test run at boot and on demand.
 * Tests: I2C bus scan, per-cell device verification through TCA9548A,
 * DAC→ADC loopback, GPIO readback, temp sensor validation, relay verify.
 */

#ifndef CELLSIM_SELF_TEST_H
#define CELLSIM_SELF_TEST_H

#include <stdbool.h>
#include <stdint.h>

#define CELLSIM_NUM_CELLS 8
#define CELLSIM_NUM_I2C_BUSES 2
#define CELLSIM_CELLS_PER_BUS 4

/* Per-cell self-test result */
struct cell_test_result {
    bool i2c_isolator_ok;   /* ISO1640 responds */
    bool dac_buck_ok;       /* MCP4725 (buck setpoint) responds + DAC→ADC loopback */
    bool dac_ldo_ok;        /* MCP4725 (LDO setpoint) responds + DAC→ADC loopback */
    bool adc_ok;            /* ADS1115/ADS1219 responds + reference check */
    bool gpio_ok;           /* TCA6408 responds + readback verify */
    bool temp_ok;           /* TMP117 responds + plausible reading (-20 to 85°C) */
    bool relay_ok;          /* Relay toggle: output voltage change detected */
    float temp_celsius;     /* Measured temperature */
};

/* Overall self-test result */
struct self_test_result {
    bool i2c_bus_ok[CELLSIM_NUM_I2C_BUSES];
    bool tca9548a_ok[CELLSIM_NUM_I2C_BUSES];
    bool phy_ok;
    float mcu_temp_celsius;
    float input_voltage;
    struct cell_test_result cells[CELLSIM_NUM_CELLS];
    bool all_passed;
};

/**
 * Run full self-test on all peripherals.
 * Call after I2C buses and ADC are initialized.
 *
 * @param result  Output: populated with per-peripheral pass/fail
 * @return 0 on success (test ran), negative errno on system error
 */
int self_test_run(struct self_test_result *result);

/**
 * Run self-test on a single cell.
 *
 * @param cell_index  0-7
 * @param result      Output: populated with per-device pass/fail
 * @return 0 on success, negative errno on error
 */
int self_test_cell(uint8_t cell_index, struct cell_test_result *result);

#endif /* CELLSIM_SELF_TEST_H */
