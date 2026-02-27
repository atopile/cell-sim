"""
CellSim v2 — Python Client Library

Connects to the CM5 REST API for remote control of the cell simulator.
Provides both synchronous (httpx) and asynchronous (httpx.AsyncClient) interfaces.

Usage (sync):
    from cellsim.client import CellSimClient

    client = CellSimClient("cellsim.local")
    state = client.get_state()
    client.set_cell(0, voltage=3.7, enabled=True)
    client.set_all_voltages(3.7)

Usage (async):
    from cellsim.client import AsyncCellSimClient

    async with AsyncCellSimClient("cellsim.local") as client:
        state = await client.get_state()
        await client.set_cell(0, voltage=3.7)

        async for frame in client.stream_measurements():
            print(frame["cards"]["0"]["cells"][0]["voltage_v"])
"""

from __future__ import annotations

import json
import logging
from contextlib import asynccontextmanager
from typing import AsyncIterator

import httpx

from cellsim.models import (
    CELLS_PER_CARD,
    NUM_SLOTS,
    BringupRequest,
    BringupStatus,
    CellRequest,
    FlashRequest,
    HealthResponse,
    KelvinMode,
    SlotPowerRequest,
    SlotPowerState,
    SystemRequest,
    SystemState,
)

logger = logging.getLogger(__name__)

DEFAULT_PORT = 8000
DEFAULT_TIMEOUT = 10.0


class CellSimError(Exception):
    """Raised when the CellSim API returns an error."""

    def __init__(self, status_code: int, detail: str):
        self.status_code = status_code
        self.detail = detail
        super().__init__(f"HTTP {status_code}: {detail}")


def _check(resp: httpx.Response) -> None:
    """Raise CellSimError on non-2xx responses."""
    if resp.status_code >= 400:
        try:
            detail = resp.json().get("detail", resp.text)
        except Exception:
            detail = resp.text
        raise CellSimError(resp.status_code, detail)


# ── Synchronous Client ──

