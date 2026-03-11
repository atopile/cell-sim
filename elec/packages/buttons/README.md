# Buttons - Momentary Switches

Simple momentary switches that can be used to bridge connections in circuits.

## Usage

```ato
#pragma experiment("BRIDGE_CONNECT")
#pragma experiment("FOR_LOOP")

import Electrical
import ElectricPower
import Resistor

from "atopile/buttons/buttons.ato" import VerticalButton
from "atopile/buttons/buttons.ato" import HorizontalButton

module Usage:
    """
    Examples showing different ways to use buttons:
    1. Simple bridging connections
    2. Pullup configuration (button pulls to ground, resistor pulls to VCC)
    3. Pulldown configuration (button pulls to VCC, resistor pulls to ground)
    """

    # Power supply for pullup/pulldown examples
    power = new ElectricPower

    # === Example 1: Simple bridging ===
    btn_bridge = new VerticalButton
    signal_a = new Electrical
    signal_b = new Electrical

    # When button is pressed, signal_a connects to signal_b
    signal_a ~> btn_bridge ~> signal_b

    # === Example 2: Pullup configuration ===
    # Button pulls signal to ground when pressed, resistor pulls to VCC when released
    btn_pullup = new HorizontalButton
    pullup_resistor = new Resistor
    pullup_signal = new Electrical

    # Configure pullup resistor
    pullup_resistor.resistance = 10kohm +/- 5%
    pullup_resistor.package = "0402"

    # Connections: signal pulled high by resistor, pulled low by button
    power.hv ~> pullup_resistor ~> pullup_signal
    pullup_signal ~> btn_pullup ~> power.lv

    # === Example 3: Pulldown configuration ===
    # Button pulls signal to VCC when pressed, resistor pulls to ground when released
    btn_pulldown = new VerticalButton
    pulldown_resistor = new Resistor
    pulldown_signal = new Electrical

    # Configure pulldown resistor
    pulldown_resistor.resistance = 10kohm +/- 5%
    pulldown_resistor.package = "0402"

    # Connections: signal pulled low by resistor, pulled high by button
    power.lv ~> pulldown_resistor ~> pulldown_signal
    pulldown_signal ~> btn_pulldown ~> power.hv

```

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
