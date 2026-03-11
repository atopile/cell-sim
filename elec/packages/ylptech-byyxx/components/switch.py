# This file is part of the faebryk project
# SPDX-License-Identifier: MIT
import logging

import faebryk.library._F as F
from faebryk.core.module import Module
from faebryk.libs.library import L

logger = logging.getLogger(__name__)


class Switch(Module):
    """
    Switch
    """

    unnamed = L.list_field(2, F.Electrical)

    @L.rt_field
    def can_bridge(self):
        return F.can_bridge_defined(*self.unnamed)


class PowerSwitch(Module):
    """
    Power Switch
    """

    unnamed = L.list_field(2, F.ElectricPower)

    @L.rt_field
    def can_bridge(self):
        return F.can_bridge_defined(*self.unnamed)
