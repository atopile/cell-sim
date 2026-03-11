# TLV75901 LDO

TLV75901 LDO Regulator with adjustable output voltage

## Usage

```ato
#pragma experiment("BRIDGE_CONNECT")
import ElectricPower
from "atopile/ti-tlv75901/ti-tlv75901.ato" import TI_TLV75901

module Usage:
    # Create LDO
    ldo = new TI_TLV75901

    # Create example power interfaces
    power_in = new ElectricPower
    power_out = new ElectricPower

    # Configure voltages
    power_in.voltage = 5V +/- 1%
    power_out.voltage = 3.3V +/- 3%

    # Connect to regulator (bridge connect)
    power_in ~> ldo ~> power_out

    # Connect to regulator (Interfaces)
    power_in ~ ldo.power_in
    power_out ~ ldo.power_out

```

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