class CellSimClient:
    """Synchronous client for the CellSim v2 REST API."""

    def __init__(
        self,
        host: str,
        port: int = DEFAULT_PORT,
        timeout: float = DEFAULT_TIMEOUT,
    ):
        self.base_url = f"http://{host}:{port}"
        self._client = httpx.Client(
            base_url=self.base_url,
            timeout=timeout,
        )

    def close(self) -> None:
        self._client.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ── Health ──

    def health(self) -> HealthResponse:
        resp = self._client.get("/api/health")
        _check(resp)
        return HealthResponse.model_validate(resp.json())

    # ── State ──

    def get_state(self) -> SystemState:
        resp = self._client.get("/api/state")
        _check(resp)
        return SystemState.model_validate(resp.json())

    # ── Cell Control ──

    def set_cell(
        self,
        cell_id: int,
        voltage: float | None = None,
        enabled: bool | None = None,
        output_relay: bool | None = None,
        load_switch: bool | None = None,
        kelvin_mode: KelvinMode | None = None,
    ) -> dict:
        """Configure a single cell."""
        req = CellRequest(
            id=cell_id,
            voltage_setpoint=voltage,
            enabled=enabled,
            output_relay_enabled=output_relay,
            load_switch_enabled=load_switch,
            kelvin_mode=kelvin_mode,
        )
        resp = self._client.post("/api/config/cell", json=req.model_dump(exclude_none=True))
        _check(resp)
        return resp.json()

    def set_all_voltages(self, voltage: float) -> dict:
        """Set all cells to the same voltage."""
        cells = [
            CellRequest(id=i, voltage_setpoint=voltage)
            for i in range(NUM_SLOTS * CELLS_PER_CARD)
        ]
        req = SystemRequest(cells=cells)
        resp = self._client.post("/api/config", json=req.model_dump(exclude_none=True))
        _check(resp)
        return resp.json()

    def enable_all_outputs(self, enabled: bool = True) -> dict:
        """Enable or disable output relays on all cells."""
        cells = [
            CellRequest(id=i, output_relay_enabled=enabled)
            for i in range(NUM_SLOTS * CELLS_PER_CARD)
        ]
        req = SystemRequest(cells=cells)
        resp = self._client.post("/api/config", json=req.model_dump(exclude_none=True))
        _check(resp)
        return resp.json()

    # ── Slots ──

    def get_slots(self) -> list[SlotPowerState]:
        resp = self._client.get("/api/slots")
        _check(resp)
        return [SlotPowerState.model_validate(s) for s in resp.json()]

    def set_slot_power(self, slot_id: int, enabled: bool) -> dict:
        req = SlotPowerRequest(enabled=enabled)
        resp = self._client.post(f"/api/slots/{slot_id}/power", json=req.model_dump())
        _check(resp)
        return resp.json()

    def reset_slot(self, slot_id: int) -> dict:
        resp = self._client.post(f"/api/slots/{slot_id}/reset")
        _check(resp)
        return resp.json()

    def flash_slot(self, slot_id: int, firmware_path: str) -> dict:
        req = FlashRequest(firmware_path=firmware_path)
        resp = self._client.post(
            f"/api/slots/{slot_id}/flash", json=req.model_dump(), timeout=120.0
        )
        _check(resp)
        return resp.json()

    # ── Cards ──

    def get_cards(self) -> list[dict]:
        resp = self._client.get("/api/cards")
        _check(resp)
        return resp.json()

    # ── Self-Test & Calibration ──

    def self_test(self) -> dict:
        resp = self._client.post("/api/self-test")
        _check(resp)
        return resp.json()

    def calibrate(self) -> dict:
        resp = self._client.post("/api/calibration")
        _check(resp)
        return resp.json()

    # ── Thermal ──

    def get_thermal(self) -> dict:
        resp = self._client.get("/api/thermal")
        _check(resp)
        return resp.json()

    # ── Bringup ──

    def start_bringup(self, firmware_path: str | None = None) -> dict:
        req = BringupRequest(firmware_path=firmware_path)
        resp = self._client.post("/api/bringup", json=req.model_dump())
        _check(resp)
        return resp.json()

    def get_bringup_status(self) -> BringupStatus:
        resp = self._client.get("/api/bringup/status")
        _check(resp)
        return BringupStatus.model_validate(resp.json())

    def abort_bringup(self) -> dict:
        resp = self._client.post("/api/bringup/abort")
        _check(resp)
        return resp.json()


# ── Asynchronous Client ──

