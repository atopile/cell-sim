# Texas Instruments TMP117 High-Precision Temperature Sensor

The TMP117 is a high-accuracy, low-power, digital temperature sensor with exceptional precision and stability. It provides ±0.1°C accuracy across a wide temperature range and is designed to meet medical industry standards.

## Features

- **Ultra-High Accuracy**: ±0.1°C from -20°C to 50°C, ±0.2°C from -55°C to 150°C
- **High Resolution**: 16-bit temperature result with 0.0078°C (7.8125 m°C) resolution
- **Low Power**: 3.5µA typical active current, 0.15µA shutdown current
- **Wide Supply Range**: 1.8V to 5.5V operation
- **I²C Interface**: SMBus-compatible with programmable alert functionality
- **Multiple Addresses**: 4 selectable I²C addresses (0x48-0x4B)
- **Medical Standards**: Meets ASTM E1112 and ISO 80601 requirements
- **EEPROM**: Integrated for device programming with 48-bit general use memory
- **Small Package**: 2×2 mm WDFN-6 with exposed thermal pad
- **Pin Compatible**: Drop-in replacement for TMP116

## Usage

```ato
#pragma experiment("TRAITS")
#pragma experiment("BRIDGE_CONNECT")

import I2C
import ElectricPower
import Resistor
import ElectricLogic

from "atopile/ti-tmp117/ti-tmp117.ato" import TI_TMP117

module Usage:
    """
    Minimal usage example for TI TMP117 high-precision temperature sensor.
    Shows basic I2C configuration with power supply and pull-up resistors.
    """

    temp_sensor = new TI_TMP117

    # Sensor power supply (3.3V)
    power_3v3 = new ElectricPower
    power_3v3 ~ temp_sensor.power
    assert power_3v3.voltage within 3.2V to 3.4V

    # I2C bus
    i2c_bus = new I2C
    i2c_bus ~ temp_sensor.i2c

    # Configure address (Possible addresses: 0x48, 0x49, 0x4A, 0x4B)
    assert temp_sensor.i2c.address is 0x48

    # Alert pin is available for temperature limit notifications
    # Can be connected to microcontroller GPIO for interrupt-driven operation
    alert_pin = new ElectricLogic
    alert_pin ~ temp_sensor.alert

```

## Hardware Features

### Power Supply
- **Operating Voltage**: 1.8V to 5.5V
- **Current Consumption**: 3.5µA active, 0.15µA shutdown
- Integrated decoupling capacitor (100nF)

### Temperature Measurement
- **Accuracy**: ±0.1°C (typical) from -20°C to 50°C
- **Resolution**: 16-bit (0.0078°C per LSB)
- **Range**: -55°C to 150°C operating
- **Response Time**: 125ms typical thermal time constant

### I²C Interface
- **Address Range**: 0x48 to 0x4B (4 addresses)
- **Address Selection**: ADD0 pin controls address
  - ADD0 = GND: 0x48
  - ADD0 = V+: 0x49
  - ADD0 = SDA: 0x4A
  - ADD0 = SCL: 0x4B
- **Default Address**: 0x48 (ADD0 tied to GND)
- **Clock Speed**: Up to 400 kHz (Fast mode)

### Alert Function
- **Programmable Limits**: High and low temperature thresholds
- **Alert Modes**: Interrupt mode, thermostat mode, and data ready alert
- **Output Type**: Open-drain, active low
- **Hysteresis**: Programmable to prevent oscillation

### Package Information
- **Package**: WDFN-6 (2×2 mm)
- **Thermal Pad**: Exposed pad for enhanced thermal performance
- **Pin Count**: 6 pins plus thermal pad
- **Height**: 0.8mm maximum

## Applications

### Medical Devices
- Electronic patient thermometers
- Wearable health monitors
- Medical equipment thermal management
- Laboratory instruments

### Industrial Applications
- HVAC temperature control
- Industrial process monitoring
- Data logger applications
- Thermal management systems

### Consumer Electronics
- Smart home thermostats
- Appliance temperature monitoring
- Portable devices
- Battery thermal protection

## Technical Specifications

- **Resolution**: 16-bit (65,536 codes)
- **Conversion Time**: 15.5ms (default), configurable
- **Supply Current**: 3.5µA active, 0.15µA shutdown
- **Temperature Coefficient**: 0.1 ppm/°C typical
- **Self-Heating**: <0.01°C in still air
- **ESD Protection**: 2kV HBM, 200V CDM

## Standards Compliance

- **Medical**: ASTM E1112, ISO 80601
- **Industrial**: RoHS compliant
- **Automotive**: AEC-Q100 qualified versions available

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

## License

This package is provided under the [MIT License](https://opensource.org/license/mit).
