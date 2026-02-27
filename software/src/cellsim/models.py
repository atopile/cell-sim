"""
CellSim v2 — Shared Data Models (Pydantic)

Pydantic models shared between server, client, and card protocol.
All models use BaseModel for automatic FastAPI validation and serialization.
"""

from __future__ import annotations

from datetime import datetime
from enum import StrEnum

from pydantic import BaseModel, Field, field_validator

# ── Constants ──

NUM_SLOTS = 16
CELLS_PER_CARD = 8
MAX_VOLTAGE_V = 5.0
MAX_CURRENT_A = 1.0


# ── Enums ──

class KelvinMode(StrEnum):
    FOUR_WIRE = "4-wire"
    TWO_WIRE = "2-wire"


class CardType(StrEnum):
    CELL = "cell"
    THERMISTOR = "thermistor"


class FanMode(StrEnum):
    AUTO = "auto"
    MANUAL = "manual"


class SlotState(StrEnum):
    POWERED_OFF = "powered_off"
    POWERING_ON = "powering_on"
    DISCOVERING = "discovering"
    FLASHING = "flashing"
    TESTING = "testing"
    VALIDATING = "validating"
    READY = "ready"
    FAILED = "failed"
    SKIPPED = "skipped"


class BringupPhase(StrEnum):
    IDLE = "idle"
    BASE_HEALTH = "base_health"
    CARD_DISCOVERY = "card_discovery"
    SLOT_BRINGUP = "slot_bringup"
    SYSTEM_VALIDATION = "system_validation"
    COMPLETE = "complete"
    FAILED = "failed"


# ── Measurement / Cell / Card State ──

class Measurement(BaseModel):
    value: float = 0.0
    timestamp: datetime = Field(default_factory=datetime.now)


class CellState(BaseModel):
    id: int = 0
    card_slot: int = 0
    cell_index: int = 0
    voltage_setpoint: float = 0.0
    voltage_measured: Measurement = Field(default_factory=Measurement)
    current_measured: Measurement = Field(default_factory=Measurement)
    buck_voltage: float = 0.0
    ldo_voltage: float = 0.0
    cell_temperature: float = 0.0
    enabled: bool = False
    output_relay_enabled: bool = False
    load_switch_enabled: bool = False
    kelvin_mode: KelvinMode = KelvinMode.FOUR_WIRE
    self_test_passed: bool = False


class ThermistorState(BaseModel):
    id: int = 0
    card_slot: int = 0
    channel_index: int = 0
    voltage_setpoint: float = 0.0
    voltage_measured: float = 0.0


class CardState(BaseModel):
    slot: int = 0
    card_id: str = ""
    card_type: CardType = CardType.CELL
    firmware_version: str = "unknown"
    ip_address: str = ""
    cells: list[CellState] = Field(default_factory=list)
    thermistors: list[ThermistorState] = Field(default_factory=list)
    board_temperature: float = 0.0
    input_voltage: float = 0.0
    self_test_passed: bool = False
    online: bool = False


class ThermalState(BaseModel):
    card_temperatures: dict[int, float] = Field(default_factory=dict)
    base_board_temperature: float = 0.0
    exhaust_temperature: float = 0.0
    fan_duty_percent: float = 0.0
    fan_rpm: float = 0.0
    fan_mode: FanMode = FanMode.AUTO


class SystemState(BaseModel):
    cards: list[CardState] = Field(default_factory=list)
    thermal: ThermalState = Field(default_factory=ThermalState)
    total_cells: int = 0
    total_thermistors: int = 0
    e_stop_active: bool = False
    uptime: float = 0.0


# ── Slot / Power State ──

class SlotPowerState(BaseModel):
    slot_id: int = 0
    power_enabled: bool = False
    card_present: bool = False
    bus_voltage_v: float = 0.0
    current_a: float = 0.0


class BusStatus(BaseModel):
    voltage_v: float = 0.0
    current_a: float = 0.0
    power_w: float = 0.0


# ── Self-Test ──

class CellTestResult(BaseModel):
    i2c_isolator_ok: bool = False
    dac_buck_ok: bool = False
    dac_ldo_ok: bool = False
    adc_ok: bool = False
    gpio_ok: bool = False
    temp_ok: bool = False
    relay_ok: bool = False
    temp_celsius: float = 0.0