class AsyncCellSimClient:
    """Asynchronous client for the CellSim v2 REST API."""

    def __init__(
        self,
        host: str,
        port: int = DEFAULT_PORT,
        timeout: float = DEFAULT_TIMEOUT,
    ):
        self.base_url = f"http://{host}:{port}"
        self.ws_url = f"ws://{host}:{port}"
        self._client = httpx.AsyncClient(
            base_url=self.base_url,
            timeout=timeout,
        )

    async def close(self) -> None:
        await self._client.aclose()

    async def __aenter__(self):
        return self

    async def __aexit__(self, *exc):
        await self.close()

    # ── Health ──

    async def health(self) -> HealthResponse:
        resp = await self._client.get("/api/health")
        _check(resp)
        return HealthResponse.model_validate(resp.json())

    # ── State ──

    async def get_state(self) -> SystemState:
        resp = await self._client.get("/api/state")
        _check(resp)
        return SystemState.model_validate(resp.json())

    # ── Cell Control ──

    async def set_cell(
        self,
        cell_id: int,
        voltage: float | None = None,
        enabled: bool | None = None,
        output_relay: bool | None = None,
        load_switch: bool | None = None,
        kelvin_mode: KelvinMode | None = None,
    ) -> dict:
        req = CellRequest(
            id=cell_id,
            voltage_setpoint=voltage,
            enabled=enabled,
            output_relay_enabled=output_relay,
            load_switch_enabled=load_switch,
            kelvin_mode=kelvin_mode,
        )
        resp = await self._client.post(
            "/api/config/cell", json=req.model_dump(exclude_none=True)
        )
        _check(resp)
        return resp.json()

    async def set_all_voltages(self, voltage: float) -> dict:
        cells = [
            CellRequest(id=i, voltage_setpoint=voltage)
            for i in range(NUM_SLOTS * CELLS_PER_CARD)
        ]
        req = SystemRequest(cells=cells)
        resp = await self._client.post(
            "/api/config", json=req.model_dump(exclude_none=True)
        )
        _check(resp)
        return resp.json()

    async def enable_all_outputs(self, enabled: bool = True) -> dict:
        cells = [
            CellRequest(id=i, output_relay_enabled=enabled)
            for i in range(NUM_SLOTS * CELLS_PER_CARD)
        ]
        req = SystemRequest(cells=cells)
        resp = await self._client.post(
            "/api/config", json=req.model_dump(exclude_none=True)
        )
        _check(resp)
        return resp.json()

    # ── Slots ──

    async def get_slots(self) -> list[SlotPowerState]:
        resp = await self._client.get("/api/slots")
        _check(resp)
        return [SlotPowerState.model_validate(s) for s in resp.json()]

    async def set_slot_power(self, slot_id: int, enabled: bool) -> dict:
        req = SlotPowerRequest(enabled=enabled)
        resp = await self._client.post(
            f"/api/slots/{slot_id}/power", json=req.model_dump()
        )
        _check(resp)
        return resp.json()

    async def reset_slot(self, slot_id: int) -> dict:
        resp = await self._client.post(f"/api/slots/{slot_id}/reset")
        _check(resp)
        return resp.json()

    async def flash_slot(self, slot_id: int, firmware_path: str) -> dict:
        req = FlashRequest(firmware_path=firmware_path)
        resp = await self._client.post(
            f"/api/slots/{slot_id}/flash", json=req.model_dump(), timeout=120.0
        )
        _check(resp)
        return resp.json()

    # ── Cards ──

    async def get_cards(self) -> list[dict]:
        resp = await self._client.get("/api/cards")
        _check(resp)
        return resp.json()

    # ── Self-Test & Calibration ──

    async def self_test(self) -> dict:
        resp = await self._client.post("/api/self-test")
        _check(resp)
        return resp.json()

    async def calibrate(self) -> dict:
        resp = await self._client.post("/api/calibration")
        _check(resp)
        return resp.json()

    # ── Thermal ──

    async def get_thermal(self) -> dict:
        resp = await self._client.get("/api/thermal")
        _check(resp)
        return resp.json()

    # ── Bringup ──

    async def start_bringup(self, firmware_path: str | None = None) -> dict:
        req = BringupRequest(firmware_path=firmware_path)
        resp = await self._client.post("/api/bringup", json=req.model_dump())
        _check(resp)
        return resp.json()

    async def get_bringup_status(self) -> BringupStatus:
        resp = await self._client.get("/api/bringup/status")
        _check(resp)
        return BringupStatus.model_validate(resp.json())

    async def abort_bringup(self) -> dict:
        resp = await self._client.post("/api/bringup/abort")
        _check(resp)
        return resp.json()

    # ── Streaming ──

    async def stream_measurements(
        self, interval_ms: int = 100
    ) -> AsyncIterator[dict]:
        """
        Stream real-time measurements via WebSocket.

        Yields dicts with structure:
            {"timestamp": float, "cards": {"0": {...}, ...}}
        """
        import websockets

        url = f"{self.ws_url}/ws/measurements?interval_ms={interval_ms}"
        async for ws in websockets.connect(url):
            try:
                async for msg in ws:
                    yield json.loads(msg)
            except websockets.ConnectionClosed:
                logger.warning("Measurement stream disconnected, reconnecting...")
                continue

    async def stream_bringup(self) -> AsyncIterator[BringupStatus]:
        """
        Stream bringup progress via SSE.

        Yields BringupStatus objects. Stream ends when bringup completes.
        """
        async with self._client.stream("GET", "/api/bringup/stream") as resp:
            _check(resp)
            async for line in resp.aiter_lines():
                if line.startswith("data: "):
                    data = json.loads(line[6:])
                    yield BringupStatus.model_validate(data)
