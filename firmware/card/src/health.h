/*
 * CellSim Card — Health Monitoring
 *
 * Reads STM32H7 internal sensors:
 *   - Die temperature (ADC3 internal channel 18)
 *   - Vrefint (ADC3 internal channel 19, for supply voltage estimation)
 *   - 24V input voltage via external resistor divider (ADC3 external channel)
 *
 * Health data is included in every UDP measurement packet.
 */

#ifndef CELLSIM_HEALTH_H
#define CELLSIM_HEALTH_H

#include <stdint.h>

/* Voltage divider ratio: 10k / (100k + 10k) = 1/11 */
#define VMON_DIVIDER_RATIO  11.0f

/* ADC reference voltage (V) */
#define ADC_VREF            3.3f

/* ADC resolution (12-bit) */
#define ADC_RESOLUTION      4096

/**
 * Initialize the health monitoring subsystem.
 * Configures ADC3 for internal temperature, Vrefint, and input voltage channels.
 *
 * @return 0 on success, negative errno on error
 */
int health_init(void);

/**
 * Read STM32H7 internal die temperature.
 *
 * @return Temperature in °C
 */
float health_get_mcu_temp(void);

/**
 * Read Vrefint (internal reference voltage).
 * Can be used to verify ADC calibration.
 *
 * @return Vrefint in V (nominally 1.21V)
 */
float health_get_vrefint(void);

/**
 * Read 24V input voltage via resistor divider.
 * Divider: 100k/10k = 1:11 ratio.
 *
 * @return Input voltage in V
 */
float health_get_input_voltage(void);

/**
 * Health data structure for inclusion in measurement packets.
 */
struct health_data {
    float mcu_temp_celsius;
    float vrefint_volts;
    float input_voltage;
    uint32_t uptime_ms;
};

/**
 * Get all health data in one call.
 */
void health_get_all(struct health_data *data);

#endif /* CELLSIM_HEALTH_H */
