# ARM Cortex Programming Headers

Collection of ARM Cortex debug headers that wire JTAG and SWD pinouts for common connector formats, including 2.54mm box headers and Tag-Connect footprints. The modules expose shared JTAG and SWD interfaces so you can drop in the connector that fits your target or programming cable.

## Usage

```ato
from "atopile/programming-headers/programming-headers.ato" import HDR_2_54mm_10pin_ARM_Cortex
from "atopile/programming-headers/programming-headers.ato" import IDC_2_54mm_10pin_ARM_Cortex
from "atopile/programming-headers/programming-headers.ato" import IDC_2_54mm_20pin_ARM
from "atopile/programming-headers/programming-headers.ato" import TC2050_ARM_Cortex

module Usage:
    hdr_2_54mm_10pin_arm_cortex = new HDR_2_54mm_10pin_ARM_Cortex
    idc_2_54mm_10pin_arm_cortex = new IDC_2_54mm_10pin_ARM_Cortex
    idc_2_54mm_20pin_arm = new IDC_2_54mm_20pin_ARM
    tc2050_arm_cortex = new TC2050_ARM_Cortex

```

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

## License

This package is provided under the [MIT License](mdc:packages/packages/packages/https:/opensource.org/license/mit).
