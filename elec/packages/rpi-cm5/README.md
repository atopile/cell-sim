# Raspberry Pi Compute Module 5 (CM5)

## Usage

```ato
from "atopile/rpi-cm5/rpi-cm5.ato" import RPI_CM5

import ElectricPower

module Usage:
    """Usage example for the Raspberry Pi CM5 module"""

    cm5 = new RPI_CM5

    power_5v = new ElectricPower
    power_5v ~ cm5.power_5v

```

## Overview

This package provides the necessary components and interfaces to integrate the Raspberry Pi Compute Module 5 into your hardware designs using atopile.

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This `rpi-cm5` atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
