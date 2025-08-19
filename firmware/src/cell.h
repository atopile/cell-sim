#ifndef CELL_H
#define CELL_H

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_MCP4725.h> // DAC
#include <Adafruit_ADS1X15.h> // ADC

class Cell
{
public:
    // Constructor
    Cell(uint8_t mux_channel, TwoWire &wire_interface);

    // Public methods
    void init();
    void enable();
    void disable();
    float getVoltage();
    float getCurrent();
    void setVoltage(float voltage);
    void turnOnOutputRelay();
    void turnOffOutputRelay();
    void turnOnLoadSwitch();
    void turnOffLoadSwitch();
    float getLDOVoltage();
    void setLDOVoltage(float voltage);
    float getBuckVoltage();
    void setBuckVoltage(float voltage);
    void calibrate();
    // Parallel calibration helpers
    void prepareCalibration();
    void setBuckDacRaw(uint16_t setpoint);
    void setLdoDacRaw(uint16_t setpoint);
    void setBuckCalibrationPoint(int index, float measuredVoltage, uint16_t setpoint);
    void setLdoCalibrationPoint(int index, float measuredVoltage, uint16_t setpoint);
    int numCalibrationPoints() const { return NUM_POINTS; }
    uint8_t GPIO_STATE = 0b00000000;

private:
    // Pins
    // ADC
    const int adc_buck_voltage = 0;
    const int adc_ldo_voltage = 1;
    const int adc_output_current = 2;
    const int adc_output_voltage = 3;

    // GPIO Expander
    const int gpio_buck_enable = 2;
    const int gpio_ldo_enable = 3;
    const int gpio_load_switch_control = 4;
    const int gpio_output_relay_control = 5;

    // Hardcoded I2C addresses
    const uint8_t LDO_ADDRESS = 0x60;
    const uint8_t BUCK_ADDRESS = 0x61;
    const uint8_t ADC_ADDRESS = 0x48;
    const uint8_t GPIO_ADDRESS = 0x20;
    const uint8_t MUX_ADDRESS = 0x70;
    const uint8_t TCA6408_ADDR = 0x20;

    // Mux channel
    const uint8_t mux_channel;
    TwoWire &wire;

    // Device instances
    Adafruit_MCP4725 ldo_dac;
    Adafruit_MCP4725 buck_dac;
    Adafruit_ADS1115 adc;

    // Private methods
    uint16_t calculateSetpoint(float voltage, bool useBuckCalibration = true);

    // Helper methods for I2C communication
    void setMuxChannel();
    void setGPIOState();
    float readShuntCurrent();

    // Shunt resistor
    const float SHUNT_RESISTOR_OHMS = 0.11128; // With 50V/V gain: 0.523V / 50 / 0.094A = 0.11128 ohms
    const float SHUNT_GAIN = 50;

    // Limits
    const float MIN_BUCK_VOLTAGE = 1.5;
    const float MAX_BUCK_VOLTAGE = 4.55;
    const float MIN_LDO_VOLTAGE = 0.35;
    const float MAX_LDO_VOLTAGE = 4.5;

    // Calibration points
    static const int NUM_POINTS = 32;
    // Each pair is defined as {measured voltage, DAC setpoint}
    std::pair<float, float> BUCK_SETPOINTS[NUM_POINTS] = {
        {4.5971, 234}, {1.5041, 2625}};
    std::pair<float, float> LDO_SETPOINTS[NUM_POINTS] = {
        {4.5176, 42}, {0.3334, 3760}};
};

#endif // CELL_H
