# This file is part of the faebryk project
# SPDX-License-Identifier: MIT

"""
TI-specific I2C address configuration module.

Many TI devices (TMP117, TMP1075, etc.) use a 4-state address pin scheme:
- ADD0 connected to GND → offset 0
- ADD0 connected to VCC → offset 1
- ADD0 connected to SDA → offset 2
- ADD0 connected to SCL → offset 3

Final address = base + offset (where base comes from datasheet)
"""

import logging
from typing import Self, cast

import pytest

import faebryk.core.faebrykpy as fbrk
import faebryk.core.node as fabll
import faebryk.library._F as F
from faebryk.core import graph
from faebryk.libs.app.checks import check_design

logger = logging.getLogger(__name__)


class TIAddressor(fabll.Node):
    """
    TI-specific I2C address configuration via 4-state address pin.

    Many TI temperature sensors and other I2C devices use a single address pin
    (typically ADD0) that can be connected to one of 4 destinations to select
    the device address:

    - GND → offset 0 (e.g., 0x48 for TMP117)
    - VCC → offset 1 (e.g., 0x49 for TMP117)
    - SDA → offset 2 (e.g., 0x4A for TMP117)
    - SCL → offset 3 (e.g., 0x4B for TMP117)

    The final address is calculated as: address = base + offset

    Example ato usage:
        addressor = new TIAddressor
        addressor.base = 0x48  # Base address from datasheet
        addressor.address_line.line ~ device.ADD0
        addressor.address_line.reference ~ power
        addressor.i2c ~ i2c_bus
        assert addressor.address is i2c.address
    """

    # ----------------------------------------
    #     parameters
    # ----------------------------------------
    address = F.Parameters.NumericParameter.MakeChild(
        unit=F.Units.Dimensionless,
        domain=F.NumberDomain.Args(negative=False, integer=True),
    )
    offset = F.Parameters.NumericParameter.MakeChild(
        unit=F.Units.Dimensionless,
        domain=F.NumberDomain.Args(negative=False, integer=True),
    )
    base = F.Parameters.NumericParameter.MakeChild(
        unit=F.Units.Dimensionless,
        domain=F.NumberDomain.Args(negative=False, integer=True),
    )

    # ----------------------------------------
    #     interfaces
    # ----------------------------------------
    address_line = F.ElectricLogic.MakeChild()
    i2c = F.I2C.MakeChild()

    # ----------------------------------------
    #                 traits
    # ----------------------------------------
    _is_module = fabll.Traits.MakeEdge(fabll.is_module.MakeChild())
    _single_electric_reference = fabll.Traits.MakeEdge(
        F.has_single_electric_reference.MakeChild()
    )

    # Design check trait for post-solve address line configuration
    design_check = fabll.Traits.MakeEdge(F.implements_design_check.MakeChild())

    # ----------------------------------------
    #     constraints (via MakeChild override)
    # ----------------------------------------
    @classmethod
    def MakeChild(cls) -> fabll._ChildField[Self]:  # type: ignore[override]
        """
        Create a TIAddressor child with constraints:
        - address = base + offset
        - 0 <= offset <= 3
        """
        out = fabll._ChildField(cls)

        # Constraint: address = base + offset
        add_expr = F.Expressions.Add.MakeChild(
            [out, cls.base],
            [out, cls.offset],
        )
        is_addr_constraint = F.Expressions.Is.MakeChild(
            [out, cls.address],
            [add_expr],
            assert_=True,
        )
        out.add_dependant(add_expr)
        out.add_dependant(is_addr_constraint)

        # Constraint: offset <= 3 (4 possible states: 0, 1, 2, 3)
        max_offset_lit = F.Literals.Numbers.MakeChild(
            min=3.0,
            max=3.0,
            unit=F.Units.Dimensionless,
        )
        offset_bound_constraint = F.Expressions.GreaterOrEqual.MakeChild(
            [max_offset_lit],
            [out, cls.offset],
            assert_=True,
        )
        out.add_dependant(max_offset_lit)
        out.add_dependant(offset_bound_constraint)

        return cast(fabll._ChildField[Self], out)

    # ----------------------------------------
    #     post-solve check
    # ----------------------------------------
    class OffsetNotResolvedError(F.implements_design_check.UnfulfilledCheckException):
        """Raised when the offset parameter cannot be resolved to a single value."""

        def __init__(self, addressor: "TIAddressor"):
            super().__init__(
                "TIAddressor offset must be constrained to a single value (0-3).",
                nodes=[addressor],
            )

    @F.implements_design_check.register_post_solve_check
    def __check_post_solve__(self):
        """
        Connect address_line to the correct destination based on offset.

        After the solver resolves the offset value (0-3), this method connects
        the address_line.line to the appropriate destination:
        - 0: GND (address_line.reference.lv)
        - 1: VCC (address_line.reference.hv)
        - 2: SDA (i2c.sda.line)
        - 3: SCL (i2c.scl.line)
        """
        # Try to extract offset from literal
        numbers = self.offset.get().try_extract_aliased_literal()
        if numbers is not None and numbers.is_literal.get().is_singleton():
            offset = int(numbers.get_single())
        else:
            # Attempt to use solver to deduce offset
            from faebryk.core.solver.defaultsolver import DefaultSolver
            from faebryk.core.solver.nullsolver import NullSolver

            solver = self.design_check.get().get_solver()
            if isinstance(solver, NullSolver):
                logger.warning("Solver is NullSolver, cannot deduce TIAddressor offset")
                return

            assert isinstance(solver, DefaultSolver)
            offset_cbo = self.offset.get().can_be_operand.get()
            offset_param = self.offset.get().is_parameter.get()
            logger.info(f"Running solver for TIAddressor: {self}")

            solver.update_superset_cache(offset_cbo)
            lit = solver.inspect_get_known_supersets(offset_param)
            if lit is None or not lit.is_singleton():
                # Offset not yet constrained - this is expected when the user
                # hasn't specified the I2C address. The address line connection
                # will be made later when the offset is known.
                logger.warning(
                    "TIAddressor offset not resolved - address line connection skipped. "
                    "Constrain the I2C address or offset to enable automatic connection."
                )
                return
            lit_node = fabll.Traits(lit).get_obj_raw()
            offset = int(lit_node.cast(F.Literals.Numbers).get_single())

        # Validate offset range
        if not 0 <= offset <= 3:
            raise ValueError(f"TIAddressor offset {offset} out of range [0, 3]")

        # Get destination based on offset
        # 0: GND, 1: VCC, 2: SDA, 3: SCL
        ref = self.address_line.get().reference.get()
        destinations = [
            ref.lv.get(),  # GND
            ref.hv.get(),  # VCC
            self.i2c.get().sda.get().line.get(),  # SDA
            self.i2c.get().scl.get().line.get(),  # SCL
        ]

        # Connect address line to the selected destination
        dest = destinations[offset]
        self.address_line.get().line.get()._is_interface.get().connect_to(dest)
        logger.debug(
            f"TIAddressor: Connected address_line to "
            f"{['GND', 'VCC', 'SDA', 'SCL'][offset]} (offset={offset})"
        )

    # ----------------------------------------
    #     usage example
    # ----------------------------------------
    usage_example = fabll.Traits.MakeEdge(
        F.has_usage_example.MakeChild(
            example="""
        import TIAddressor, I2C, ElectricPower

        # For TI devices with 4-state address pin (TMP117, TMP1075, etc.)
        addressor = new TIAddressor
        addressor.base = 0x48  # Base address from datasheet

        # Connect power reference for address pin
        power_3v3 = new ElectricPower
        addressor.address_line.reference ~ power_3v3

        # Connect address pin to device
        device.ADD0 ~ addressor.address_line.line

        # Connect I2C bus (needed for SDA/SCL address options)
        i2c_bus = new I2C
        addressor.i2c ~ i2c_bus

        # Constrain the address
        assert addressor.address is i2c_bus.address
        """,
            language=F.has_usage_example.Language.ato,
        ).put_on_type()
    )


