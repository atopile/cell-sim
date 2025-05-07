#include "Cell.h"

// Constructor
Cell::Cell(uint8_t mux_channel, TwoWire &wire_interface) : mux_channel(mux_channel), wire(wire_interface)
{
    // ...
}

// Implementation of public methods
void Cell::init()
{
    // Set the mux channel
    setMuxChannel();

    // Initialize DACs
    ldo_dac.begin(LDO_ADDRESS, &wire);
    buck_dac.begin(BUCK_ADDRESS, &wire);

    // Initialize ADC
    adc.begin(ADC_ADDRESS, &wire);

    // Configure GPIO expander
    wire.beginTransmission(0x20);
    wire.write(0x03); // Configuration register
    wire.write(0x00); // Set all pins as output
    wire.endTransmission();

    // Set the outputs to 0
    wire.beginTransmission(0x20);
    wire.write(0x01); // Configuration register
    wire.write(0x00); // Set all pins as output
    wire.endTransmission();
}

void Cell::disable()
{
    setMuxChannel();
    GPIO_STATE &= ~(1 << gpio_buck_enable);
    GPIO_STATE &= ~(1 << gpio_ldo_enable);
    setGPIOState();
}

void Cell::enable()
{
    setMuxChannel();
    GPIO_STATE |= (1 << gpio_buck_enable);
    GPIO_STATE |= (1 << gpio_ldo_enable);
    setGPIOState();
}

float Cell::getVoltage()
{
    setMuxChannel();
    int16_t adc_value = adc.readADC_SingleEnded(adc_output_voltage);
    float volts = adc.computeVolts(adc_value);
    return volts;
}

void Cell::setVoltage(float voltage)
{
    // Set the mux channel
    setMuxChannel();

    float buck_voltage = voltage * 1.05;
    float ldo_voltage = voltage;

    // Buck voltage limits
    if (buck_voltage < MIN_BUCK_VOLTAGE)
        buck_voltage = MIN_BUCK_VOLTAGE;
    if (buck_voltage > MAX_BUCK_VOLTAGE)
        buck_voltage = MAX_BUCK_VOLTAGE;
    if (ldo_voltage < MIN_LDO_VOLTAGE)
        ldo_voltage = MIN_LDO_VOLTAGE;
    if (ldo_voltage > MAX_LDO_VOLTAGE)
        ldo_voltage = MAX_LDO_VOLTAGE;

    // Set the output voltage
    setBuckVoltage(buck_voltage);
    setLDOVoltage(ldo_voltage);
}

void Cell::turnOnOutputRelay()
{
    setMuxChannel();
    GPIO_STATE |= (1 << gpio_output_relay_control);
    setGPIOState();
}

void Cell::turnOffOutputRelay()
{
    setMuxChannel();
    GPIO_STATE &= ~(1 << gpio_output_relay_control);
    setGPIOState();
}

void Cell::turnOnLoadSwitch()
{
    setMuxChannel();
    GPIO_STATE |= (1 << gpio_load_switch_control);
    setGPIOState();
}

void Cell::turnOffLoadSwitch()
{
    setMuxChannel();
    GPIO_STATE &= ~(1 << gpio_load_switch_control);
    setGPIOState();
}

float Cell::getCurrent()
{
    setMuxChannel();
    return readShuntCurrent();
}

// Implementation of private methods

float Cell::getLDOVoltage()
{
    setMuxChannel();
    int16_t adc_value = adc.readADC_SingleEnded(adc_ldo_voltage);
    float volts = adc.computeVolts(adc_value);
    return volts;
}

float Cell::getBuckVoltage()
{
    setMuxChannel();
    int16_t adc_value = adc.readADC_SingleEnded(adc_buck_voltage);
    float volts = adc.computeVolts(adc_value);
    return volts;
}

void Cell::setBuckVoltage(float voltage)
{
    setMuxChannel();
    uint16_t setpoint = calculateSetpoint(voltage, true);

    buck_dac.setVoltage(setpoint, false);
}

void Cell::setLDOVoltage(float voltage)
{
    setMuxChannel();
    uint16_t setpoint = calculateSetpoint(voltage, false);
    ldo_dac.setVoltage(setpoint, false);
}
// Implementation of helper methods

void Cell::setGPIOState()
{
    setMuxChannel();
    // Set output relay on P7
    wire.beginTransmission(TCA6408_ADDR);
    wire.write(0x01); // Output register
    wire.write(GPIO_STATE);
    wire.endTransmission();
}

float Cell::readShuntCurrent()
{
    setMuxChannel();
    int16_t adc_value = adc.readADC_SingleEnded(adc_output_current);
    float volts = adc.computeVolts(adc_value);

    return volts / SHUNT_RESISTOR_OHMS / SHUNT_GAIN;
}

void Cell::setMuxChannel()
{
    wire.beginTransmission(MUX_ADDRESS);
    wire.write(1 << mux_channel);
    wire.endTransmission();
}

void Cell::calibrate()
{
    // Calibrate the buck between 234 and 2625
    float delta = 2625 - 234;
    int step = round(delta / NUM_POINTS);
    for (int i = 0; i < NUM_POINTS; i++)
    {
        enable();
        turnOnOutputRelay();
        float setpoint = 234 + i * step;
        buck_dac.setVoltage(setpoint, false);
        delay(100);
        float voltage = getBuckVoltage();
        BUCK_SETPOINTS[i] = {voltage, setpoint};
    }

    // Set buck output to max
    buck_dac.setVoltage(234, false);
    delay(50);
    // Calibrate the ldo between 42 and 3760
    delta = 3760 - 42;
    step = round(delta / NUM_POINTS);
    for (int i = 0; i < NUM_POINTS; i++)
    {
        enable();
        turnOnOutputRelay();
        float setpoint = 42 + i * step;
        ldo_dac.setVoltage(setpoint, false);
        delay(100);
        float voltage = getVoltage();
        LDO_SETPOINTS[i] = {voltage, setpoint};
    }
}

uint16_t Cell::calculateSetpoint(float voltage, bool useBuckCalibration)
{
    // Use the appropriate calibration array; each element is {measured voltage, DAC setpoint}
    const std::pair<float, float> *calibration_points = useBuckCalibration ? BUCK_SETPOINTS : LDO_SETPOINTS;
    const int numPoints = NUM_POINTS;
    // For descending data: clamp if voltage is above the maximum or below the minimum.
    if (voltage >= calibration_points[0].first)
    {
        return calibration_points[0].second;
    }
    if (voltage <= calibration_points[numPoints - 1].first)
    {
        return calibration_points[numPoints - 1].second;
    }
    // Find the first index where the measured voltage becomes less than or equal to the target.
    int index = 1;
    while (index < numPoints && calibration_points[index].first > voltage)
    {
        index++;
    }
    int above_index = index - 1;
    int below_index = index;
    if (above_index == below_index)
    {
        return calibration_points[above_index].second;
    }
    // Linear interpolation:
    // DAC setpoint = s1 + (voltage - v1) * (s2 - s1) / (v2 - v1)
    float v1 = calibration_points[above_index].first;
    float v2 = calibration_points[below_index].first;
    float s1 = calibration_points[above_index].second;
    float s2 = calibration_points[below_index].second;
    float m = (s2 - s1) / (v2 - v1);
    float b = s1 - m * v1;
    return static_cast<uint16_t>(m * voltage + b);
}