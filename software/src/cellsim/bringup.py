"""
CellSim v2 — Autonomous Bringup Orchestrator

State machine that discovers cards, powers them on, flashes firmware if needed,
runs self-tests, and validates every cell. Designed to be invoked via REST API.

Bringup Sequence:
    Phase 0: Base Board Health — verify 24V bus, thermal
    Phase 1: Card Discovery — read card-present lines
    Phase 2: Per-Slot Bringup — sequential power-on, flash, self-test, validate
    Phase 3: System Validation — all cards active, stability test

Each slot goes through:
    POWERED_OFF → POWERING_ON → DISCOVERING → FLASHING → TESTING → VALIDATING → READY
    Any failure → FAILED (card stays powered for debugging)

Cancellation:
    The orchestrator checks self._cancelled before each phase and after each
    blocking operation. Call abort() from any other coroutine to signal stop.
"""

from __future__ import annotations

import asyncio
import logging
import struct
import time
from functools import partial

from cellsim.card_manager import CardManager
from cellsim.models import (
    CELLS_PER_CARD,
    NUM_SLOTS,
    BringupPhase,
    BringupResult,
    BringupSlotResult,
    SlotState,
)
from cellsim.protocol import Cmd, Status, encode_enable_output, encode_set_voltage
from cellsim.slot_mux import SlotMux
from cellsim.slot_power import SlotPowerManager
from cellsim.uart_flasher import FlashProgress, UartFlasher

logger = logging.getLogger(__name__)

# Timing constants
POWER_SETTLE_S = 0.5
MDNS_DISCOVER_TIMEOUT_S = 5
BOOT_TIMEOUT_S = 3
CELL_SETTLE_S = 0.2

# Validation thresholds
VOLTAGE_TOLERANCE_MV = 5.0
MIN_BUS_VOLTAGE_V = 22.0
MAX_BUS_VOLTAGE_V = 26.0
MAX_BOARD_TEMP_C = 60.0
VALIDATION_VOLTAGE_MV = 2500


