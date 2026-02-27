"""
CellSim v2 — UART Flasher (STM32 ROM Bootloader Client)

Implements the AN3155 protocol (USART bootloader) to flash firmware to
STM32 cards via the backplane slot mux.

Protocol flow:
    1. Auto-baud: send 0x7F, wait for ACK (0x79)
    2. Get command: verify bootloader version and supported commands
    3. Erase: mass erase or page erase
    4. Write: 256-byte aligned chunks with checksum
    5. Verify: optional readback verification
    6. Go: jump to application at start address

Reference: ST AN3155 "USART protocol used in the STM32 bootloader"
"""

from __future__ import annotations

import logging
import struct
import time
from dataclasses import dataclass

logger = logging.getLogger(__name__)

# AN3155 protocol constants
SYNC_BYTE = 0x7F
ACK = 0x79
NACK = 0x1F

# Bootloader commands
CMD_GET = 0x00
CMD_GET_VERSION = 0x01
CMD_GET_ID = 0x02
CMD_READ_MEMORY = 0x11
CMD_GO = 0x21
CMD_WRITE_MEMORY = 0x31
CMD_ERASE = 0x43
CMD_EXTENDED_ERASE = 0x44

# STM32H7 flash start address
FLASH_START = 0x08000000

# Write chunk size (AN3155 maximum)
WRITE_CHUNK_SIZE = 256

# Timeout for ACK/NACK responses (seconds)
RESPONSE_TIMEOUT = 5.0


@dataclass
class FlashProgress:
    """Progress of a flash operation."""
    stage: str = ""        # "erasing", "writing", "verifying", "done", "error"
    bytes_written: int = 0
    bytes_total: int = 0
    percent: float = 0.0
    error: str | None = None


