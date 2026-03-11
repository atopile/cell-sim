# TI INA185A1 Current Sense Amplifier

Atopile driver for the TI INA185A1 zero-drift, bidirectional current sense amplifier.

## Usage

```ato
#pragma experiment("TRAITS")

# --- Standard library imports ---
import ElectricPower
import ElectricSignal
import Resistor

# --- Package import ---
from "atopile/ti-ina185a1/ti-ina185a1.ato" import TI_INA185A1

module Usage:
    """
    Minimal usage example for the TI INA185A1 current sense amplifier.
    """
    amplifier = new TI_INA185A1
    power_to_load = new ElectricPower

    # fake analog signal output
    amplified_signal = new ElectricSignal

    # System rails
    system_3v3 = new ElectricPower
    system_3v3.voltage = 3.3V +/- 5%

    load_power = new ElectricPower
    load_power.voltage = 12V +/- 5%

    # Shunt resistor for current sensing
    shunt = new Resistor
    shunt.resistance = 10mohm +/- 1%
    shunt.package = "1206"

    # Power the amplifier
    amplifier.power ~ system_3v3

    # connect the output reference to ground for 0 offset
    amplifier.ref.line ~ system_3v3.lv

    # connect amplifier output
    amplified_signal ~ amplifier.output

    # connect the load and shunt and grounds
    load_power.hv ~> shunt ~> power_to_load.hv
    load_power.lv ~ power_to_load.lv
    load_power.lv ~ system_3v3.lv
    amplifier.input.p.line ~> shunt ~> amplifier.input.n.line

```

## Contributing

Contributions are welcome! Please open an issue or pull request and ensure the `usage` build target passes (`ato build usage`).

## License

This package is provided under the [MIT License](https://opensource.org/license/mit/).
