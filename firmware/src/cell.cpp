#include "Cell.h"

// Constructor
Cell::Cell(uint8_t mux_channel) : mux_channel(mux_channel) {}

// Implementation of public methods
void Cell::init()
{
    // Set the mux channel
    setMuxChannel();
    // Initialize DACs
    ldo_dac.begin(LDO_ADDRESS);
    buck_dac.begin(BUCK_ADDRESS);

    // Initialize ADC
    adc.begin(ADC_ADDRESS);

    // Configure GPIO expander
    Wire.beginTransmission(0x20);
    Wire.write(0x03); // Configuration register
    Wire.write(0x00); // Set all pins as output
    Wire.endTransmission();

    // Set the outputs to 0
    Wire.beginTransmission(0x20);
    Wire.write(0x01); // Configuration register
    Wire.write(0x00); // Set all pins as output
    Wire.endTransmission();
}

void Cell::disable()
{
    GPIO_STATE &= ~(1 << 2);
    GPIO_STATE &= ~(1 << 3);
    setGPIOState();
}

void Cell::enable()
{
    GPIO_STATE |= (1 << 2);
    GPIO_STATE |= (1 << 3);
    setGPIOState();
}

float Cell::getVoltage()
{
    int16_t adc_value = adc.readADC_SingleEnded(2);
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
    if (buck_voltage < MIN_BUCK_VOLTAGE) buck_voltage = MIN_BUCK_VOLTAGE;
    if (buck_voltage > MAX_BUCK_VOLTAGE) buck_voltage = MAX_BUCK_VOLTAGE;
    if (ldo_voltage < MIN_LDO_VOLTAGE) ldo_voltage = MIN_LDO_VOLTAGE;
    if (ldo_voltage > MAX_LDO_VOLTAGE) ldo_voltage = MAX_LDO_VOLTAGE;

    // Set the output voltage
    setBuckVoltage(buck_voltage);
    setLDOVoltage(ldo_voltage);
}

void Cell::turnOnDMMRelay()
{
    GPIO_STATE |= (1 << 6);
    setGPIOState();
}

void Cell::turnOffDMMRelay()
{
    GPIO_STATE &= ~(1 << 6);
    setGPIOState();
}

void Cell::turnOnOutputRelay()
{
    GPIO_STATE |= (1 << 7);
    setGPIOState();
}

void Cell::turnOffOutputRelay()
{
    GPIO_STATE &= ~(1 << 7);
    setGPIOState();
}

float Cell::getCurrent()
{
    return readShuntCurrent();
}

// Implementation of private methods

float Cell::getLDOVoltage()
{
    setMuxChannel();
    int16_t adc_value = adc.readADC_SingleEnded(1);
    float volts = adc.computeVolts(adc_value);
    return volts;
}

uint16_t Cell::calculateSetpoint(float voltage, bool useBuckCalibration)
{
    // Select calibration points based on parameter
    const std::pair<float, float>* calibration_points = useBuckCalibration ? BUCK_SETPOINTS : LDO_SETPOINTS;

    // Calculate slope using the two points
    float m = (calibration_points[1].first - calibration_points[0].first) / 
              (calibration_points[1].second - calibration_points[0].second);

    // Calculate intercept
    float b = calibration_points[0].first - m * calibration_points[0].second;

    // Calculate and return the setpoint for the desired voltage
    return static_cast<uint16_t>(m * voltage + b);
}

float Cell::getBuckVoltage()
{
    setMuxChannel();
    int16_t adc_value = adc.readADC_SingleEnded(0);
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
    Wire.beginTransmission(TCA6408_ADDR);
    Wire.write(0x01); // Output register
    Wire.write(GPIO_STATE);
    Wire.endTransmission();
}

float Cell::readShuntCurrent()
{
    setMuxChannel();
    int16_t adc_value = adc.readADC_SingleEnded(3);
    float volts = adc.computeVolts(adc_value);

    return volts / SHUNT_RESISTOR_OHMS / SHUNT_GAIN;
}

void Cell::setMuxChannel()
{
    Wire.beginTransmission(MUX_ADDRESS);
    Wire.write(1 << mux_channel);
    Wire.endTransmission();
}
