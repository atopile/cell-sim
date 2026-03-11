# This file is part of the faebryk project
# SPDX-License-Identifier: MIT

"""
I2C Isolator base module for bridging two I2C interfaces.

Connects two I2C interfaces together logically without connecting their
underlying electrical signals. Useful for passing logical I2C connections
through galvanic isolators, switches, or level shifters.
"""

import faebryk.core.faebrykpy as fbrk
import faebryk.core.node as fabll
import faebryk.library._F as F


class I2CIsolator(fabll.Node):
    """
    Connects two I2C interfaces together without connecting the underlying
    electrical signals.

    power_rails[0]/power_rails[1] and i2cs[0]/i2cs[1] represent the two
    isolated sides of the I2C bus.
    """

    _is_module = fabll.Traits.MakeEdge(fabll.is_module.MakeChild())

    # Power interfaces for each side
    power_rails = [F.ElectricPower.MakeChild() for _ in range(2)]

    # I2C interfaces for each side
    i2cs = [F.I2C.MakeChild() for _ in range(2)]

    # Shallow connection between I2C interfaces - logical connection without electrical
    _i2c_connection = fabll.MakeEdge(
        [i2cs[0]],
        [i2cs[1]],
        edge=fbrk.EdgeInterfaceConnection.build(shallow=True),
    )

    # Bridge trait for ~> syntax
    _can_bridge = fabll.Traits.MakeEdge(
        F.can_bridge.MakeChild(in_=["i2cs[0]"], out_=["i2cs[1]"])
    )
