"""
CellSim v2 — CM5 Orchestration Server

FastAPI REST server running on the Raspberry Pi CM5.
Discovers cards on the internal Ethernet network, aggregates measurements,
and exposes a unified API for cell control, calibration, monitoring, and
autonomous bringup.

Architecture:
    CM5 eth0 (uplink) -> serves REST API to lab network / host PC
    CM5 eth1 (internal) -> communicates with cards over private 10.0.0.x subnet
    Cards are never directly exposed to the external network.
"""

from __future__ import annotations

import asyncio
import logging
import time
from contextlib import asynccontextmanager

from fastapi import Depends, FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import StreamingResponse

from cellsim.app_state import AppState
from cellsim.bringup import BringupOrchestrator
from cellsim.card_manager import CardManager
from cellsim.deps import (
    get_app_state,
    get_card_manager,
    get_slot_mux,
    get_slot_power,
    validate_slot_id,
)
from cellsim.models import (
    CELLS_PER_CARD,
    NUM_SLOTS,
    BringupRequest,
    BringupStatus,
    CellRequest,
    ErrorResponse,
    FlashRequest,
    HealthResponse,
    KelvinMode,
    SlotPowerRequest,
    SlotPowerState,
    SystemRequest,
    SystemState,
)
from cellsim.protocol import MeasurementPacket
from cellsim.slot_mux import SlotMux
from cellsim.slot_power import SlotPowerManager
from cellsim.uart_flasher import UartFlasher

logger = logging.getLogger(__name__)

