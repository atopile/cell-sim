# TI CD74HC4067SM 16-Channel Analog Multiplexer/Demultiplexer

Atopile driver for the TI CD74HC4067SM 16-Channel Analog Multiplexer/Demultiplexer.

The CD74HC4067SM is a 16-channel analog multiplexer/demultiplexer with 4 select lines.

## Features

- 16-channel analog multiplexer/demultiplexer
- 4 bit select lines
- 16 inputs/output
- 1 input/output

## Usage

```ato
#pragma experiment("FOR_LOOP")
#pragma experiment("TRAITS")

# --- Standard library imports ---
import ElectricPower
import has_part_removed

# --- Package import ---
from "atopile/ti-cd74hc4067sm/ti-cd74hc4067sm.ato" import TI_CD74HC4067SM

module FakeMCU:
    """
    Fake MCU to control the multiplexer.
    """
    power = new ElectricPower
    gpio = new ElectricLogic[5]

    trait has_part_removed

module Usage:
    """
    Minimal usage example for the `TI_CD74HC4067SM` 16-Channel Analog Multiplexer/Demultiplexer.
    """

    multiplexer = new TI_CD74HC4067SM
    mcu = new FakeMCU

    some_logic = new ElectricLogic

    power = new ElectricPower
    power.voltage = 5V +/- 5%

    multiplexer.power ~ power
    mcu.power ~ power

    multiplexer.selects[0].line ~ mcu.gpio[0].line
    multiplexer.selects[1].line ~ mcu.gpio[1].line
    multiplexer.selects[2].line ~ mcu.gpio[2].line
    multiplexer.selects[3].line ~ mcu.gpio[3].line

    multiplexer.common.line ~ mcu.gpio[4].line

    multiplexer.ios[0].line ~ power.lv
    multiplexer.ios[1].line ~ power.hv
    multiplexer.ios[2] ~ some_logic

```

## Contributing

Contributions are welcome! Please open an issue or pull request and ensure the `usage` build target passes (`ato build usage`).

## License

This package is provided under the [MIT License](https://opensource.org/license/mit/).
