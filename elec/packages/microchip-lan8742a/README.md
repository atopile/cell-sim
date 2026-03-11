# Microchip LAN8742A 10/100 Ethernet PHY (RMII)

Minimal driver for LAN8742A with RMII interface and MDIO management.

- JLCPN: C621424
- Datasheet: https://jlcpcb.com/api/file/downloadByFileSystemAccessId/8588905531406749696

## Usage

```ato
#pragma experiment("FOR_LOOP")
#pragma experiment("BRIDGE_CONNECT")

import ElectricPower
import I2C
import ElectricLogic
from "atopile/microchip-lan8742a/microchip-lan8742a.ato" import Microchip_LAN8742A
from "atopile/microchip-lan8742a/microchip-lan8742a.ato" import RMII
from "atopile/microchip-lan8742a/microchip-lan8742a.ato" import MDIO

module Usage:
    """
    Minimal usage example: connect power and RMII interface.
    """
    power_3v3 = new ElectricPower
    phy = new Microchip_LAN8742A
    rmii = new RMII
    mdio = new MDIO

    power_3v3 ~ phy.power_3v3
    rmii ~ phy.rmii
    mdio ~ phy.mdio
```
