/*
 * CellSim Card — Health Monitoring Implementation
 */

#include "health.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(health, LOG_LEVEL_INF);

/*
 * ADC3 channel configuration for STM32H7:
 *   - Internal temperature sensor: channel 18
 *   - Vrefint: channel 19
 *   - Input voltage divider: external pin (channel TBD, depends on PCB)
 *
 * TODO: Finalize ADC channel assignment when PCB pin assignment is done.
 *       Using placeholder channel 0 for input voltage.
 */

#define ADC_TEMP_CHANNEL    18
#define ADC_VREFINT_CHANNEL 19
#define ADC_VIN_CHANNEL     0   /* TODO: assign actual pin/channel */

/* STM32H7 temperature calibration values (from datasheet) */
#define TEMP_CAL1_TEMP  30.0f   /* Calibration point 1: 30°C */
#define TEMP_CAL1_VREF  3.3f    /* At Vref = 3.3V */

/* ADC device */
static const struct device *adc_dev;
static bool adc_initialized;

/* ADC channel configs */
static struct adc_channel_cfg temp_channel_cfg = {
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 480),
    .channel_id = ADC_TEMP_CHANNEL,
    .differential = 0,
};

static struct adc_channel_cfg vrefint_channel_cfg = {
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 480),
    .channel_id = ADC_VREFINT_CHANNEL,
    .differential = 0,
};

static struct adc_channel_cfg vin_channel_cfg = {
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 64),
    .channel_id = ADC_VIN_CHANNEL,
    .differential = 0,
};

static int16_t adc_read_channel(uint8_t channel_id)
{
    int16_t sample_buffer = 0;
    struct adc_sequence sequence = {
        .channels = BIT(channel_id),
        .buffer = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution = 12,
    };

    int ret = adc_read(adc_dev, &sequence);
    if (ret != 0) {
        LOG_ERR("ADC read channel %d failed: %d", channel_id, ret);
        return 0;
    }

    return sample_buffer;
}

int health_init(void)
{
    adc_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(adc3));
    if (adc_dev == NULL || !device_is_ready(adc_dev)) {
        LOG_WRN("ADC3 not available, health monitoring disabled");
        adc_initialized = false;
        return -ENODEV;
    }

    int ret;

    ret = adc_channel_setup(adc_dev, &temp_channel_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to setup temp ADC channel: %d", ret);
        return ret;
    }

    ret = adc_channel_setup(adc_dev, &vrefint_channel_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to setup Vrefint ADC channel: %d", ret);
        return ret;
    }

    ret = adc_channel_setup(adc_dev, &vin_channel_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to setup Vin ADC channel: %d", ret);
        return ret;
    }

    adc_initialized = true;
    LOG_INF("Health monitoring initialized (ADC3)");
    return 0;
}

float health_get_mcu_temp(void)
{
    if (!adc_initialized) {
        return 0.0f;
    }

    int16_t raw = adc_read_channel(ADC_TEMP_CHANNEL);

    /*
     * STM32H7 internal temp sensor conversion (simplified):
     * V_sense = raw * Vref / ADC_RESOLUTION
     * Temp = ((V_sense - V_25) / Avg_Slope) + 25
     *
     * Typical values from datasheet:
     *   V_25 = 0.76V (voltage at 25°C)
     *   Avg_Slope = 2.5 mV/°C
     */
    float v_sense = (float)raw * ADC_VREF / (float)ADC_RESOLUTION;
    float temp = ((v_sense - 0.76f) / 0.0025f) + 25.0f;

    return temp;
}

float health_get_vrefint(void)
{
    if (!adc_initialized) {
        return 0.0f;
    }

    int16_t raw = adc_read_channel(ADC_VREFINT_CHANNEL);
    float vrefint = (float)raw * ADC_VREF / (float)ADC_RESOLUTION;

    return vrefint;
}

float health_get_input_voltage(void)
{
    if (!adc_initialized) {
        return 0.0f;
    }

    int16_t raw = adc_read_channel(ADC_VIN_CHANNEL);
    float v_adc = (float)raw * ADC_VREF / (float)ADC_RESOLUTION;
    float v_in = v_adc * VMON_DIVIDER_RATIO;

    return v_in;
}

void health_get_all(struct health_data *data)
{
    if (data == NULL) {
        return;
    }

    data->mcu_temp_celsius = health_get_mcu_temp();
    data->vrefint_volts = health_get_vrefint();
    data->input_voltage = health_get_input_voltage();
    data->uptime_ms = k_uptime_get_32();
}
