# Mounting Holes

Various mounting holes for PCB mechanical attachment.

## Supported Sizes

- M2 (2.2mm hole)
- M2.5 (2.7mm hole)
- M3 (3.2mm hole)
- M4 (4.3mm hole)
- M5 (5.3mm hole)
- M6 (6.4mm hole)
- M8 (8.4mm hole)

## Supported Pad Types

- No Pad (no electrical contact)
- Pad (standard pad)
- Pad_TopBottom (pads on both sides)
- Pad_TopOnly (pad on top layer only)
- Pad_Via (pad with vias for thermal/electrical connection)

## Usage

```ato
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_2_M2
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_2_M2_Pad
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_2_M2_Pad_TopBottom
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_2_M2_Pad_TopOnly
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_2_M2_Pad_Via
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_7_M2_5
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_7_M2_5_Pad
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_7_M2_5_Pad_TopBottom
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_7_M2_5_Pad_TopOnly
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_2_7_M2_5_Pad_Via
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_3_2_M3
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_3_2_M3_Pad
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_3_2_M3_Pad_TopBottom
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_3_2_M3_Pad_TopOnly
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_3_2_M3_Pad_Via
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_4_3_M4
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_4_3_M4_Pad
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_4_3_M4_Pad_TopBottom
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_4_3_M4_Pad_TopOnly
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_4_3_M4_Pad_Via
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_5_3_M5
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_5_3_M5_Pad
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_5_3_M5_Pad_TopBottom
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_5_3_M5_Pad_TopOnly
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_5_3_M5_Pad_Via
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_6_4_M6
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_6_4_M6_Pad
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_6_4_M6_Pad_TopBottom
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_6_4_M6_Pad_TopOnly
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_6_4_M6_Pad_Via
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_8_4_M8
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_8_4_M8_Pad
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_8_4_M8_Pad_TopBottom
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_8_4_M8_Pad_TopOnly
from "atopile/mounting-holes/mounting-holes.ato" import MountingHole_8_4_M8_Pad_Via

module Usage:
    mountinghole_2_2_m2 = new MountingHole_2_2_M2
    mountinghole_2_2_m2_pad = new MountingHole_2_2_M2_Pad
    mountinghole_2_2_m2_pad_topbottom = new MountingHole_2_2_M2_Pad_TopBottom
    mountinghole_2_2_m2_pad_toponly = new MountingHole_2_2_M2_Pad_TopOnly
    mountinghole_2_2_m2_pad_via = new MountingHole_2_2_M2_Pad_Via
    mountinghole_2_7_m2_5 = new MountingHole_2_7_M2_5
    mountinghole_2_7_m2_5_pad = new MountingHole_2_7_M2_5_Pad
    mountinghole_2_7_m2_5_pad_topbottom = new MountingHole_2_7_M2_5_Pad_TopBottom
    mountinghole_2_7_m2_5_pad_toponly = new MountingHole_2_7_M2_5_Pad_TopOnly
    mountinghole_2_7_m2_5_pad_via = new MountingHole_2_7_M2_5_Pad_Via
    mountinghole_3_2_m3 = new MountingHole_3_2_M3
    mountinghole_3_2_m3_pad = new MountingHole_3_2_M3_Pad
    mountinghole_32m3padtopbottom = new MountingHole_3_2_M3_Pad_TopBottom
    mountinghole_32m3padtoponly = new MountingHole_3_2_M3_Pad_TopOnly
    mountinghole_32m3padvia = new MountingHole_3_2_M3_Pad_Via
    mountinghole_43m4 = new MountingHole_4_3_M4
    mountinghole_43m4pad = new MountingHole_4_3_M4_Pad
    mountinghole_43m4padtopbottom = new MountingHole_4_3_M4_Pad_TopBottom
    mountinghole_43m4padtoponly = new MountingHole_4_3_M4_Pad_TopOnly
    mountinghole_43m4padvia = new MountingHole_4_3_M4_Pad_Via
    mountinghole_53m5 = new MountingHole_5_3_M5
    mountinghole_53m5pad = new MountingHole_5_3_M5_Pad
    mountinghole_53m5padtopbottom = new MountingHole_5_3_M5_Pad_TopBottom
    mountinghole_53m5padtoponly = new MountingHole_5_3_M5_Pad_TopOnly
    mountinghole_53m5padvia = new MountingHole_5_3_M5_Pad_Via
    mountinghole_64m6 = new MountingHole_6_4_M6
    mountinghole_64m6pad = new MountingHole_6_4_M6_Pad
    mountinghole_64m6padtopbottom = new MountingHole_6_4_M6_Pad_TopBottom
    mountinghole_64m6padtoponly = new MountingHole_6_4_M6_Pad_TopOnly
    mountinghole_64m6padvia = new MountingHole_6_4_M6_Pad_Via
    mountinghole_84m8 = new MountingHole_8_4_M8
    mountinghole_84m8pad = new MountingHole_8_4_M8_Pad
    mountinghole_84m8padtopbottom = new MountingHole_8_4_M8_Pad_TopBottom
    mountinghole_84m8padtoponly = new MountingHole_8_4_M8_Pad_TopOnly
    mountinghole_84m8padvia = new MountingHole_8_4_M8_Pad_Via

    mountinghole_2_2_m2_pad.contact ~ mountinghole_2_2_m2_pad_topbottom.contact
```

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
