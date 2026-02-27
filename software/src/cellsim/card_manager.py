"""
CellSim v2 — Card Manager

Discovers and manages cards on the internal Ethernet network.

Responsibilities:
  - mDNS discovery via zeroconf (cards advertise as _cellsim._tcp.local.)
  - Per-card TCP command channel (persistent connections with reconnect)
  - UDP 100 Hz measurement stream decoding
  - Health tracking (heartbeats, online/offline transitions)
"""

from __future__ import annotations

import asyncio
import logging
import struct
import time
from dataclasses import dataclass, field
from typing import Callable

from cellsim.protocol import (
    CELLS_PER_CARD,
    TCP_CMD_PORT,
    UDP_MEAS_PORT,
    Cmd,
    MeasurementPacket,
    Status,
    decode_measurement,
    decode_response,
    encode_command,
)

logger = logging.getLogger(__name__)

# Timing
HEARTBEAT_INTERVAL_S = 2.0
OFFLINE_TIMEOUT_S = 10.0
RECONNECT_DELAY_S = 1.0
MDNS_SERVICE = "_cellsim._tcp.local."


@dataclass
class CardConnection:
    """State for a connected card."""
    slot_id: int = 0
    card_type: str = "cell"
    ip_address: str = ""
    firmware_version: str = "unknown"
    card_id: str = ""
    online: bool = False
    last_seen: float = 0.0
    _reader: asyncio.StreamReader | None = field(default=None, repr=False)
    _writer: asyncio.StreamWriter | None = field(default=None, repr=False)
    _lock: asyncio.Lock = field(default_factory=asyncio.Lock, repr=False)


class _MeasurementProtocol(asyncio.DatagramProtocol):
    """Async UDP protocol for receiving measurement packets."""

    def __init__(self, manager: CardManager):
        self._manager = manager
        self.transport: asyncio.DatagramTransport | None = None

    def connection_made(self, transport: asyncio.DatagramTransport) -> None:
        self.transport = transport

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            pkt = decode_measurement(data)
        except ValueError:
            return  # malformed packet

        self._manager._on_measurement(pkt)


