# Texas Instruments TCA9548A I2C Multiplexer

8-channel I2C multiplexer/switch with reset functionality for expanding I2C buses.

**Features:**
- 8 bidirectional I2C channels
- Wide voltage range: 1.65V to 5.5V
- 3-bit address selection (8 possible addresses: 0x70-0x77)
- Active-low reset pin
- Built-in channel isolation
- Up to 400kHz I2C speed
- Low power consumption

## Usage

```ato
#pragma experiment("BRIDGE_CONNECT")

import ElectricPower
import I2C

from "atopile/ti-tca9548a/ti-tca9548a.ato" import TI_TCA9548A

module Usage:
    """
    Minimal usage example for `ti-tca9548a`.
    Demonstrates basic connections for the TCA9548A I2C multiplexer.
    """

    # Power supply
    power_supply = new ElectricPower
    power_supply.voltage = 3.3V +/- 5%

    # Main I2C bus
    main_i2c = new I2C
    main_i2c.frequency = 400kHz

    # I2C multiplexer
    mux = new TI_TCA9548A

    # Connections
    power_supply ~ mux.power
    main_i2c ~ mux.i2c

    # Configure address - addressor will automatically set the address lines
    # Valid addresses are 0x70 to 0x77
    mux.i2c.address = 0x70

    # Connect to I2C channels as needed
    # Each channel is isolated and can have different devices
    channel_0 = new I2C
    channel_1 = new I2C

    channel_0 ~ mux.i2cs[0]
    channel_1 ~ mux.i2cs[1]

```

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