# -----------------------------------------------------------------------------
#                                 Tests
# -----------------------------------------------------------------------------


def test_ti_addressor_basic_instantiation():
    """Test basic TIAddressor creation via MakeChild."""
    g = graph.GraphView.create()
    tg = fbrk.TypeGraph.create(g=g)

    class _App(fabll.Node):
        _is_module = fabll.Traits.MakeEdge(fabll.is_module.MakeChild())
        addressor = TIAddressor.MakeChild()

    app = _App.bind_typegraph(tg=tg).create_instance(g=g)

    # Verify all expected children exist
    assert app.addressor.get() is not None
    assert app.addressor.get().address.get() is not None
    assert app.addressor.get().offset.get() is not None
    assert app.addressor.get().base.get() is not None
    assert app.addressor.get().address_line.get() is not None
    assert app.addressor.get().i2c.get() is not None


def test_ti_addressor_parameters():
    """Test setting TIAddressor parameters."""
    g = graph.GraphView.create()
    tg = fbrk.TypeGraph.create(g=g)

    class _App(fabll.Node):
        _is_module = fabll.Traits.MakeEdge(fabll.is_module.MakeChild())
        addressor = TIAddressor.MakeChild()

    app = _App.bind_typegraph(tg=tg).create_instance(g=g)

    # Set base and offset
    app.addressor.get().base.get().alias_to_literal(g, float(0x48))
    app.addressor.get().offset.get().alias_to_literal(g, 2.0)

    # Verify they were set
    assert (
        int(app.addressor.get().base.get().force_extract_literal().get_single()) == 0x48
    )
    assert (
        int(app.addressor.get().offset.get().force_extract_literal().get_single()) == 2
    )