class CardManager:
    """Discovers and manages all cards on the internal network."""

    def __init__(self, internal_interface: str = "eth1"):
        self.interface = internal_interface
        self.cards: dict[int, CardConnection] = {}
        self.measurements: dict[int, MeasurementPacket] = {}

        self._on_measurement_cbs: list[Callable[[MeasurementPacket], None]] = []
        self._discovery_task: asyncio.Task | None = None
        self._heartbeat_task: asyncio.Task | None = None
        self._meas_transport: asyncio.DatagramTransport | None = None
        self._zc = None  # zeroconf.Zeroconf instance

    # ── Lifecycle ──

    async def start_discovery(self) -> None:
        """Start mDNS service browser for card advertisements."""
        try:
            from zeroconf import ServiceStateChange, Zeroconf
            from zeroconf.asyncio import AsyncServiceBrowser, AsyncZeroconf

            self._azc = AsyncZeroconf(interfaces=[self.interface])
            self._browser = AsyncServiceBrowser(
                self._azc.zeroconf,
                MDNS_SERVICE,
                handlers=[self._on_service_change],
            )
            logger.info("mDNS discovery started on %s", self.interface)
        except Exception:
            logger.warning("zeroconf unavailable, falling back to IP scan")
            self._discovery_task = asyncio.create_task(self._scan_loop())

        self._heartbeat_task = asyncio.create_task(self._heartbeat_loop())

    async def start_measurement_listener(self) -> None:
        """Start async UDP listener for 100 Hz measurement streams."""
        loop = asyncio.get_running_loop()
        transport, _ = await loop.create_datagram_endpoint(
            lambda: _MeasurementProtocol(self),
            local_addr=("0.0.0.0", UDP_MEAS_PORT),
            reuse_port=True,
        )
        self._meas_transport = transport
        logger.info("UDP measurement listener on port %d", UDP_MEAS_PORT)

    async def stop(self) -> None:
        """Stop all background tasks and close connections."""
        if self._discovery_task:
            self._discovery_task.cancel()
        if self._heartbeat_task:
            self._heartbeat_task.cancel()
        if self._meas_transport:
            self._meas_transport.close()

        # Close zeroconf
        azc = getattr(self, "_azc", None)
        if azc:
            await azc.async_close()

        # Close all TCP connections
        for card in self.cards.values():
            await self._disconnect(card)

        logger.info("CardManager stopped")

    # ── Command API ──

    async def send_command(
        self,
        slot_id: int,
        cmd: Cmd,
        cell_id: int = 0,
        payload: bytes = b"",
        timeout: float = 5.0,
    ) -> tuple[Status, bytes]:
        """
        Send a command to a card over TCP (persistent connection).

        Returns (status, response_payload).
        Raises ConnectionError if the card is unreachable.
        """
        card = self.cards.get(slot_id)
        if card is None:
            raise ValueError(f"Card {slot_id} not registered")

        async with card._lock:
            # Ensure connected
            if card._writer is None or card._writer.is_closing():
                await self._connect(card)

            frame = encode_command(cmd, cell_id, payload)

            try:
                card._writer.write(frame)
                await card._writer.drain()

                # Read response: [status:u8][payload_len:u16][payload]
                resp_hdr = await asyncio.wait_for(
                    card._reader.readexactly(3), timeout=timeout
                )
                status_byte = resp_hdr[0]
                resp_len = struct.unpack(">H", resp_hdr[1:3])[0]

                resp_payload = b""
                if resp_len > 0:
                    resp_payload = await asyncio.wait_for(
                        card._reader.readexactly(resp_len), timeout=timeout
                    )

                card.last_seen = time.monotonic()
                card.online = True
                return Status(status_byte), resp_payload

            except (asyncio.TimeoutError, ConnectionError, OSError) as e:
                logger.warning("Command to slot %d failed: %s", slot_id, e)
                await self._disconnect(card)
                card.online = False
                raise ConnectionError(f"Card {slot_id}: {e}") from e

    async def send_heartbeat(self, slot_id: int) -> bool:
        """Send heartbeat. Returns True if card responds OK."""
        try:
            status, _ = await self.send_command(slot_id, Cmd.HEARTBEAT, timeout=2.0)
            return status == Status.OK
        except (ConnectionError, ValueError):
            return False

    # ── Measurement callbacks ──

    def on_measurement(self, cb: Callable[[MeasurementPacket], None]) -> None:
        """Register a callback for incoming measurement packets."""
        self._on_measurement_cbs.append(cb)

    def _on_measurement(self, pkt: MeasurementPacket) -> None:
        """Called by the UDP protocol when a packet arrives."""
        self.measurements[pkt.slot_id] = pkt

        # Update card liveness
        card = self.cards.get(pkt.slot_id)
        if card:
            card.last_seen = time.monotonic()
            card.online = True

        for cb in self._on_measurement_cbs:
            try:
                cb(pkt)
            except Exception:
                logger.exception("Measurement callback error")

    # ── mDNS callbacks ──

    def _on_service_change(self, zeroconf, service_type, name, state_change) -> None:
        """Called by zeroconf when a card service is added/removed."""
        from zeroconf import ServiceStateChange

        if state_change == ServiceStateChange.Added:
            asyncio.get_event_loop().create_task(
                self._resolve_service(zeroconf, service_type, name)
            )
        elif state_change == ServiceStateChange.Removed:
            # Extract slot_id from service name "cellsim-card-{slot_id}"
            try:
                slot_id = int(name.split("-")[-1].split(".")[0])
                if slot_id in self.cards:
                    self.cards[slot_id].online = False
                    logger.info("Card slot %d went offline (mDNS removed)", slot_id)
            except (ValueError, IndexError):
                pass

    async def _resolve_service(self, zeroconf, service_type, name) -> None:
        """Resolve a discovered mDNS service to get IP and metadata."""
        from zeroconf import Zeroconf

        info = zeroconf.get_service_info(service_type, name, timeout=3000)
        if info is None:
            return

        # Parse slot_id from name: "cellsim-card-{slot_id}._cellsim._tcp.local."
        try:
            hostname = name.split(".")[0]  # cellsim-card-N
            slot_id = int(hostname.split("-")[-1])
        except (ValueError, IndexError):
            logger.warning("Cannot parse slot_id from mDNS name: %s", name)
            return

        ip = info.parsed_addresses()[0] if info.parsed_addresses() else None
        if ip is None:
            return

        # Read TXT records
        props = {k.decode(): v.decode() if v else "" for k, v in info.properties.items()}

        card = self.cards.get(slot_id)
        if card is None:
            card = CardConnection(slot_id=slot_id)
            self.cards[slot_id] = card

        card.ip_address = ip
        card.card_type = props.get("type", "cell")
        card.firmware_version = props.get("fw", "unknown")
        card.card_id = props.get("id", "")
        card.online = True
        card.last_seen = time.monotonic()

        logger.info(
            "Discovered card slot %d at %s (type=%s, fw=%s)",
            slot_id, ip, card.card_type, card.firmware_version,
        )

    # ── Private ──

    async def _connect(self, card: CardConnection) -> None:
        """Open a persistent TCP connection to a card."""
        if not card.ip_address:
            raise ConnectionError(f"Card {card.slot_id}: no IP address")

        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(card.ip_address, TCP_CMD_PORT),
            timeout=5.0,
        )
        card._reader = reader
        card._writer = writer
        logger.debug("TCP connected to slot %d (%s)", card.slot_id, card.ip_address)

    async def _disconnect(self, card: CardConnection) -> None:
        """Close TCP connection to a card."""
        if card._writer and not card._writer.is_closing():
            card._writer.close()
            try:
                await card._writer.wait_closed()
            except Exception:
                pass
        card._reader = None
        card._writer = None

    async def _heartbeat_loop(self) -> None:
        """Send periodic heartbeats and mark cards offline if unresponsive."""
        while True:
            try:
                now = time.monotonic()
                for slot_id, card in list(self.cards.items()):
                    if not card.online:
                        continue

                    # Check for staleness
                    if now - card.last_seen > OFFLINE_TIMEOUT_S:
                        card.online = False
                        await self._disconnect(card)
                        logger.warning("Card slot %d timed out", slot_id)
                        continue

                    # Send heartbeat
                    ok = await self.send_heartbeat(slot_id)
                    if not ok:
                        logger.warning("Card slot %d heartbeat failed", slot_id)

            except asyncio.CancelledError:
                return
            except Exception:
                logger.exception("Heartbeat loop error")

            await asyncio.sleep(HEARTBEAT_INTERVAL_S)

    async def _scan_loop(self) -> None:
        """Fallback: periodically scan 10.0.0.100-115 for cards."""
        while True:
            try:
                for slot_id in range(16):
                    ip = f"10.0.0.{100 + slot_id}"

                    try:
                        reader, writer = await asyncio.wait_for(
                            asyncio.open_connection(ip, TCP_CMD_PORT),
                            timeout=0.3,
                        )
                        writer.close()
                        await writer.wait_closed()

                        if slot_id not in self.cards:
                            self.cards[slot_id] = CardConnection(
                                slot_id=slot_id,
                                ip_address=ip,
                                online=True,
                                last_seen=time.monotonic(),
                            )
                            logger.info("Scan found card at slot %d (%s)", slot_id, ip)
                        else:
                            self.cards[slot_id].online = True
                            self.cards[slot_id].last_seen = time.monotonic()

                    except (asyncio.TimeoutError, ConnectionRefusedError, OSError):
                        if slot_id in self.cards:
                            self.cards[slot_id].online = False

            except asyncio.CancelledError:
                return
            except Exception:
                logger.exception("Scan loop error")

            await asyncio.sleep(5.0)