class BringupOrchestrator:
    """
    Autonomous bringup state machine with cancellation support.

    Usage:
        orch = BringupOrchestrator(slot_power, slot_mux, card_manager)
        result = await orch.run()
    """

    def __init__(
        self,
        slot_power: SlotPowerManager,
        slot_mux: SlotMux,
        card_manager: CardManager,
        firmware_path: str | None = None,
    ):
        self._slot_power = slot_power
        self._slot_mux = slot_mux
        self._card_manager = card_manager
        self._firmware_path = firmware_path
        self._result = BringupResult()
        self._running = False
        self._cancelled = False

    @property
    def result(self) -> BringupResult:
        return self._result

    @property
    def is_running(self) -> bool:
        return self._running

    def _check_cancelled(self) -> None:
        """Raise if abort() was called."""
        if self._cancelled:
            raise asyncio.CancelledError("Bringup aborted")

    async def _run_blocking(self, fn, *args):
        """Run a blocking function (I2C/GPIO) in the default executor."""
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, partial(fn, *args))

    async def run(self) -> BringupResult:
        """Execute the full bringup sequence."""
        self._running = True
        self._cancelled = False
        self._result = BringupResult(
            started_at=time.time(),
            slots=[BringupSlotResult(slot_id=i) for i in range(NUM_SLOTS)],
        )

        try:
            await self._phase_base_health()
            self._check_cancelled()
            if self._result.phase == BringupPhase.FAILED:
                return self._result

            await self._phase_card_discovery()
            self._check_cancelled()

            await self._phase_slot_bringup()
            self._check_cancelled()

            await self._phase_system_validation()

        except asyncio.CancelledError:
            self._result.phase = BringupPhase.FAILED
            self._result.error = "Aborted by user"
        except Exception as e:
            logger.exception("Bringup failed")
            self._result.phase = BringupPhase.FAILED
            self._result.error = str(e)
        finally:
            self._result.duration_s = time.time() - self._result.started_at
            self._running = False

        return self._result

    async def abort(self) -> None:
        """Signal the orchestrator to stop after the current operation."""
        logger.warning("Bringup ABORT requested")
        self._cancelled = True
        self._running = False

        # Safe state: disable all outputs, deselect mux
        await self._run_blocking(self._slot_power.disable_all)
        await self._run_blocking(self._slot_mux.deselect)

        self._result.phase = BringupPhase.FAILED
        self._result.error = "Aborted by user"

    # ── Phase 0: Base Board Health ──

    async def _phase_base_health(self) -> None:
        self._result.phase = BringupPhase.BASE_HEALTH
        logger.info("Phase 0: Base board health check")

        bus = await self._run_blocking(self._slot_power.read_bus_status)
        self._result.bus_voltage_v = bus.voltage_v
        self._result.bus_current_a = bus.current_a

        if not (MIN_BUS_VOLTAGE_V <= bus.voltage_v <= MAX_BUS_VOLTAGE_V):
            self._result.phase = BringupPhase.FAILED
            self._result.error = (
                f"Bus voltage {bus.voltage_v:.1f}V out of range "
                f"({MIN_BUS_VOLTAGE_V}–{MAX_BUS_VOLTAGE_V}V)"
            )
            logger.error(self._result.error)
            return

        # Check e-stop
        estop = await self._run_blocking(self._slot_power.get_estop_state)
        if estop:
            self._result.phase = BringupPhase.FAILED
            self._result.error = "E-stop is active"
            logger.error(self._result.error)
            return

        logger.info("Bus: %.1fV, %.2fA — OK", bus.voltage_v, bus.current_a)

    # ── Phase 1: Card Discovery ──

    async def _phase_card_discovery(self) -> None:
        self._result.phase = BringupPhase.CARD_DISCOVERY
        logger.info("Phase 1: Card discovery")

        card_present = await self._run_blocking(self._slot_power.get_card_present)
        found = []
        for i, present in enumerate(card_present):
            self._result.slots[i].card_present = present
            if present:
                found.append(i)
            else:
                self._result.slots[i].state = SlotState.SKIPPED

        self._result.cards_found = len(found)
        logger.info("Cards found in slots: %s (%d total)", found, len(found))

    # ── Phase 2: Per-Slot Bringup ──

    async def _phase_slot_bringup(self) -> None:
        self._result.phase = BringupPhase.SLOT_BRINGUP
        logger.info("Phase 2: Per-slot bringup")

        for slot in self._result.slots:
            self._check_cancelled()
            if not slot.card_present:
                continue

            t0 = time.time()
            try:
                await self._bringup_slot(slot)
            except asyncio.CancelledError:
                raise
            except Exception as e:
                logger.exception("Slot %d bringup failed", slot.slot_id)
                slot.state = SlotState.FAILED
                slot.error = str(e)
            finally:
                slot.duration_s = time.time() - t0

            if slot.state == SlotState.READY:
                self._result.cards_ready += 1
                self._result.total_cells_passed += slot.cells_validated
            elif slot.state == SlotState.FAILED:
                self._result.cards_failed += 1

    async def _bringup_slot(self, slot: BringupSlotResult) -> None:
        sid = slot.slot_id
        logger.info("--- Slot %d: starting bringup ---", sid)

        # Power on
        slot.state = SlotState.POWERING_ON
        await self._run_blocking(self._slot_power.enable_slot, sid)
        await asyncio.sleep(POWER_SETTLE_S)
        self._check_cancelled()

        # Verify power
        voltage = await self._run_blocking(self._slot_power.read_slot_voltage, sid)
        current = await self._run_blocking(self._slot_power.read_slot_current, sid)
        logger.info("Slot %d: %.1fV, %.3fA after power-on", sid, voltage, current)

        if voltage < 20.0:
            slot.state = SlotState.FAILED
            slot.error = f"Low voltage after power-on: {voltage:.1f}V"
            return

        # Wait for mDNS discovery
        slot.state = SlotState.DISCOVERING
        ip = await self._discover_card(sid)

        if ip is None and self._firmware_path:
            logger.info("Slot %d: not discovered, attempting flash", sid)
            slot.state = SlotState.FLASHING
            self._check_cancelled()

            success = await self._flash_slot(sid)
            if not success:
                slot.state = SlotState.FAILED
                slot.error = "Flash failed"
                return
            slot.firmware_flashed = True

            await asyncio.sleep(BOOT_TIMEOUT_S)
            ip = await self._discover_card(sid)

        if ip is None:
            slot.state = SlotState.FAILED
            slot.error = "Card not discoverable"
            return

        slot.ip_address = ip
        logger.info("Slot %d: discovered at %s", sid, ip)

        # Self-test via TCP
        slot.state = SlotState.TESTING
        self._check_cancelled()
        test_passed = await self._run_self_test(sid)
        slot.self_test_passed = test_passed

        if not test_passed:
            slot.state = SlotState.FAILED
            slot.error = "Self-test failed"
            return

        # Validate cells via TCP
        slot.state = SlotState.VALIDATING
        self._check_cancelled()
        cells_ok = await self._validate_cells(sid)
        slot.cells_validated = cells_ok

        if cells_ok < slot.cells_total:
            slot.state = SlotState.FAILED
            slot.error = f"Only {cells_ok}/{slot.cells_total} cells passed"
            self._result.total_cells_failed += (slot.cells_total - cells_ok)
        else:
            slot.state = SlotState.READY
            logger.info("Slot %d: READY (%d/%d cells)", sid, cells_ok, slot.cells_total)

    async def _discover_card(self, slot_id: int) -> str | None:
        """Wait for a card to appear via mDNS/scan. Returns IP or None."""
        deadline = time.monotonic() + MDNS_DISCOVER_TIMEOUT_S
        while time.monotonic() < deadline:
            card = self._card_manager.cards.get(slot_id)
            if card and card.online:
                return card.ip_address
            await asyncio.sleep(0.5)
        return None

    async def _flash_slot(self, slot_id: int) -> bool:
        """Flash firmware via UART bootloader through the mux."""
        if self._firmware_path is None:
            return False

        await self._run_blocking(self._slot_mux.enter_bootloader, slot_id)
        await asyncio.sleep(0.5)

        uart_dev = self._slot_mux.get_uart_device()
        flasher = UartFlasher(uart_dev)

        try:
            await self._run_blocking(flasher.open)
            connected = await self._run_blocking(flasher.connect)
            if not connected:
                logger.error("Slot %d: bootloader connection failed", slot_id)
                return False

            def progress_cb(p: FlashProgress):
                logger.info("Slot %d flash: %s %.0f%%", slot_id, p.stage, p.percent)

            success = await self._run_blocking(
                flasher.flash_firmware, self._firmware_path, progress_cb
            )
            return success
        finally:
            await self._run_blocking(flasher.close)
            await self._run_blocking(self._slot_mux.exit_bootloader, slot_id)

    async def _run_self_test(self, slot_id: int) -> bool:
        """Send CMD_SELF_TEST to a card and check the result."""
        try:
            status, payload = await self._card_manager.send_command(
                slot_id, Cmd.SELF_TEST, timeout=10.0
            )
            if status != Status.OK:
                logger.error("Slot %d: self-test returned status %s", slot_id, status.name)
                return False

            # Payload contains per-cell pass/fail bitmap (1 byte)
            if payload:
                bitmap = payload[0]
                all_pass = bitmap == 0xFF
                if not all_pass:
                    for i in range(CELLS_PER_CARD):
                        if not (bitmap & (1 << i)):
                            logger.error("Slot %d cell %d: self-test FAILED", slot_id, i)
                return all_pass

            return True  # No payload = card didn't send details, trust status
        except ConnectionError as e:
            logger.error("Slot %d: self-test command failed: %s", slot_id, e)
            return False

    async def _validate_cells(self, slot_id: int) -> int:
        """
        Validate each cell: set voltage, read back, toggle relay.
        Returns count of cells that passed.
        """
        passed = 0

        for cell_id in range(CELLS_PER_CARD):
            self._check_cancelled()
            try:
                ok = await self._validate_single_cell(slot_id, cell_id)
                if ok:
                    passed += 1
            except ConnectionError as e:
                logger.error("Slot %d cell %d: validation failed: %s", slot_id, cell_id, e)

        # Cleanup: set all cells to 0V, disable outputs
        for cell_id in range(CELLS_PER_CARD):
            try:
                await self._card_manager.send_command(
                    slot_id, Cmd.SET_VOLTAGE, cell_id, encode_set_voltage(0)
                )
                await self._card_manager.send_command(
                    slot_id, Cmd.ENABLE_OUTPUT, cell_id, encode_enable_output(False)
                )
            except ConnectionError:
                pass

        return passed

    async def _validate_single_cell(self, slot_id: int, cell_id: int) -> bool:
        """Validate a single cell: set 2.5V, check readback, toggle relay."""
        cm = self._card_manager

        # Set voltage
        status, _ = await cm.send_command(
            slot_id, Cmd.SET_VOLTAGE, cell_id,
            encode_set_voltage(VALIDATION_VOLTAGE_MV),
        )
        if status != Status.OK:
            logger.error("Slot %d cell %d: SET_VOLTAGE failed (%s)", slot_id, cell_id, status.name)
            return False

        # Enable output
        status, _ = await cm.send_command(
            slot_id, Cmd.ENABLE_OUTPUT, cell_id,
            encode_enable_output(True),
        )
        if status != Status.OK:
            return False

        # Wait for settle
        await asyncio.sleep(CELL_SETTLE_S)

        # Read measurement from UDP stream
        meas = self._card_manager.measurements.get(slot_id)
        if meas is None or meas.cells is None:
            logger.warning("Slot %d cell %d: no measurement data", slot_id, cell_id)
            return False

        cell_meas = meas.cells[cell_id]
        error_mv = abs(cell_meas.voltage_mv - VALIDATION_VOLTAGE_MV)

        if error_mv > VOLTAGE_TOLERANCE_MV:
            logger.error(
                "Slot %d cell %d: voltage %dmV (expected %dmV ±%.0fmV)",
                slot_id, cell_id, cell_meas.voltage_mv,
                VALIDATION_VOLTAGE_MV, VOLTAGE_TOLERANCE_MV,
            )
            return False

        # Disable output — voltage should drop
        await cm.send_command(
            slot_id, Cmd.ENABLE_OUTPUT, cell_id, encode_enable_output(False)
        )
        await asyncio.sleep(CELL_SETTLE_S)

        # Re-read: voltage should be near 0
        meas = self._card_manager.measurements.get(slot_id)
        if meas and meas.cells:
            if meas.cells[cell_id].voltage_mv > 100:  # >100mV with relay off is a fault
                logger.error(
                    "Slot %d cell %d: relay OFF but voltage still %dmV",
                    slot_id, cell_id, meas.cells[cell_id].voltage_mv,
                )
                return False

        logger.info("Slot %d cell %d: PASS", slot_id, cell_id)
        return True

    # ── Phase 3: System Validation ──

    async def _phase_system_validation(self) -> None:
        self._result.phase = BringupPhase.SYSTEM_VALIDATION
        logger.info("Phase 3: System validation")

        # Set all ready cards to 3.7V, stream for 2s, check stability
        ready_slots = [s for s in self._result.slots if s.state == SlotState.READY]
        if not ready_slots:
            self._result.phase = BringupPhase.COMPLETE
            return

        # Set all cells to 3.7V
        for slot in ready_slots:
            for cell_id in range(CELLS_PER_CARD):
                try:
                    await self._card_manager.send_command(
                        slot.slot_id, Cmd.SET_VOLTAGE, cell_id,
                        encode_set_voltage(3700),
                    )
                    await self._card_manager.send_command(
                        slot.slot_id, Cmd.ENABLE_OUTPUT, cell_id,
                        encode_enable_output(True),
                    )
                except ConnectionError:
                    pass

        # Let voltages stabilize
        await asyncio.sleep(2.0)
        self._check_cancelled()

        # Check all measurements are within tolerance
        all_stable = True
        for slot in ready_slots:
            meas = self._card_manager.measurements.get(slot.slot_id)
            if meas is None or meas.cells is None:
                continue
            for i, cell in enumerate(meas.cells):
                if abs(cell.voltage_mv - 3700) > VOLTAGE_TOLERANCE_MV:
                    logger.warning(
                        "Slot %d cell %d: system validation drift %dmV",
                        slot.slot_id, i, cell.voltage_mv,
                    )
                    all_stable = False

        # Cleanup: all outputs off
        for slot in ready_slots:
            for cell_id in range(CELLS_PER_CARD):
                try:
                    await self._card_manager.send_command(
                        slot.slot_id, Cmd.SET_VOLTAGE, cell_id,
                        encode_set_voltage(0),
                    )
                    await self._card_manager.send_command(
                        slot.slot_id, Cmd.ENABLE_OUTPUT, cell_id,
                        encode_enable_output(False),
                    )
                except ConnectionError:
                    pass

        self._result.phase = BringupPhase.COMPLETE
        logger.info(
            "Bringup COMPLETE: %d/%d cards ready, %d cells passed, %d failed, stable=%s",
            self._result.cards_ready,
            self._result.cards_found,
            self._result.total_cells_passed,
            self._result.total_cells_failed,
            all_stable,
        )
