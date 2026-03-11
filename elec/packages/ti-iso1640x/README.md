# Texas Instruments ISO1640QDWRQ1 Bidirectional I2C Isolator

The ISO1640QDWRQ1 is a bidirectional I2C isolator that provides 1500V isolation between two I2C buses. It supports data rates up to 1MHz and operates with supply voltages from 2.25V to 5.5V on each side. This device is automotive qualified (AEC-Q100).

## Usage

```ato
import I2C
import ElectricPower

from "atopile/ti-iso1640x/ti-iso1640x.ato" import TI_ISO1640QDWRQ1

module Usage:
    """
    Example usage of ISO1640QDWRQ1 I2C isolator
    """

    isolator = new TI_ISO1640QDWRQ1

    power_3v3 = new ElectricPower
    power_iso_3v3 = new ElectricPower

    assert power_3v3.voltage within 3.3V +/- 5%
    assert power_iso_3v3.voltage within 3.3V +/- 5%

    i2c_mcu = new I2C
    i2c_sensor = new I2C

    assert i2c_mcu.frequency <= 400kHz
    assert i2c_sensor.frequency <= 400kHz

    # Connections
    i2c_mcu ~ isolator.i2cs[0]
    i2c_sensor ~ isolator.i2cs[1]
    power_3v3 ~ isolator.power_rails[0]
    power_iso_3v3 ~ isolator.power_rails[1]
```

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

## License

This package is provided under the [MIT License](https://opensource.org/license/mit/).
