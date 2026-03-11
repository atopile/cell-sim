# STMicroelectronics LDK220M-R Adjustable Low-Dropout Regulator

## Overview

The LDK220M-R is a 200mA adjustable low-dropout voltage regulator with low quiescent current and low noise characteristics. This package provides an atopile driver for easy integration into your projects.

## Features

- **Output Current**: Up to 200mA
- **Input Voltage Range**: 2.5V to 13.2V
- **Output Voltage Range**: 1.2V to 12V
- **Reference Voltage**: 1.185V ±2%
- **Dropout Voltage**: 100mV to 350mV (typical)
- **Package**: SOT-23-5L
- **Built-in decoupling capacitors**: 1µF on both input and output

## Usage

```ato
#pragma experiment("TRAITS")
#pragma experiment("BRIDGE_CONNECT")
#pragma experiment("TRAITS")

import ElectricPower
import has_part_removed

from "atopile/st-ldk220/st-ldk220.ato" import LDK220M_R

module MCU:
    """Host MCU providing power rail."""

    trait has_part_removed

    power = new ElectricPower
    assert power.voltage is 3.3V +/- 5%

    trait has_part_removed


module Usage:
    """Minimal example for the LDK220M-R LDO."""

    some_input_power = new ElectricPower
    assert some_input_power.voltage is 5V +/- 5%

    # MCU & sensor
    mcu = new MCU
    ldo = new LDK220M_R

    # Shared 3V3 rail
    some_input_power ~> ldo ~> mcu.power
```

## Output Voltage Configuration

The LDK220M_R module automatically configures the feedback resistor divider based on the output voltage assertion. Simply assert the desired output voltage and the module will calculate the appropriate resistor values.

```ato
# Example: Configure for 1.8V output
ldo = new LDK220M_R
assert ldo.power_out.voltage is 1.8V +/- 5%
```

## Pin Configuration

The module handles all pin connections internally:

- **IN**: Input voltage
- **GND**: Ground reference
- **EN**: Enable (connected to input voltage for always-on operation)
- **OUT**: Regulated output voltage
- **ADJ**: Feedback pin (connected to voltage divider)

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
