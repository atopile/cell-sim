# TI TPS54560x Compatibility Wrapper

## Usage

```ato
from "atopile/ti-tps54560x/ti-tps54560x.ato" import TI_TPS54560
import ElectricPower

module Usage:
    """Legacy compatibility example backed by the local TPSM84209 module."""

    power_24v = new ElectricPower
    assert power_24v.voltage within 22V to 26V

    power_5v = new ElectricPower

    regulator = new TI_TPS54560

    power_24v ~ regulator.power_in
    regulator.power_out ~ power_5v

```

## License

This package is released under the MIT License.

## Author

Created by Narayan Powderly <narayan@atopile.io>
