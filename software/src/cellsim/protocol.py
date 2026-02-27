"""
CellSim v2 — Wire Protocol

Python mirror of firmware/card/src/network.h.
Defines command opcodes, status codes, and packet encode/decode helpers
for CM5 ↔ card communication over TCP (commands) and UDP (measurements).

TCP command frame (CM5 → card):
    [cmd:u8][cell_id:u8][payload_len:u16][payload:...]
TCP response frame (card → CM5):
    [status:u8][payload_len:u16][payload:...]

UDP measurement packet (card → CM5 at 100 Hz):
    [slot_id:u8][seq:u32][timestamp_us:u64]
    × 8 cells: [setpoint_mv:u16][voltage_mv:u16][current_ua:u32][temp_c10:i16][flags:u8]
    [mcu_temp_c10:i16][vin_mv:u16][uptime_ms:u32]
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum

# ── Network Ports ──

TCP_CMD_PORT = 5001
UDP_MEAS_PORT = 5000

# ── Constants ──

CELLS_PER_CARD = 8
CMD_HEADER_SIZE = 4  # cmd(1) + cell_id(1) + payload_len(2)
RESP_HEADER_SIZE = 3  # status(1) + payload_len(2)

# Per-cell measurement: setpoint_mv(2) + voltage_mv(2) + current_ua(4) + temp_c10(2) + flags(1) = 11 bytes
CELL_MEAS_SIZE = 11
# Health trailer: mcu_temp_c10(2) + vin_mv(2) + uptime_ms(4) = 8 bytes
HEALTH_TRAILER_SIZE = 8
# Full packet: header(13) + 8 cells(88) + health(8) = 109 bytes
MEAS_HEADER_SIZE = 13  # slot_id(1) + seq(4) + timestamp_us(8)
MEAS_PACKET_SIZE = MEAS_HEADER_SIZE + (CELLS_PER_CARD * CELL_MEAS_SIZE) + HEALTH_TRAILER_SIZE


# ── Command Opcodes (match firmware enum cellsim_cmd) ──

class Cmd(IntEnum):
    NOP = 0x00
    SET_VOLTAGE = 0x01
    ENABLE_OUTPUT = 0x02
    SET_MODE = 0x03
    SELF_TEST = 0x10
    GET_STATE = 0x11
    CALIBRATE = 0x12
    HEARTBEAT = 0x20
    SAFE_STATE = 0x21
    RECOVERY = 0x22
    REBOOT = 0xFE


# ── Response Status Codes (match firmware enum cellsim_status) ──

class Status(IntEnum):
    OK = 0x00
    ERR_INVALID = 0x01
    ERR_BUSY = 0x02
    ERR_SAFE = 0x03
    ERR_INTERNAL = 0xFF


# ── Cell Flags (bitfield in measurement packet) ──

class CellFlags(IntEnum):
    OUTPUT_ENABLED = 0x01
    RELAY_CLOSED = 0x02
    LOAD_SWITCH_ON = 0x04
    FOUR_WIRE = 0x08
    SELF_TEST_PASS = 0x10
    OVER_CURRENT = 0x20
    OVER_TEMP = 0x40


# ── Data Structures ──

@dataclass(slots=True)
class CellMeasurement:
    """Single cell measurement from a UDP packet."""
    setpoint_mv: int = 0
    voltage_mv: int = 0
    current_ua: int = 0
    temp_c10: int = 0
    flags: int = 0

    @property
    def setpoint_v(self) -> float:
        return self.setpoint_mv / 1000.0

    @property
    def voltage_v(self) -> float:
        return self.voltage_mv / 1000.0

    @property
    def current_a(self) -> float:
        return self.current_ua / 1_000_000.0

    @property
    def current_ma(self) -> float:
        return self.current_ua / 1000.0

    @property
    def temp_c(self) -> float:
        return self.temp_c10 / 10.0

    @property
    def output_enabled(self) -> bool:
        return bool(self.flags & CellFlags.OUTPUT_ENABLED)

    @property
    def relay_closed(self) -> bool:
        return bool(self.flags & CellFlags.RELAY_CLOSED)

    @property
    def four_wire(self) -> bool:
        return bool(self.flags & CellFlags.FOUR_WIRE)

    @property
    def over_current(self) -> bool:
        return bool(self.flags & CellFlags.OVER_CURRENT)

    @property
    def over_temp(self) -> bool:
        return bool(self.flags & CellFlags.OVER_TEMP)


@dataclass(slots=True)
class MeasurementPacket:
    """Decoded UDP measurement packet from a card."""
    slot_id: int = 0
    sequence: int = 0
    timestamp_us: int = 0
    cells: list[CellMeasurement] | None = None
    mcu_temp_c10: int = 0
    vin_mv: int = 0
    uptime_ms: int = 0

    def __post_init__(self) -> None:
        if self.cells is None:
            self.cells = [CellMeasurement() for _ in range(CELLS_PER_CARD)]

    @property
    def mcu_temp_c(self) -> float:
        return self.mcu_temp_c10 / 10.0

    @property
    def vin_v(self) -> float:
        return self.vin_mv / 1000.0


# ── Encoding Helpers ──

# Struct formats (big-endian)
_CMD_HDR_FMT = ">BBH"  # cmd, cell_id, payload_len
_RESP_HDR_FMT = ">BH"  # status, payload_len
_MEAS_HDR_FMT = ">BIQ"  # slot_id, seq, timestamp_us
_CELL_MEAS_FMT = ">HHIhB"  # setpoint_mv, voltage_mv, current_ua, temp_c10, flags
_HEALTH_FMT = ">hHI"  # mcu_temp_c10, vin_mv, uptime_ms


def encode_command(cmd: Cmd, cell_id: int = 0, payload: bytes = b"") -> bytes:
    """Encode a TCP command frame."""
    header = struct.pack(_CMD_HDR_FMT, int(cmd), cell_id, len(payload))
    return header + payload


def decode_response(data: bytes) -> tuple[Status, bytes]:
    """Decode a TCP response frame. Returns (status, payload)."""
    if len(data) < RESP_HEADER_SIZE:
        raise ValueError(f"Response too short: {len(data)} < {RESP_HEADER_SIZE}")
    status, payload_len = struct.unpack(_RESP_HDR_FMT, data[:RESP_HEADER_SIZE])
    payload = data[RESP_HEADER_SIZE:RESP_HEADER_SIZE + payload_len]
    if len(payload) < payload_len:
        raise ValueError(f"Response payload truncated: {len(payload)} < {payload_len}")
    return Status(status), payload


def decode_measurement(data: bytes) -> MeasurementPacket:
    """Decode a UDP measurement packet from a card."""
    if len(data) < MEAS_PACKET_SIZE:
        raise ValueError(f"Measurement packet too short: {len(data)} < {MEAS_PACKET_SIZE}")

    slot_id, seq, timestamp_us = struct.unpack(_MEAS_HDR_FMT, data[:MEAS_HEADER_SIZE])

    cells: list[CellMeasurement] = []
    offset = MEAS_HEADER_SIZE
    for _ in range(CELLS_PER_CARD):
        sp, v, i, t, f = struct.unpack(_CELL_MEAS_FMT, data[offset:offset + CELL_MEAS_SIZE])
        cells.append(CellMeasurement(
            setpoint_mv=sp, voltage_mv=v, current_ua=i, temp_c10=t, flags=f,
        ))
        offset += CELL_MEAS_SIZE

    mcu_temp, vin, uptime = struct.unpack(_HEALTH_FMT, data[offset:offset + HEALTH_TRAILER_SIZE])

    return MeasurementPacket(
        slot_id=slot_id,
        sequence=seq,
        timestamp_us=timestamp_us,
        cells=cells,
        mcu_temp_c10=mcu_temp,
        vin_mv=vin,
        uptime_ms=uptime,
    )


def encode_set_voltage(millivolts: int) -> bytes:
    """Encode SET_VOLTAGE payload: [setpoint_mv:u16]."""
    return struct.pack(">H", millivolts)


def encode_enable_output(enable: bool) -> bytes:
    """Encode ENABLE_OUTPUT payload: [enable:u8]."""
    return struct.pack(">B", 1 if enable else 0)


def encode_set_mode(four_wire: bool) -> bytes:
    """Encode SET_MODE payload: [mode:u8] (1=4-wire, 0=2-wire)."""
    return struct.pack(">B", 1 if four_wire else 0)