_start_time: float = 0.0


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Startup: initialize hardware, discover cards. Shutdown: clean up."""
    global _start_time
    _start_time = time.monotonic()

    state = AppState()
    app.state.cellsim = state

    try:
        await state.startup()
    except Exception:
        logger.exception("Startup failed — running in degraded mode")

    yield

    await state.shutdown()


app = FastAPI(
    title="CellSim v2",
    version="2.0.0-dev",
    lifespan=lifespan,
    responses={503: {"model": ErrorResponse}},
)


# ── Health ──

@app.get("/api/health")
async def health(state: AppState = Depends(get_app_state)) -> HealthResponse:
    """Liveness / readiness probe."""
    online = sum(1 for c in state.card_manager.cards.values() if c.online)
    return HealthResponse(
        uptime=time.monotonic() - _start_time,
        cards_online=online,
        e_stop_active=state.slot_power.get_estop_state(),
    )


# ── System State ──

@app.get("/api/state")
async def get_state(state: AppState = Depends(get_app_state)) -> SystemState:
    """Get complete system state: all cards, cells, thermistors, thermal."""
    from cellsim.models import CardState, CellState, Measurement, ThermalState

    cards_out = []
    cm = state.card_manager

    for slot_id, conn in cm.cards.items():
        if not conn.online:
            continue

        cells = []
        meas = cm.measurements.get(slot_id)
        if meas and meas.cells:
            for i, c in enumerate(meas.cells):
                cells.append(CellState(
                    id=slot_id * CELLS_PER_CARD + i,
                    card_slot=slot_id,
                    cell_index=i,
                    voltage_setpoint=c.setpoint_v,
                    voltage_measured=Measurement(value=c.voltage_v),
                    current_measured=Measurement(value=c.current_a),
                    cell_temperature=c.temp_c,
                    enabled=c.output_enabled,
                    output_relay_enabled=c.relay_closed,
                    kelvin_mode=KelvinMode.FOUR_WIRE if c.four_wire else KelvinMode.TWO_WIRE,
                    self_test_passed=bool(c.flags & 0x10),
                ))

        board_temp = meas.mcu_temp_c if meas else 0.0
        input_v = meas.vin_v if meas else 0.0

        cards_out.append(CardState(
            slot=slot_id,
            card_id=conn.card_id,
            card_type=conn.card_type,
            firmware_version=conn.firmware_version,
            ip_address=conn.ip_address,
            cells=cells,
            board_temperature=board_temp,
            input_voltage=input_v,
            online=conn.online,
        ))

    loop = asyncio.get_running_loop()
    estop = await loop.run_in_executor(None, state.slot_power.get_estop_state)

    # Aggregate thermal from all cards
    card_temps = {c.slot: c.board_temperature for c in cards_out}
    thermal = ThermalState(card_temperatures=card_temps)

    return SystemState(
        cards=cards_out,
        thermal=thermal,
        total_cells=sum(len(c.cells) for c in cards_out),
        uptime=time.monotonic() - _start_time,
        e_stop_active=estop,
    )


@app.post("/api/config")
async def set_config(
    request: SystemRequest,
    state: AppState = Depends(get_app_state),
):
    """Bulk configure cells and thermistors."""
    from cellsim.protocol import Cmd, encode_enable_output, encode_set_mode, encode_set_voltage

    cm = state.card_manager
    errors = []

    for cell_req in request.cells:
        slot_id = cell_req.id // CELLS_PER_CARD
        cell_idx = cell_req.id % CELLS_PER_CARD

        try:
            if cell_req.voltage_setpoint is not None:
                mv = int(cell_req.voltage_setpoint * 1000)
                await cm.send_command(slot_id, Cmd.SET_VOLTAGE, cell_idx, encode_set_voltage(mv))

            if cell_req.output_relay_enabled is not None:
                await cm.send_command(
                    slot_id, Cmd.ENABLE_OUTPUT, cell_idx,
                    encode_enable_output(cell_req.output_relay_enabled),
                )

            if cell_req.kelvin_mode is not None:
                four_wire = cell_req.kelvin_mode == KelvinMode.FOUR_WIRE
                await cm.send_command(
                    slot_id, Cmd.SET_MODE, cell_idx, encode_set_mode(four_wire)
                )
        except (ConnectionError, ValueError) as e:
            errors.append({"cell_id": cell_req.id, "error": str(e)})

    return {"accepted": len(request.cells), "errors": errors}


@app.post("/api/config/cell")
async def set_cell_config(
    request: CellRequest,
    state: AppState = Depends(get_app_state),
):
    """Configure a single cell."""
    from cellsim.protocol import Cmd, encode_enable_output, encode_set_mode, encode_set_voltage

    slot_id = request.id // CELLS_PER_CARD
    cell_idx = request.id % CELLS_PER_CARD
    cm = state.card_manager

    try:
        if request.voltage_setpoint is not None:
            mv = int(request.voltage_setpoint * 1000)
            await cm.send_command(slot_id, Cmd.SET_VOLTAGE, cell_idx, encode_set_voltage(mv))

        if request.output_relay_enabled is not None:
            await cm.send_command(
                slot_id, Cmd.ENABLE_OUTPUT, cell_idx,
                encode_enable_output(request.output_relay_enabled),
            )

        if request.kelvin_mode is not None:
            four_wire = request.kelvin_mode == KelvinMode.FOUR_WIRE
            await cm.send_command(
                slot_id, Cmd.SET_MODE, cell_idx, encode_set_mode(four_wire)
            )
    except ConnectionError as e:
        raise HTTPException(502, f"Card communication failed: {e}")

    return {"cell_id": request.id, "accepted": True}


# ── Card Management ──

@app.get("/api/cards")
async def get_cards(state: AppState = Depends(get_app_state)):
    """Get card discovery status, firmware versions, health."""
    return [
        {
            "slot_id": c.slot_id,
            "card_type": c.card_type,
            "card_id": c.card_id,
            "ip_address": c.ip_address,
            "firmware_version": c.firmware_version,
            "online": c.online,
            "last_seen": c.last_seen,
        }
        for c in state.card_manager.cards.values()
    ]


# ── Self-Test & Calibration ──

@app.post("/api/self-test")
async def self_test(state: AppState = Depends(get_app_state)):
    """Run BIST on all online cards. Returns per-card results."""
    from cellsim.protocol import Cmd, Status

    cm = state.card_manager
    results = {}

    for slot_id, card in cm.cards.items():
        if not card.online:
            continue

        try:
            status, payload = await cm.send_command(slot_id, Cmd.SELF_TEST, timeout=10.0)
            results[slot_id] = {
                "status": status.name,
                "passed": status == Status.OK,
                "payload": payload.hex() if payload else None,
            }
        except ConnectionError as e:
            results[slot_id] = {"status": "CONNECTION_ERROR", "passed": False, "error": str(e)}

    return {"results": results}


@app.post("/api/calibration")
async def calibrate(state: AppState = Depends(get_app_state)):
    """Run calibration on all online cards."""
    from cellsim.protocol import Cmd, Status

    cm = state.card_manager
    results = {}

    for slot_id, card in cm.cards.items():
        if not card.online:
            continue

        try:
            status, payload = await cm.send_command(slot_id, Cmd.CALIBRATE, timeout=30.0)
            results[slot_id] = {
                "status": status.name,
                "payload": payload.hex() if payload else None,
            }
        except ConnectionError as e:
            results[slot_id] = {"status": "CONNECTION_ERROR", "error": str(e)}

    return {"results": results}


# ── Thermal ──

@app.get("/api/thermal")
async def get_thermal(state: AppState = Depends(get_app_state)):
    """Get temperatures from measurement stream and bus status."""
    from cellsim.models import ThermalState

    cm = state.card_manager
    card_temps = {}
    for slot_id, meas in cm.measurements.items():
        card_temps[slot_id] = meas.mcu_temp_c

    loop = asyncio.get_running_loop()
    bus = await loop.run_in_executor(None, state.slot_power.read_bus_status)

    return ThermalState(
        card_temperatures=card_temps,
    ).model_dump()


# ── Firmware ──

@app.post("/api/firmware/upload")
async def upload_firmware(state: AppState = Depends(get_app_state)):
    """Push firmware update to cards over Ethernet (SMP/MCUmgr)."""
    raise HTTPException(501, "OTA firmware update not yet implemented")


# ── Slot Management ──

@app.get("/api/slots")
async def get_slots(
    slot_power: SlotPowerManager = Depends(get_slot_power),
) -> list[SlotPowerState]:
    """Get per-slot power state, card presence, voltage, and current."""
    loop = asyncio.get_running_loop()
    statuses = await loop.run_in_executor(None, slot_power.get_all_slot_status)
    return [
        SlotPowerState(
            slot_id=s.slot_id,
            power_enabled=s.power_enabled,
            card_present=s.card_present,
            bus_voltage_v=s.bus_voltage_v,
            current_a=s.current_a,
        )
        for s in statuses
    ]


@app.post("/api/slots/{slot_id}/power")
async def set_slot_power(
    slot_id: int = Depends(validate_slot_id),
    request: SlotPowerRequest = ...,
    slot_power: SlotPowerManager = Depends(get_slot_power),
):
    """Enable or disable power to a slot."""
    loop = asyncio.get_running_loop()
    if request.enabled:
        await loop.run_in_executor(None, slot_power.enable_slot, slot_id)
    else:
        await loop.run_in_executor(None, slot_power.disable_slot, slot_id)

    return {"slot_id": slot_id, "power_enabled": request.enabled}


@app.post("/api/slots/{slot_id}/reset")
async def reset_slot(
    slot_id: int = Depends(validate_slot_id),
    slot_mux: SlotMux = Depends(get_slot_mux),
):
    """Reset a card by pulsing its NRST line."""
    loop = asyncio.get_running_loop()
    await loop.run_in_executor(None, slot_mux.reset_card, slot_id)
    return {"slot_id": slot_id, "reset": True}


@app.post("/api/slots/{slot_id}/flash")
async def flash_slot(
    request: FlashRequest,
    slot_id: int = Depends(validate_slot_id),
    slot_mux: SlotMux = Depends(get_slot_mux),
):
    """Flash firmware to a card via UART bootloader."""
    loop = asyncio.get_running_loop()

    # Enter bootloader (blocking GPIO/I2C)
    await loop.run_in_executor(None, slot_mux.enter_bootloader, slot_id)

    uart_dev = slot_mux.get_uart_device()
    flasher = UartFlasher(uart_dev)

    try:
        await loop.run_in_executor(None, flasher.open)

        connected = await loop.run_in_executor(None, flasher.connect)
        if not connected:
            raise HTTPException(500, "Bootloader connection failed")

        success = await loop.run_in_executor(
            None, flasher.flash_firmware, request.firmware_path
        )
        if not success:
            raise HTTPException(500, "Flash failed")
    finally:
        await loop.run_in_executor(None, flasher.close)
        await loop.run_in_executor(None, slot_mux.exit_bootloader, slot_id)

    return {"slot_id": slot_id, "flashed": True}


# ── Bringup ──

@app.post("/api/bringup")
async def start_bringup(
    request: BringupRequest,
    state: AppState = Depends(get_app_state),
):
    """Start the autonomous bringup sequence."""
    if state.bringup_task and not state.bringup_task.done():
        raise HTTPException(409, "Bringup already in progress")

    orchestrator = BringupOrchestrator(
        slot_power=state.slot_power,
        slot_mux=state.slot_mux,
        card_manager=state.card_manager,
        firmware_path=request.firmware_path,
    )

    # Store orchestrator on app state so status endpoint can read it
    state._bringup = orchestrator
    state.bringup_task = asyncio.create_task(orchestrator.run())

    return {"status": "started"}


@app.get("/api/bringup/status")
async def get_bringup_status(
    state: AppState = Depends(get_app_state),
) -> BringupStatus:
    """Get the current bringup progress."""
    orchestrator = getattr(state, "_bringup", None)
    if orchestrator is None:
        return BringupStatus()

    r = orchestrator.result
    return BringupStatus(
        phase=r.phase,
        running=orchestrator.is_running,
        bus_voltage_v=r.bus_voltage_v,
        bus_current_a=r.bus_current_a,
        cards_found=r.cards_found,
        cards_ready=r.cards_ready,
        cards_failed=r.cards_failed,
        total_cells_passed=r.total_cells_passed,
        total_cells_failed=r.total_cells_failed,
        duration_s=r.duration_s,
        error=r.error,
        slots=[
            s for s in r.slots if s.card_present
        ] if hasattr(r, "slots") else [],
    )


@app.post("/api/bringup/abort")
async def abort_bringup(state: AppState = Depends(get_app_state)):
    """Abort the bringup sequence."""
    orchestrator = getattr(state, "_bringup", None)
    if orchestrator is None or not orchestrator.is_running:
        raise HTTPException(400, "No bringup in progress")

    await orchestrator.abort()
    return {"status": "aborted"}


# ── WebSocket: Live Measurement Stream ──

@app.websocket("/ws/measurements")
async def ws_measurements(
    websocket: WebSocket,
    interval_ms: int = 100,
):
    """
    Stream real-time cell measurements over WebSocket.

    Sends a JSON message every `interval_ms` milliseconds containing
    the latest measurement packet for each online card. Clients can
    specify a slower interval (e.g., 500ms for dashboards).

    Wire format (JSON per frame):
        {
          "timestamp": <float>,
          "cards": {
            "0": { "slot_id": 0, "cells": [...], "mcu_temp_c": ..., ... },
            ...
          }
        }
    """
    await websocket.accept()
    state: AppState = getattr(websocket.app.state, "cellsim", None)
    if state is None:
        await websocket.close(code=1011, reason="System not initialized")
        return

    interval_s = max(interval_ms, 10) / 1000.0  # floor at 10ms

    try:
        while True:
            cards_data = {}
            for slot_id, pkt in state.card_manager.measurements.items():
                cards_data[str(slot_id)] = {
                    "slot_id": pkt.slot_id,
                    "sequence": pkt.sequence,
                    "timestamp_us": pkt.timestamp_us,
                    "cells": [
                        {
                            "setpoint_v": c.setpoint_v,
                            "voltage_v": c.voltage_v,
                            "current_ma": c.current_ma,
                            "temp_c": c.temp_c,
                            "output_enabled": c.output_enabled,
                            "four_wire": c.four_wire,
                            "over_current": c.over_current,
                            "over_temp": c.over_temp,
                        }
                        for c in (pkt.cells or [])
                    ],
                    "mcu_temp_c": pkt.mcu_temp_c,
                    "vin_v": pkt.vin_v,
                    "uptime_ms": pkt.uptime_ms,
                }

            await websocket.send_json({
                "timestamp": time.time(),
                "cards": cards_data,
            })
            await asyncio.sleep(interval_s)

    except WebSocketDisconnect:
        pass
    except Exception:
        logger.exception("WebSocket measurement stream error")
        try:
            await websocket.close(code=1011)
        except Exception:
            pass


# ── SSE: Bringup Progress Stream ──

@app.get("/api/bringup/stream")
async def stream_bringup(
    state: AppState = Depends(get_app_state),
) -> StreamingResponse:
    """
    Server-Sent Events stream of bringup progress.

    Each event is a JSON-serialized BringupStatus. The stream ends
    when bringup completes or is aborted.
    """

    async def event_generator():
        prev_phase = None
        while True:
            orchestrator = getattr(state, "_bringup", None)
            if orchestrator is None:
                yield _sse_event({"phase": "idle", "running": False})
                return

            r = orchestrator.result
            status = BringupStatus(
                phase=r.phase,
                running=orchestrator.is_running,
                bus_voltage_v=r.bus_voltage_v,
                bus_current_a=r.bus_current_a,
                cards_found=r.cards_found,
                cards_ready=r.cards_ready,
                cards_failed=r.cards_failed,
                total_cells_passed=r.total_cells_passed,
                total_cells_failed=r.total_cells_failed,
                duration_s=r.duration_s,
                error=r.error,
                slots=[s for s in r.slots if s.card_present]
                if hasattr(r, "slots") else [],
            )

            yield _sse_event(status.model_dump())

            # Stop streaming when bringup finishes
            if not orchestrator.is_running:
                return

            await asyncio.sleep(0.5)

    return StreamingResponse(
        event_generator(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
            "X-Accel-Buffering": "no",
        },
    )


def _sse_event(data: dict) -> str:
    """Format a dict as an SSE event string."""
    import json
    return f"data: {json.dumps(data)}\n\n"


# ── Entry Point ──

def run():
    """Entry point for `cellsim` CLI command."""
    import uvicorn

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)-8s %(name)s: %(message)s",
    )
    uvicorn.run(app, host="0.0.0.0", port=8000)
