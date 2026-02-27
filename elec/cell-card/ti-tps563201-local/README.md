# Texas Instruments TPS563201 3A Buck Converter

The TPS563201 is a synchronous buck converter with integrated MOSFETs capable of delivering up to 3A of continuous output current.

## Specifications

- **Input Voltage**: 4.5V to 17V
- **Output Voltage**: 0.76V to 7V (adjustable)
- **Output Current**: Up to 3A continuous
- **Switching Frequency**: Fixed 580kHz
- **Package**: SOT-23-6
- **Features**:
  - Integrated MOSFETs
  - Current mode control
  - Internal compensation
  - Power good output
  - Enable pin

## Usage

```ato
#pragma experiment("FOR_LOOP")
#pragma experiment("BRIDGE_CONNECT")

import ElectricPower
from "atopile/ti-tps563201/ti-tps563201.ato" import TI_TPS563201

module Usage:
    """
    Example usage of TPS563201 Buck Regulator
    Converting 5V input to 3.3V output
    """

    # Define power rails
    power_5v = new ElectricPower
    power_3v3 = new ElectricPower

    # Configure input voltage
    assert power_5v.voltage is 5V +/- 5%

    # Create the buck regulator
    buck = new TI_TPS563201

    # Configure output voltage
    assert buck.output_voltage within 3.3V +/- 4%

    # Connect power
    power_5v ~ buck.power_in
    buck.power_out ~ power_3v3

    # Enable control (optional - defaults to always enabled)
    # If you want external control, connect to an ElectricLogic signal:
    # enable = new ElectricLogic
    # enable ~ buck.enable

```

## Example Configurations

### 1.8V Output
```ato
regulator = new TPS563201
assert regulator.input_voltage is 5V +/- 10%
assert regulator.output_voltage is 1.8V +/- 3%
```

### 5V Output from 12V Input
```ato
regulator = new TPS563201
assert regulator.input_voltage is 12V +/- 10%
assert regulator.output_voltage is 5V +/- 3%
```

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
