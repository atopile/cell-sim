# ST STM32H723 Cortex-M7 MCU (RMII, USB, I2C, SPI, UART)

High-performance Arm Cortex-M7 microcontroller up to 550 MHz with up to 1 MB Flash, 564 KB RAM, USB FS, multiple I2C/SPI/UART, and built-in Ethernet MAC. Includes RMII interface to connect an external PHY (e.g. Microchip LAN8742A).

- Datasheet: https://jlcpcb.com/api/file/downloadByFileSystemAccessId/8588886273382109184

## Usage

```ato
#pragma experiment("FOR_LOOP")
#pragma experiment("BRIDGE_CONNECT")

import ElectricPower
import ElectricLogic

from "atopile/st-stm32h723/st-stm32h723.ato" import ST_STM32H723
# from "atopile/microchip-lan8742a/microchip-lan8742a.ato" import Microchip_LAN8742A

module Usage:
    """
    Minimal usage for STM32H723 with power, SWD, USB FS, I2C and RMII to LAN8742A.
    """
    power_3v3 = new ElectricPower
    mcu = new ST_STM32H723
    reset = new ElectricLogic

    # Power
    power_3v3 ~ mcu.power_3v3
```

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

## License

This package is provided under the MIT License.