class UartFlasher:
    """STM32 USART bootloader client (AN3155 protocol)."""

    def __init__(self, port: str, baudrate: int = 115200):
        """
        Initialize the flasher.

        Args:
            port: Serial port device (e.g., '/dev/ttyAMA1')
            baudrate: Baud rate (default 115200)
        """
        self._port = port
        self._baudrate = baudrate
        self._serial = None
        self._connected = False

    def open(self) -> None:
        """Open the serial port."""
        import serial
        self._serial = serial.Serial(
            port=self._port,
            baudrate=self._baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_EVEN,  # AN3155 requires even parity
            stopbits=serial.STOPBITS_ONE,
            timeout=RESPONSE_TIMEOUT,
        )
        self._serial.reset_input_buffer()
        logger.info("Opened serial port %s at %d baud", self._port, self._baudrate)

    def close(self) -> None:
        """Close the serial port."""
        if self._serial is not None:
            self._serial.close()
            self._serial = None
        self._connected = False

    def connect(self) -> bool:
        """
        Establish connection with the bootloader (auto-baud synchronization).
        Send 0x7F and wait for ACK.

        Returns:
            True if connected successfully
        """
        logger.info("Connecting to bootloader (auto-baud sync)...")

        for attempt in range(5):
            self._serial.reset_input_buffer()
            self._serial.write(bytes([SYNC_BYTE]))
            self._serial.flush()

            resp = self._serial.read(1)
            if resp and resp[0] == ACK:
                self._connected = True
                logger.info("Bootloader connected (attempt %d)", attempt + 1)
                return True
            elif resp and resp[0] == NACK:
                logger.debug("Got NACK on attempt %d, retrying", attempt + 1)
            else:
                logger.debug("No response on attempt %d", attempt + 1)
            time.sleep(0.1)

        logger.error("Failed to connect to bootloader after 5 attempts")
        return False

    def get_version(self) -> tuple[int, int] | None:
        """
        Get bootloader version.

        Returns:
            (version, option_bytes) tuple, or None on failure
        """
        if not self._send_command(CMD_GET_VERSION):
            return None

        data = self._serial.read(3)  # version, option1, option2
        ack = self._serial.read(1)

        if len(data) < 3 or not ack or ack[0] != ACK:
            return None

        version = data[0]
        logger.info("Bootloader version: %d.%d", version >> 4, version & 0x0F)
        return (data[0], data[1])

    def get_id(self) -> int | None:
        """
        Get chip ID (PID).

        Returns:
            Product ID, or None on failure
        """
        if not self._send_command(CMD_GET_ID):
            return None

        n = self._serial.read(1)
        if not n:
            return None
        count = n[0] + 1
        data = self._serial.read(count)
        ack = self._serial.read(1)

        if len(data) < count or not ack or ack[0] != ACK:
            return None

        pid = int.from_bytes(data[:2], "big")
        logger.info("Chip PID: 0x%04X", pid)
        return pid

    def mass_erase(self) -> bool:
        """
        Erase entire flash memory.

        Returns:
            True on success
        """
        logger.info("Mass erasing flash...")

        # Try extended erase first (STM32H7 uses this)
        if self._send_command(CMD_EXTENDED_ERASE):
            # Mass erase: send 0xFFFF + checksum
            data = bytes([0xFF, 0xFF, 0x00])  # Special erase = mass, checksum
            self._serial.write(data)
            self._serial.flush()

            # Mass erase can take several seconds
            old_timeout = self._serial.timeout
            self._serial.timeout = 30.0
            ack = self._serial.read(1)
            self._serial.timeout = old_timeout

            if ack and ack[0] == ACK:
                logger.info("Mass erase complete")
                return True

        # Fallback: legacy erase command
        if self._send_command(CMD_ERASE):
            self._serial.write(bytes([0xFF, 0x00]))  # Global erase + checksum
            self._serial.flush()

            old_timeout = self._serial.timeout
            self._serial.timeout = 30.0
            ack = self._serial.read(1)
            self._serial.timeout = old_timeout

            if ack and ack[0] == ACK:
                logger.info("Mass erase complete (legacy)")
                return True

        logger.error("Mass erase failed")
        return False

    def write_memory(self, address: int, data: bytes,
                     progress_cb=None) -> bool:
        """
        Write data to flash memory in 256-byte chunks.

        Args:
            address: Start address (must be word-aligned)
            data: Firmware binary data
            progress_cb: Optional callback(FlashProgress) for progress updates

        Returns:
            True on success
        """
        total = len(data)
        written = 0

        logger.info("Writing %d bytes to 0x%08X...", total, address)

        # Pad to 256-byte boundary
        if total % WRITE_CHUNK_SIZE != 0:
            pad_len = WRITE_CHUNK_SIZE - (total % WRITE_CHUNK_SIZE)
            data = data + b'\xFF' * pad_len

        for offset in range(0, len(data), WRITE_CHUNK_SIZE):
            chunk = data[offset:offset + WRITE_CHUNK_SIZE]
            addr = address + offset

            if not self._write_chunk(addr, chunk):
                logger.error("Write failed at 0x%08X", addr)
                if progress_cb:
                    progress_cb(FlashProgress(
                        stage="error", bytes_written=written, bytes_total=total,
                        error=f"Write failed at 0x{addr:08X}"
                    ))
                return False

            written = min(offset + WRITE_CHUNK_SIZE, total)
            if progress_cb:
                progress_cb(FlashProgress(
                    stage="writing",
                    bytes_written=written,
                    bytes_total=total,
                    percent=(written / total) * 100,
                ))

        logger.info("Write complete: %d bytes", total)
        return True

    def verify_memory(self, address: int, data: bytes,
                      progress_cb=None) -> bool:
        """
        Verify flash memory contents by reading back and comparing.

        Args:
            address: Start address
            data: Expected data
            progress_cb: Optional callback(FlashProgress)

        Returns:
            True if verification passes
        """
        total = len(data)
        logger.info("Verifying %d bytes at 0x%08X...", total, address)

        for offset in range(0, total, WRITE_CHUNK_SIZE):
            chunk_len = min(WRITE_CHUNK_SIZE, total - offset)
            addr = address + offset

            readback = self._read_chunk(addr, chunk_len)
            if readback is None:
                logger.error("Read failed at 0x%08X", addr)
                return False

            expected = data[offset:offset + chunk_len]
            if readback != expected:
                logger.error("Verify mismatch at 0x%08X", addr)
                return False

            if progress_cb:
                verified = offset + chunk_len
                progress_cb(FlashProgress(
                    stage="verifying",
                    bytes_written=verified,
                    bytes_total=total,
                    percent=(verified / total) * 100,
                ))

        logger.info("Verification passed")
        return True

    def go(self, address: int = FLASH_START) -> bool:
        """
        Jump to application at the given address.

        Args:
            address: Application start address (default: flash start)

        Returns:
            True on success
        """
        logger.info("Jumping to 0x%08X...", address)

        if not self._send_command(CMD_GO):
            return False

        # Send address + checksum
        addr_bytes = struct.pack(">I", address)
        checksum = self._xor_checksum(addr_bytes)
        self._serial.write(addr_bytes + bytes([checksum]))
        self._serial.flush()

        ack = self._serial.read(1)
        if ack and ack[0] == ACK:
            logger.info("Go command accepted, application starting")
            self._connected = False
            return True

        logger.error("Go command failed")
        return False

    def flash_firmware(self, firmware_path: str,
                       address: int = FLASH_START,
                       verify: bool = True,
                       progress_cb=None) -> bool:
        """
        Full flash sequence: erase, write, verify, go.

        Args:
            firmware_path: Path to .bin firmware file
            address: Flash start address
            verify: Whether to verify after writing
            progress_cb: Optional callback(FlashProgress)

        Returns:
            True if firmware was flashed successfully
        """
        with open(firmware_path, "rb") as f:
            firmware = f.read()

        logger.info("Flashing %s (%d bytes)", firmware_path, len(firmware))

        # Erase
        if progress_cb:
            progress_cb(FlashProgress(stage="erasing", bytes_total=len(firmware)))
        if not self.mass_erase():
            if progress_cb:
                progress_cb(FlashProgress(stage="error", error="Erase failed"))
            return False

        # Write
        if not self.write_memory(address, firmware, progress_cb):
            return False

        # Verify
        if verify:
            if not self.verify_memory(address, firmware, progress_cb):
                if progress_cb:
                    progress_cb(FlashProgress(stage="error", error="Verification failed"))
                return False

        # Go
        if not self.go(address):
            if progress_cb:
                progress_cb(FlashProgress(stage="error", error="Go command failed"))
            return False

        if progress_cb:
            progress_cb(FlashProgress(
                stage="done",
                bytes_written=len(firmware),
                bytes_total=len(firmware),
                percent=100.0,
            ))

        logger.info("Firmware flash complete")
        return True

    # --- Private helpers ---

    def _send_command(self, cmd: int) -> bool:
        """Send a command byte with its complement, wait for ACK."""
        self._serial.write(bytes([cmd, cmd ^ 0xFF]))
        self._serial.flush()

        ack = self._serial.read(1)
        if ack and ack[0] == ACK:
            return True
        if ack and ack[0] == NACK:
            logger.debug("Command 0x%02X NACKed", cmd)
        else:
            logger.debug("Command 0x%02X: no response", cmd)
        return False

    def _write_chunk(self, address: int, data: bytes) -> bool:
        """Write a single chunk (up to 256 bytes) at the given address."""
        if not self._send_command(CMD_WRITE_MEMORY):
            return False

        # Send address + checksum
        addr_bytes = struct.pack(">I", address)
        checksum = self._xor_checksum(addr_bytes)
        self._serial.write(addr_bytes + bytes([checksum]))
        self._serial.flush()

        ack = self._serial.read(1)
        if not ack or ack[0] != ACK:
            return False

        # Send data: N-1 (count byte), data bytes, checksum
        n = len(data) - 1
        payload = bytes([n]) + data
        checksum = self._xor_checksum(payload)
        self._serial.write(payload + bytes([checksum]))
        self._serial.flush()

        ack = self._serial.read(1)
        return bool(ack and ack[0] == ACK)

    def _read_chunk(self, address: int, length: int) -> bytes | None:
        """Read a chunk of memory."""
        if not self._send_command(CMD_READ_MEMORY):
            return None

        # Send address + checksum
        addr_bytes = struct.pack(">I", address)
        checksum = self._xor_checksum(addr_bytes)
        self._serial.write(addr_bytes + bytes([checksum]))
        self._serial.flush()

        ack = self._serial.read(1)
        if not ack or ack[0] != ACK:
            return None

        # Send count (N-1) + checksum
        n = length - 1
        self._serial.write(bytes([n, n ^ 0xFF]))
        self._serial.flush()

        ack = self._serial.read(1)
        if not ack or ack[0] != ACK:
            return None

        data = self._serial.read(length)
        if len(data) != length:
            return None

        return data

    @staticmethod
    def _xor_checksum(data: bytes) -> int:
        """Compute XOR checksum over all bytes."""
        result = 0
        for b in data:
            result ^= b
        return result
