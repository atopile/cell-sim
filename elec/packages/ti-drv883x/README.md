# Texas Instruments DRV8837/DRV8838 H-Bridge Motor Drivers

Low-voltage H-bridge motor drivers for brushed DC motors.

- **DRV8837**: IN0/IN1 control interface
- **DRV8838**: PH/EN (Phase/Enable) control interface

## Features

- Logic voltage: 1.8V to 7V
- Motor voltage: 0V to 11V
- Integrated decoupling capacitors
- Sleep control for low-power mode

## Usage

```ato
#pragma experiment("TRAITS")
#pragma experiment("BRIDGE_CONNECT")
#pragma experiment("MODULE_TEMPLATING")

import ElectricPower
import ElectricLogic
import Electrical
import can_bridge_by_name

from "atopile/ti-drv883x/ti-drv883x.ato" import TI_DRV8837
from "atopile/ti-drv883x/ti-drv883x.ato" import TI_DRV8838

module FakeMotor:
    """
    Mock motor device for testing
    """
    unnamed = new Electrical[2]
    trait can_bridge_by_name<input_name = "unnamed[0]", output_name = "unnamed[1]">

module Usage:
    """
    Example usage of the TI_DRV8837 and TI_DRV8838 H-bridge (motor) drivers
    """

    power_3v3 = new ElectricPower
    power_3v3.voltage = 3.3V +/- 5%
    power_6v = new ElectricPower
    power_6v.voltage = 6V +/- 5%

    # some mock gpio from e.g. a gpio expander
    gpio_0 = new ElectricLogic
    gpio_1 = new ElectricLogic
    gpio_3 = new ElectricLogic

    motor_driver_in0_in1 = new TI_DRV8837
    motor_driver_in0_in1.logic_power ~ power_3v3
    motor_driver_in0_in1.motor_power ~ power_6v
    motor_driver_in0_in1.sleep ~ gpio_3
    motor_driver_in0_in1.inputs[0] ~ gpio_0
    motor_driver_in0_in1.inputs[1] ~ gpio_1
    motor_0 = new FakeMotor
    motor_driver_in0_in1.motor_outs[0] ~> motor_0 ~> motor_driver_in0_in1.motor_outs[1]

    motor_driver_ph_en = new TI_DRV8838
    motor_driver_ph_en.logic_power ~ power_3v3
    motor_driver_ph_en.motor_power ~ power_6v
    motor_driver_ph_en.sleep ~ gpio_3
    motor_driver_ph_en.phase ~ gpio_0
    motor_driver_ph_en.enable ~ gpio_1
    motor_1 = new FakeMotor
    motor_driver_ph_en.motor_outs[0] ~> motor_1 ~> motor_driver_ph_en.motor_outs[1]

```

### DRV8837 Truth Table (IN0/IN1)

| nSLEEP | IN1 | IN2 | OUT1 | OUT2 | Function |
| ------ | --- | --- | ---- | ---- | -------- |
| 0      | X   | X   | Z    | Z    | Coast    |
| 1      | 0   | 0   | Z    | Z    | Coast    |
| 1      | 0   | 1   | L    | H    | Reverse  |
| 1      | 1   | 0   | H    | L    | Forward  |
| 1      | 1   | 1   | L    | L    | Brake    |

### DRV8838 Truth Table (PH/EN)

| nSLEEP | PH  | EN  | OUT1 | OUT2 | Function |
| ------ | --- | --- | ---- | ---- | -------- |
| 0      | X   | X   | Z    | Z    | Coast    |
| 1      | X   | 0   | L    | L    | Brake    |
| 1      | 1   | 1   | L    | H    | Reverse  |
| 1      | 0   | 1   | H    | L    | Forward  |

## Contributing

Contributions to this package are welcome via pull requests on the GitHub repository.

## License

This atopile package is provided under the [MIT License](https://opensource.org/license/mit/).