@pytest.mark.parametrize(
    "offset,expected_dest",
    [
        (0, "GND"),
        (1, "VCC"),
        (2, "SDA"),
        (3, "SCL"),
    ],
)
def test_ti_addressor_sets_address_line(offset: int, expected_dest: str):
    """Test that address_line is connected to correct destination based on offset."""
    from faebryk.core.solver.defaultsolver import DefaultSolver

    g = graph.GraphView.create()
    tg = fbrk.TypeGraph.create(g=g)

    class _App(fabll.Node):
        _is_module = fabll.Traits.MakeEdge(fabll.is_module.MakeChild())
        power = F.ElectricPower.MakeChild()
        i2c = F.I2C.MakeChild()
        addressor = TIAddressor.MakeChild()

    app = _App.bind_typegraph(tg=tg).create_instance(g=g)

    # Connect references
    app.addressor.get().address_line.get().reference.get()._is_interface.get().connect_to(
        app.power.get()
    )
    app.addressor.get().i2c.get()._is_interface.get().connect_to(app.i2c.get())
    app.i2c.get().scl.get().reference.get()._is_interface.get().connect_to(
        app.power.get()
    )
    app.i2c.get().sda.get().reference.get()._is_interface.get().connect_to(
        app.power.get()
    )

    # Set base and offset
    app.addressor.get().base.get().alias_to_literal(g, float(0x48))
    app.addressor.get().offset.get().alias_to_literal(g, float(offset))

    # Run solver
    solver = DefaultSolver()
    solver.simplify(g, tg)
    fabll.Traits.create_and_add_instance_to(app, F.has_solver).setup(solver)

    # Run post-solve checks
    check_design(app, stage=F.implements_design_check.CheckStage.POST_SOLVE)

    # Verify connection
    addr_line = app.addressor.get().address_line.get().line.get()

    if expected_dest == "GND":
        assert addr_line._is_interface.get().is_connected_to(
            app.power.get().lv.get()
        ), f"Expected address_line connected to GND for offset={offset}"
    elif expected_dest == "VCC":
        assert addr_line._is_interface.get().is_connected_to(
            app.power.get().hv.get()
        ), f"Expected address_line connected to VCC for offset={offset}"
    elif expected_dest == "SDA":
        assert addr_line._is_interface.get().is_connected_to(
            app.i2c.get().sda.get().line.get()
        ), f"Expected address_line connected to SDA for offset={offset}"
    elif expected_dest == "SCL":
        assert addr_line._is_interface.get().is_connected_to(
            app.i2c.get().scl.get().line.get()
        ), f"Expected address_line connected to SCL for offset={offset}"


