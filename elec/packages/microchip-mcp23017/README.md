# Microchip MCP23017 – 16-bit I²C GPIO Expander

A declarative Atopile driver for the **Microchip MCP23017** 16-bit general-purpose I/O expander (QFN-28, LCSC part **C629439**).  The device provides two 8-bit GPIO banks over an I²C interface and operates from **1.8 V to 5.5 V**.

## Usage

```ato
#pragma experiment("MODULE_TEMPLATING")
#pragma experiment("BRIDGE_CONNECT")
#pragma experiment("FOR_LOOP")

# --- Standard library imports ---
import ElectricPower
import I2C

# --- Package import ---
from "atopile/microchip-mcp23017/microchip-mcp23017.ato" import Microchip_MCP23017


module Usage:
    """
    Minimal usage example for `microchip-mcp23017`.
    Powers the MCP23017 from a shared 3 V 3 rail and places it on an I²C bus
    running at the default 7-bit address (0x20).
    """

    # Power rail (3.3 V)
    power_3v3 = new ElectricPower
    power_3v3.voltage = 3.3V

    # I²C bus
    i2c_bus = new I2C

    # Expander instance
    expander = new Microchip_MCP23017

    # Connect required interfaces
    power_3v3 ~ expander.power
    power_3v3 ~ i2c_bus.scl.reference
    power_3v3 ~ i2c_bus.sda.reference

    i2c_bus ~ expander.i2c

    # (Optional) Explicitly set the expander address – defaults to 0x20
    expander.i2c.address = 0x20

```

## Contributing

Contributions are welcome! Feel free to open an issue or pull request—please ensure the `usage` build target passes (`ato build usage`).

## License

This package is provided under the [MIT License](https://opensource.org/license/mit/).
