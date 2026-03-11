# Texas Instruments INA228 85V, 20-bit I2C Current/Power Monitor

The INA228 is a high-precision current and power monitoring IC from Texas Instruments featuring a 20-bit delta-sigma ADC. It supports both high-side and low-side current sensing with up to 85V common-mode voltage capability, making it suitable for a wide range of power monitoring applications.

## Key Features

- **20-bit ADC**: Ultra-precise current and power measurements
- **Wide common-mode voltage range**: -0.3V to +85V
- **High accuracy**: Better than 0.1% current measurement accuracy
- **I2C interface**: 2.94 MHz high-speed I2C with 16 selectable addresses
- **Integrated temperature sensor**: ±1°C accuracy for thermal monitoring
- **Configurable alerts**: Fast 75µs alert response time
- **Low power consumption**: 2.7V to 5.5V supply voltage range

## Applications

- Battery management systems
- Power supply monitoring
- Motor control systems
- Solar panel monitoring
- Industrial automation
- Automotive power monitoring

## Usage

```ato
#pragma experiment("TRAITS")
#pragma experiment("BRIDGE_CONNECT")
#pragma experiment("FOR_LOOP")
import ElectricPower
import ElectricLogic
import I2C
import Resistor
from "atopile/ti-ina228/ti-ina228.ato" import TI_INA228

module Usage:
    """
    Comprehensive usage examples for TI INA228 power monitor.

    This example demonstrates:
    - Multiple INA228 devices on a single I2C bus with different addresses
    - Monitoring various current ranges (100mA to 10A)
    - Different power rail configurations (3.3V, 5V, 12V, 48V)
    """

    # === Power rails ===
    # Supply voltage for all INA228 devices (3.3V logic)
    supply_3v3 = new ElectricPower
    supply_3v3.voltage = 3.3V +/- 5%

    # === Example 1: Low current USB device monitoring (100mA max) ===
    # USB 5V rail monitoring
    usb_5v_in = new ElectricPower
    usb_5v_out = new ElectricPower
    usb_5v_in.voltage = 5V +/- 10%

    usb_monitor = new TI_INA228
    usb_monitor.max_current = 500mA
    usb_monitor.power ~ supply_3v3
    i2c = new I2C
    usb_monitor.i2c ~ i2c
    usb_monitor.i2c.address = 0x40  # A0=GND, A1=GND

    # Insert monitor in USB power path
    usb_5v_in ~> usb_monitor ~> usb_5v_out

    # === Example 2: Medium current 3.3V rail monitoring (2A max) ===
    # Main 3.3V system rail
    main_3v3_in = new ElectricPower
    main_3v3_out = new ElectricPower
    main_3v3_in.voltage = 3.3V +/- 5%

    main_3v3_monitor = new TI_INA228
    main_3v3_monitor.max_current = 2A
    main_3v3_monitor.power ~ supply_3v3
    i2c1 = new I2C
    main_3v3_monitor.i2c ~ i2c1
    main_3v3_monitor.i2c.address = 0x41  # A0=VS, A1=GND

    # Insert monitor in 3.3V power path
    main_3v3_in ~> main_3v3_monitor ~> main_3v3_out

    # === Example 3: High current 12V rail monitoring (5A max) ===
    # 12V power supply monitoring
    supply_12v_in = new ElectricPower
    supply_12v_out = new ElectricPower
    supply_12v_in.voltage = 12V +/- 5%

    supply_12v_monitor = new TI_INA228
    supply_12v_monitor.max_current = 5A
    supply_12v_monitor.power ~ supply_3v3
    i2c2 = new I2C
    supply_12v_monitor.i2c ~ i2c2
    supply_12v_monitor.i2c.address = 0x42  # A0=SDA, A1=GND

    # Insert monitor in 12V power path
    supply_12v_in ~> supply_12v_monitor ~> supply_12v_out

    # === Example 4: Very high current motor drive monitoring (10A max) ===
    # Motor power monitoring
    motor_pwr_in = new ElectricPower
    motor_pwr_out = new ElectricPower
    motor_pwr_in.voltage = 24V +/- 10%

    motor_monitor = new TI_INA228
    motor_monitor.max_current = 10A
    motor_monitor.power ~ supply_3v3
    i2c3 = new I2C
    motor_monitor.i2c ~ i2c3
    motor_monitor.i2c.address = 0x43  # A0=SCL, A1=GND

    # Insert monitor in motor power path
    motor_pwr_in ~> motor_monitor ~> motor_pwr_out

    # === Example 5: High voltage PoE monitoring (48V, 500mA max) ===
    # Power over Ethernet monitoring
    poe_in = new ElectricPower
    poe_out = new ElectricPower
    poe_in.voltage = 48V +/- 10%

    poe_monitor = new TI_INA228
    poe_monitor.max_current = 500mA
    poe_monitor.power ~ supply_3v3
    i2c4 = new I2C
    poe_monitor.i2c ~ i2c4
    poe_monitor.i2c.address = 0x44  # A0=GND, A1=VS

    # Insert monitor in PoE power path
    poe_in ~> poe_monitor ~> poe_out

    # === Example 6: Battery discharge monitoring (3.7V Li-Ion, 3A max) ===
    # Battery monitoring with alert
    battery_in = new ElectricPower
    battery_out = new ElectricPower
    battery_in.voltage = 3.7V +/- 0.5V  # Li-Ion voltage range

    battery_monitor = new TI_INA228
    battery_monitor.max_current = 2A
    battery_monitor.power ~ supply_3v3
    i2c5 = new I2C
    battery_monitor.i2c ~ i2c5
    battery_monitor.i2c.address = 0x48  # A0=GND, A1=SDA

    # Insert monitor in battery discharge path
    battery_in ~> battery_monitor ~> battery_out

    # Connect alert output to microcontroller interrupt pin
    # Note: The INA228 module includes built-in 10k pullup on alert pin
    mcu_interrupt = new ElectricLogic
    mcu_interrupt ~ battery_monitor.alert

```

