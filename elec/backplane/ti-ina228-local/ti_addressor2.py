# This file is part of the faebryk project
# SPDX-License-Identifier: MIT
import logging

import faebryk.library._F as F
from faebryk.core.moduleinterface import ModuleInterface
from faebryk.libs.library import L

logger = logging.getLogger(__name__)


class TIAddressor2(ModuleInterface):
    address = L.p_field(domain=L.Domains.Numbers.NATURAL())
    offset = L.p_field(domain=L.Domains.Numbers.NATURAL())
    base = L.p_field(domain=L.Domains.Numbers.NATURAL())
    num_addresses = L.p_field(domain=L.Domains.Numbers.NATURAL())
    address_line_a0: F.ElectricLogic
    address_line_a1: F.ElectricLogic
    i2c: F.I2C

    @L.rt_field
    def single_electric_reference(self):
        return F.has_single_electric_reference_defined(
            F.ElectricLogic.connect_all_module_references(self)
        )

    def __preinit__(self) -> None:
        for x in (self.address, self.offset, self.base):
            x.constrain_ge(0)

        self.address.alias_is(self.base + self.offset)

        # A0 address line connections (offset & 0x3)
        # 0: GND, 1: VS, 2: SDA, 3: SCL
        a0_destinations = [
            self.address_line_a0.reference.lv,  # GND
            self.address_line_a0.reference.hv,  # VS
            self.i2c.sda.line,  # SDA
            self.i2c.scl.line,  # SCL
        ]

        for i, dest in enumerate(a0_destinations):
            (self.offset.operation_is_subset(i)).if_then_else(
                lambda dest=dest: self.address_line_a0.line.connect(dest)
            )
            (self.offset.operation_is_subset(i + 4)).if_then_else(
                lambda dest=dest: self.address_line_a0.line.connect(dest)
            )
            (self.offset.operation_is_subset(i + 8)).if_then_else(
                lambda dest=dest: self.address_line_a0.line.connect(dest)
            )
            (self.offset.operation_is_subset(i + 12)).if_then_else(
                lambda dest=dest: self.address_line_a0.line.connect(dest)
            )

        # A1 address line connections
        # 0-3: GND, 4-7: VS, 8-11: SDA, 12-15: SCL
        a1_destinations = [
            self.address_line_a1.reference.lv,  # GND
            self.address_line_a1.reference.hv,  # VS
            self.i2c.sda.line,  # SDA
            self.i2c.scl.line,  # SCL
        ]

        for i, dest in enumerate(a1_destinations):
            for j in range(4):
                (self.offset.operation_is_subset(i * 4 + j)).if_then_else(
                    lambda dest=dest: self.address_line_a1.line.connect(dest)
                )