class SelfTestResult(BaseModel):
    slot_id: int = 0
    i2c_bus_ok: list[bool] = Field(default_factory=lambda: [False, False])
    tca9548a_ok: list[bool] = Field(default_factory=lambda: [False, False])
    phy_ok: bool = False
    mcu_temp_celsius: float = 0.0
    input_voltage: float = 0.0
    cells: list[CellTestResult] = Field(
        default_factory=lambda: [CellTestResult() for _ in range(CELLS_PER_CARD)]
    )
    all_passed: bool = False


# ── Bringup State ──

class BringupSlotResult(BaseModel):
    slot_id: int = 0
    state: SlotState = SlotState.POWERED_OFF
    card_present: bool = False
    firmware_flashed: bool = False
    self_test_passed: bool = False
    cells_validated: int = 0
    cells_total: int = CELLS_PER_CARD
    ip_address: str = ""
    error: str | None = None
    duration_s: float = 0.0


class BringupResult(BaseModel):
    phase: BringupPhase = BringupPhase.IDLE
    bus_voltage_v: float = 0.0
    bus_current_a: float = 0.0
    board_temp_c: float = 0.0
    slots: list[BringupSlotResult] = Field(default_factory=list)
    cards_found: int = 0
    cards_ready: int = 0
    cards_failed: int = 0
    total_cells_passed: int = 0
    total_cells_failed: int = 0
    started_at: float = 0.0
    duration_s: float = 0.0
    error: str | None = None


class BringupStatus(BaseModel):
    """Serialized bringup status returned by GET /api/bringup/status."""
    phase: BringupPhase = BringupPhase.IDLE
    running: bool = False
    bus_voltage_v: float = 0.0
    bus_current_a: float = 0.0
    cards_found: int = 0
    cards_ready: int = 0
    cards_failed: int = 0
    total_cells_passed: int = 0
    total_cells_failed: int = 0
    duration_s: float = 0.0
    error: str | None = None
    slots: list[BringupSlotResult] = Field(default_factory=list)


# ── Calibration ──

class CellCalibration(BaseModel):
    card_id: str
    cell_index: int
    calibrated_at: datetime = Field(default_factory=datetime.now)
    temperature_c: float = 25.0
    voltage_dac_codes: list[int] = Field(default_factory=list)
    voltage_internal: list[float] = Field(default_factory=list)
    voltage_reference: list[float] = Field(default_factory=list)
    current_internal: list[float] = Field(default_factory=list)
    current_reference: list[float] = Field(default_factory=list)


class CardCalibration(BaseModel):
    card_id: str
    card_type: CardType = CardType.CELL
    hw_revision: str = ""
    cells: list[CellCalibration] = Field(default_factory=list)


# ── Request Models ──

class CellRequest(BaseModel):
    id: int
    voltage_setpoint: float | None = None
    enabled: bool | None = None
    output_relay_enabled: bool | None = None
    load_switch_enabled: bool | None = None
    kelvin_mode: KelvinMode | None = None

    @field_validator("voltage_setpoint")
    @classmethod
    def voltage_in_range(cls, v: float | None) -> float | None:
        if v is not None and not (0.0 <= v <= MAX_VOLTAGE_V):
            raise ValueError(f"voltage must be 0–{MAX_VOLTAGE_V} V, got {v}")
        return v


class ThermistorRequest(BaseModel):
    id: int
    voltage_setpoint: float | None = None


class SystemRequest(BaseModel):
    cells: list[CellRequest] = Field(default_factory=list)
    thermistors: list[ThermistorRequest] = Field(default_factory=list)


class SlotPowerRequest(BaseModel):
    enabled: bool


class FlashRequest(BaseModel):
    firmware_path: str


class BringupRequest(BaseModel):
    firmware_path: str | None = None


class CalibrationStartRequest(BaseModel):
    card_id: str | None = None  # None = all cards


# ── Response Models ──

class HealthResponse(BaseModel):
    status: str = "ok"
    version: str = "2.0.0-dev"
    uptime: float = 0.0
    cards_online: int = 0
    total_slots: int = NUM_SLOTS
    e_stop_active: bool = False


class ErrorResponse(BaseModel):
    detail: str
    code: str | None = None
