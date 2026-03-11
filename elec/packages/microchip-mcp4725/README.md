# Microchip MCP4725 DAC

## Usage

```ato
import ElectricPower
import I2C

from "atopile/microchip-mcp4725/microchip-mcp4725.ato" import Microchip_MCP4725_driver

module Usage:
    """Minimal example for the Microchip MCP4725 DAC."""

    # DAC
    dac = new Microchip_MCP4725_driver

    # 3V3 power supply
    power = new ElectricPower
    power.voltage = 3.3V
    power ~ dac.power

    # I²C bus
    i2c = new I2C
    i2c ~ dac.i2c
```

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
