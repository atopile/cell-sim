"""
CellSim v2 — FastAPI Dependencies

Dependency injection functions for accessing subsystem managers from route handlers.
All blocking I2C/GPIO calls are wrapped with run_in_executor internally.
"""

from __future__ import annotations

from fastapi import Depends, HTTPException, Request

from cellsim.app_state import AppState
from cellsim.card_manager import CardManager
from cellsim.models import NUM_SLOTS
from cellsim.slot_mux import SlotMux
from cellsim.slot_power import SlotPowerManager


def get_app_state(request: Request) -> AppState:
    """Retrieve the AppState stored on the FastAPI app instance."""
    state: AppState | None = getattr(request.app.state, "cellsim", None)
    if state is None:
        raise HTTPException(503, "System not initialized")
    return state


def get_slot_power(state: AppState = Depends(get_app_state)) -> SlotPowerManager:
    return state.slot_power


def get_slot_mux(state: AppState = Depends(get_app_state)) -> SlotMux:
    return state.slot_mux


def get_card_manager(state: AppState = Depends(get_app_state)) -> CardManager:
    return state.card_manager


def validate_slot_id(slot_id: int) -> int:
    """Path parameter validator for slot IDs."""
    if not 0 <= slot_id < NUM_SLOTS:
        raise HTTPException(400, f"Invalid slot ID: {slot_id} (must be 0–{NUM_SLOTS - 1})")
    return slot_id
