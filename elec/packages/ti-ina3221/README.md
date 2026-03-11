# Texas Instruments INA3221 Triple-Channel Current & Power Monitor

The INA3221 is a triple-channel, high-side current and power monitor with an I2C and SMBus-compatible interface from Texas Instruments. This device is ideal for monitoring multiple power rails in battery-powered systems, servers, and other applications requiring precise current measurement.

## Features

- **Triple-channel high-side current sensing**
- **Wide supply voltage range**: 2.7V to 5.5V (IC supply)
- **Bus voltage monitoring**: 0V to 26V per channel
- **High accuracy**: ±0.1% gain error (typical)
- **I2C interface** with configurable address (0x40-0x43)
- **Programmable conversion times** and averaging modes
- **Alert functionality** with warning and critical thresholds
- **Low power consumption**: 350µA typical supply current
- **Operating temperature**: -40°C to +125°C
- **16-bit ADC** with programmable conversion time
- **Compact package**: QFN-16 (4mm × 4mm)

## Usage

```ato
#pragma experiment("TRAITS")
#pragma experiment("BRIDGE_CONNECT")

import ElectricPower
import I2C
import has_part_removed

from "atopile/ti-ina3221/ti-ina3221.ato" import TI_INA3221

module Sink:
    """
    mock sink device for testing
    """
    power = new ElectricPower
    trait has_part_removed

module Usage:
    """
    Minimal usage example for TI INA3221 triple-channel current and power monitor.
    Shows how to connect power supply, I2C bus, and three monitored power rails.
    """

    # Create current monitor instance
    current_monitor = new TI_INA3221
    sinks = new Sink[3]

    # Main power supply for the IC (3.3V or 5V)
    power_3v3 = new ElectricPower
    power_3v3.voltage = 3.3V +/- 5%

    # External I2C bus
    i2c_bus = new I2C

    # Example monitored power rails
    power_5v = new ElectricPower
    power_5v.voltage = 5V +/- 5%

    # Connect interfaces
    power_3v3 ~ current_monitor.power
    i2c_bus ~ current_monitor.i2c

    # Connect monitored channels (bridgeable)
    power_5v ~> current_monitor.channels[0] ~> sinks[0].power
    power_5v ~> current_monitor.channels[1] ~> sinks[1].power
    power_5v ~> current_monitor.channels[2] ~> sinks[2].power

    # Set I2C address (default 0x40 with A0 pin low)
    current_monitor.i2c.address = 0x40

```

## Technical Specifications

- **Supply Voltage (VS)**: 2.7V to 5.5V
- **Bus Voltage Range**: 0V to 26V per channel
- **Common-Mode Voltage**: 0V to 26V
- **I2C Address**: 0x40 (default), 0x41, 0x42, 0x43 (configurable via A0 pin)
- **Supply Current**: 350µA (typical)
- **Shunt Voltage Range**: ±163.8mV
- **ADC Resolution**: 16-bit (13-bit effective)
- **Conversion Time**: 140µs to 8.244ms (programmable)
- **I2C Speed**: Up to 2.94 MHz
- **Package**: QFN-16 (4mm × 4mm)

## I2C Address Configuration

The INA3221 supports four different I2C addresses controlled by the A0 pin:

| A0 Connection | I2C Address | Description |
|---------------|-------------|-------------|
| GND           | 0x40        | Default configuration |
| VS            | 0x41        | Connected to supply voltage |
| SDA           | 0x42        | Connected to I2C data line |
| SCL           | 0x43        | Connected to I2C clock line |

## Pin Configuration

| Pin | Name      | Description |
|-----|-----------|-------------|
| 1   | IN_3      | Channel 3 Input (-) |
| 2   | INplus3   | Channel 3 Input (+) |
| 3   | GND       | Ground |
| 4   | VS        | Supply Voltage |
| 5   | A0        | Address Select Pin |
| 6   | SCL       | I2C Serial Clock |
| 7   | SDA       | I2C Serial Data |
| 8   | WARNING   | Warning Alert Output |
| 9   | CRITICAL  | Critical Alert Output |
| 10  | PV        | Pullup Voltage |
| 11  | IN_1      | Channel 1 Input (-) |
| 12  | INplus1   | Channel 1 Input (+) |
| 13  | TC        | Timing Control |
| 14  | IN_2      | Channel 2 Input (-) |
| 15  | INplus2   | Channel 2 Input (+) |
| 16  | VPU       | Internal Pullup |
| 17  | PAD       | Exposed Pad (Ground) |

## Current Sensing Configuration

The INA3221 includes three `Channel` modules, each with integrated shunt resistors:

### Channel Module Features
- **Bridgeable design**: Use `~>` to connect power rails through the current sensor
- **Integrated shunt resistor**: Default 100mΩ ±1% (configurable)
- **Power flow**: `power_in ~> shunt ~> power_out`
- **Differential sensing**: Connected to INA3221 via `sense.p` and `sense.n`
- **Bus voltage monitoring**: 0V to 26V per channel

### Shunt Resistor Selection
- **Shunt voltage range**: ±163.8mV
- **Typical shunt values**: 0.1Ω, 0.01Ω, 0.001Ω
- **Current calculation**: I = V_shunt / R_shunt
- **Power calculation**: P = V_bus × I

### Example Usage Patterns
```ato
# Direct monitoring (channel as bridge)
power_source ~> current_monitor.channels[0] ~> load

# Monitoring without load connection
power_rail ~ current_monitor.channels[1].power_in
# Load connects to channels[1].power_out separately
```

## Applications

- Battery management systems
- Server and datacenter power monitoring
- Industrial automation
- Automotive power management
- Solar power systems
- Multi-rail power supplies
- IoT device power profiling
- Power consumption analysis

## Alert Functions

- **WARNING**: Configurable threshold for bus voltage, shunt voltage, or power
- **CRITICAL**: Configurable threshold for bus voltage, shunt voltage, or power
- **Timing Control**: External conversion triggering
- **Conversion-ready flag**: Available via I2C status register

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

## License

This package is provided under the [MIT License](https://opensource.org/license/mit).
