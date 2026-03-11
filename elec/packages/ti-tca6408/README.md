# Texas Instruments TCA6408 – 8-Bit I²C GPIO Expander

Atopile driver for the **Texas Instruments TCA6408** low-voltage 8-bit I²C GPIO expander (LCSC part **C181499**). The device provides 8 bidirectional GPIO ports with interrupt output capability and operates from 1.65V to 5.5V. It features configurable interrupt sources, polarity inversion on all pins, and two selectable I²C addresses (0x20 or 0x21).

## Usage

```ato
#pragma experiment("BRIDGE_CONNECT")
#pragma experiment("FOR_LOOP")

# --- Standard library imports ---
import ElectricPower
import I2C
import Resistor

# --- Package import ---
from "atopile/ti-tca6408/ti-tca6408.ato" import TI_TCA6408


module Usage:
    """
    Minimal usage example for TI TCA6408 8-bit I²C GPIO expander.

    Shows how to connect two TCA6408 devices to the same I²C bus
    with different addresses (0x20 and 0x21).
    """

    # Power rail (3.3V)
    power_3v3 = new ElectricPower
    power_3v3.voltage = 3.3V

    # I²C bus
    i2c_bus = new I2C

    # GPIO expander instances
    expander = new TI_TCA6408

    # Set addresses
    expander.i2c.address = 0x20

    # Connect power and I²C
    power_3v3 ~ expander.power
    expander.i2c ~ i2c_bus

    # Provide logic references
    power_3v3 ~ i2c_bus.scl.reference
    power_3v3 ~ i2c_bus.sda.reference

```

## Contributing

Contributions are welcome! Please open an issue or pull request and ensure the `usage` build target passes (`ato build usage`).

## License

This package is provided under the [MIT License](https://opensource.org/license/mit/).
