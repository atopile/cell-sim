# Microchip EMC2101 Temperature Sensor and Fan Controller

A comprehensive driver for the Microchip EMC2101 temperature monitoring and PWM fan control IC. This package provides a complete atopile implementation for thermal management applications.

## Features

- **Internal Temperature Sensor**: ±2°C accuracy for local temperature monitoring
- **External Temperature Sensing**: Remote diode connection via DP/DN pins for CPU/GPU temperature monitoring
- **PWM Fan Control**: Precise fan speed control via dedicated FAN output pin
- **Tachometer/Alert**: Combined nALERT_TACH pin for thermal alerts and fan speed feedback
- **I2C/SMBus Interface**: Standard 3.3V communication interface (SMCLK/SMDATA)
- **Package**: MSOP-8 surface mount package
- **JLCPCB Part**: C626968 (EMC2101-R-ACZL-TR)

## Pin Configuration (MSOP-8)

1. **VDD** - Power supply (3.0V to 3.6V)
2. **DP** - External diode positive terminal
3. **DN** - External diode negative terminal
4. **FAN** - PWM fan drive output
5. **GND** - Ground
6. **nALERT_TACH** - Alert output / Tachometer input (shared pin)
7. **SMDATA** - I2C/SMBus Data
8. **SMCLK** - I2C/SMBus Clock

## Usage

```ato
#pragma experiment("TRAITS")
#pragma experiment("FOR_LOOP")
#pragma experiment("BRIDGE_CONNECT")

import ElectricLogic
import ElectricPower
import I2C
from "atopile/microchip-emc2101/microchip-emc2101.ato" import Microchip_EMC2101

module Usage:
    """
    Minimal usage example for microchip-emc2101.
    Demonstrates how to use the EMC2101 temperature sensor and fan controller
    in a typical thermal management application with MSOP-8 package.
    """

    # Power supply
    power_3v3 = new ElectricPower
    assert power_3v3.voltage within 3.2V to 3.4V

    # I2C bus for communication
    i2c_bus = new I2C
    assert i2c_bus.frequency within 100kHz to 400kHz

    # Fan control signal
    fan_pwm_signal = new ElectricLogic

    # EMC2101 temperature sensor and fan controller
    temp_controller = new Microchip_EMC2101

    # Connect power
    power_3v3 ~ temp_controller.power

    # Connect I2C bus
    i2c_bus ~ temp_controller.i2c

    # Connect fan PWM output
    fan_pwm_signal ~ temp_controller.pwm_out

```

### Optional Interfaces

The EMC2101 driver also provides optional interfaces for advanced features:

- **`alert_tach`**: Combined alert output and tachometer input (nALERT_TACH pin)
- **`temp_diode_p`** and **`temp_diode_n`**: External temperature diode connections for remote sensing

These interfaces can be connected when needed for more advanced thermal management applications.

## Key Interfaces

### Power Supply
- **Voltage**: 3.0V to 3.6V (3.3V typical)
- **Current**: Low power operation
- **Decoupling**: 100nF capacitor included

### I2C Communication
- **Default Address**: 0x4C (7-bit)
- **Speed**: Up to 400kHz (Fast-mode)
- **Pull-up Resistors**: 10kΩ included (optional - may be external)

### Temperature Sensing
- **Internal**: Built-in temperature sensor
- **External**: Remote diode sensing via DP/DN pins
- **Accuracy**: ±2°C typical

### Fan Control
- **Output**: PWM signal on FAN pin
- **Feedback**: Tachometer input on nALERT_TACH pin (shared with alert)
- **Alert**: Thermal limit violations on nALERT_TACH pin

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

## License

This package is provided under the [MIT License](https://opensource.org/license/mit).