def test_ti_addressor_with_i2c():
    """Test TIAddressor integration with I2C address constraint."""
    from faebryk.core.solver.defaultsolver import DefaultSolver

    g = graph.GraphView.create()
    tg = fbrk.TypeGraph.create(g=g)

    class _App(fabll.Node):
        _is_module = fabll.Traits.MakeEdge(fabll.is_module.MakeChild())
        power = F.ElectricPower.MakeChild()
        i2c = F.I2C.MakeChild()
        addressor = TIAddressor.MakeChild()

    app = _App.bind_typegraph(tg=tg).create_instance(g=g)

    # Connect references
    app.addressor.get().address_line.get().reference.get()._is_interface.get().connect_to(
        app.power.get()
    )
    app.addressor.get().i2c.get()._is_interface.get().connect_to(app.i2c.get())
    app.i2c.get().scl.get().reference.get()._is_interface.get().connect_to(
        app.power.get()
    )
    app.i2c.get().sda.get().reference.get()._is_interface.get().connect_to(
        app.power.get()
    )

    # Set base=0x48, offset=1 → address should be 0x49
    app.addressor.get().base.get().alias_to_literal(g, float(0x48))
    app.addressor.get().offset.get().alias_to_literal(g, 1.0)

    # Link addressor.address to i2c.address
    F.Expressions.Is.c(
        app.addressor.get().address.get().can_be_operand.get(),
        app.i2c.get().address.get().can_be_operand.get(),
        g=g,
        tg=tg,
        assert_=True,
    )

    # Run solver
    solver = DefaultSolver()
    solver.simplify(g, tg)
    fabll.Traits.create_and_add_instance_to(app, F.has_solver).setup(solver)

    # Run post-solve checks
    check_design(app, stage=F.implements_design_check.CheckStage.POST_SOLVE)

    # Verify address was computed correctly (0x48 + 1 = 0x49)
    i2c_addr_param = app.i2c.get().address.get().is_parameter.get()
    result = solver.inspect_get_known_supersets(i2c_addr_param)
    assert result is not None
    assert result.is_singleton()
    result_node = fabll.Traits(result).get_obj_raw().cast(F.Literals.Numbers)
    assert int(result_node.get_single()) == 0x49


def test_ti_addressor_expression_propagation():
    """Test that solver can deduce offset from address and base."""
    from faebryk.core.solver.defaultsolver import DefaultSolver

    g = graph.GraphView.create()
    tg = fbrk.TypeGraph.create(g=g)

    class _App(fabll.Node):
        _is_module = fabll.Traits.MakeEdge(fabll.is_module.MakeChild())
        addressor = TIAddressor.MakeChild()

    app = _App.bind_typegraph(tg=tg).create_instance(g=g)

    # Set base=0x48, address=0x4A → offset should be deduced as 2
    app.addressor.get().base.get().alias_to_literal(g, float(0x48))
    app.addressor.get().address.get().alias_to_literal(g, float(0x4A))

    # Run solver
    solver = DefaultSolver()
    offset_cbo = app.addressor.get().offset.get().can_be_operand.get()
    offset_param = app.addressor.get().offset.get().is_parameter.get()

    solver.update_superset_cache(offset_cbo)
    result = solver.inspect_get_known_supersets(offset_param)

    assert result is not None, "Solver should deduce offset"
    assert result.is_singleton(), "Offset should be a singleton"

    result_node = fabll.Traits(result).get_obj_raw().cast(F.Literals.Numbers)
    assert int(result_node.get_single()) == 2, (
        f"Offset should be 2, got {result_node.get_single()}"
    )
