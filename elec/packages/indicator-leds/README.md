# Indicator LEDs (0603) – Red/Green/Blue/Yellow/Yellow-Green/White

Convenience modules for single indicator LEDs with a correctly sized series resistor and bridgeable interface.

## Usage

```ato
#pragma experiment("BRIDGE_CONNECT")

import ElectricPower

from "atopile/indicator-leds/indicator-leds.ato" import LEDIndicatorRed
from "atopile/indicator-leds/indicator-leds.ato" import LEDIndicatorGreen

module Usage:
    """
    Minimal usage example for `indicator-leds`.
    Demonstrates direct power connection and bridge usage.
    """

    red_led = new LEDIndicatorRed
    green_led = new LEDIndicatorGreen

    # Direct power connection
    power = new ElectricPower
    power.voltage = 5V
    power ~ red_led.power
    power ~ green_led.power

```

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

## License

This package is provided under the [MIT License](https://opensource.org/license/mit/).