## Interface Details

### Power Supply
- **Voltage Range**: 2.7V to 5.5V
- **Current Consumption**: Low power operation
- **Decoupling**: 100nF capacitor included in design

### I2C Configuration
- **Address Range**: 0x40 to 0x4F (16 addresses)
- **Default Address**: 0x40 (A0 and A1 both connected to GND)
- **Speed**: Up to 2.94 MHz
- **Pull-up Resistors**: Required (typically 4.7kΩ)
- **Address Configuration**: A0 and A1 can each connect to GND, VS, SDA, or SCL
- **Address Formula**: `0x40 + (A1_config << 2) + (A0_config << 1)`
  - GND = 0, VS = 1, SDA = 2, SCL = 3

### Current Sensing
- **Shunt Voltage Range**: ±163.84mV or ±40.96mV
- **Common-Mode Voltage**: Up to 85V
- **Resolution**: 2.5µA to 10µA per LSB depending on range

## Pin Configuration

The INA228 uses address pins A0 and A1 to configure the I2C address:
- Connect to GND, VS, SDA, or SCL for different address combinations
- Default (both pins to GND) gives address 0x40
- Supports up to 16 devices on the same I2C bus

### Address Configuration Examples
| A1 Connection | A0 Connection | Address |
|---------------|---------------|---------|
| GND          | GND          | 0x40    |
| GND          | VS           | 0x42    |
| GND          | SDA          | 0x44    |
| GND          | SCL          | 0x46    |
| VS           | GND          | 0x48    |
| VS           | VS           | 0x4A    |
| SDA          | SDA          | 0x4C    |
| SCL          | SCL          | 0x4F    |

## Usage Pattern

The INA228 module supports the bridge pattern, allowing it to be inserted inline in power paths:

```ato
supply ~> current_monitor ~> load
```

This automatically connects the shunt resistor in series with the current path and configures the voltage monitoring.

## PCB Layout Guidelines

For optimal performance, follow these PCB layout recommendations:

1. **Shunt Resistor Placement**: Keep the shunt resistor as close as possible to the INpos/INneg pins
2. **Kelvin Connections**: Use separate traces for the current path and voltage sensing to minimize errors
3. **Noise Reduction**: Route INpos/INneg traces away from switching circuits and high-frequency signals
4. **Ground Plane**: Use a solid ground plane underneath the INA228 and shunt resistor
5. **Decoupling**: Place the decoupling capacitor within 5mm of the VS pin
6. **High-Voltage Isolation**: For voltages above 30V, ensure proper isolation between VBUS and low-voltage circuits

## Safety Considerations

⚠️ **HIGH VOLTAGE WARNING**: The VBUS pin can handle voltages up to 85V. When designing circuits with voltages above 30V:

- Follow IPC-2221 spacing requirements for high-voltage traces
- Use appropriate creepage and clearance distances
- Consider using conformal coating for additional protection
- Ensure proper isolation between high-voltage and low-voltage sections
- Test thoroughly before handling or powering the circuit

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

## License

This package is provided under the [MIT License](https://opensource.org/license/mit).
