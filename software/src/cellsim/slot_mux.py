"""
CellSim v2 — Slot Mux Controller

Controls the CD74HC4067SM analog multiplexers to route UART, NRST, and BOOT0
signals from the CM5 to a selected card slot. Also drives the NRST and BOOT0
lines via CM5 GPIO pins.

Hardware:
    8x CD74HC4067SM: 16:1 analog muxes (2 banks × 4 signals)
    MCP23017 #1: provides mux select lines (port A[4:7] = sel[0:3], port B[0] = bank)
    CM5 GPIO: NRST_DRIVE (active low), BOOT0_DRIVE (active high)

Select Logic:
    sel[3:0] from MCP23017 #1 port A bits 4-7
    bank_sel from MCP23017 #1 port B bit 0
    bank_sel=0 -> slots 0-15 (16-channel mux)
    bank_sel=1 -> slots 16-19 (4 used of 16 channels)

Safety:
    Non-selected slots have BOOT0 pulled low (card-side) and NRST pulled high
    (card-side) via onboard resistors, so only the actively selected slot is
    affected by mux operations.
"""

from __future__ import annotations

import logging
import time

logger = logging.getLogger(__name__)

# MCP23017 #1 address and registers
ADDR_GPIO_AUX = 0x21
MCP23017_OLATA = 0x14
MCP23017_OLATB = 0x15

# CM5 GPIO pin numbers (BCM numbering)
# TODO: assign actual CM5 GPIO pins once schematic is finalized
GPIO_NRST_DRIVE = 17   # Active low: pull low to reset card
GPIO_BOOT0_DRIVE = 27  # Active high: drive high for bootloader entry

NUM_SLOTS = 16


class SlotMux:
    """Controls the slot mux to route signals to a selected card."""

    def __init__(self, i2c_bus=None):
        """
        Initialize the slot mux controller.

        Args:
            i2c_bus: An smbus2.SMBus instance (shared with SlotPowerManager)
        """
        self._bus = i2c_bus
        self._gpio = None
        self._selected_slot: int | None = None
        self._initialized = False

    def init(self, i2c_bus=None) -> None:
        """Initialize GPIO and I2C for mux control."""
        if i2c_bus is not None:
            self._bus = i2c_bus

        # Initialize CM5 GPIO for NRST and BOOT0 drive
        try:
            import RPi.GPIO as GPIO
            GPIO.setmode(GPIO.BCM)
            GPIO.setup(GPIO_NRST_DRIVE, GPIO.OUT, initial=GPIO.HIGH)  # Not in reset
            GPIO.setup(GPIO_BOOT0_DRIVE, GPIO.OUT, initial=GPIO.LOW)  # Normal boot
            self._gpio = GPIO
        except (ImportError, RuntimeError):
            logger.warning("RPi.GPIO not available, using mock GPIO")
            self._gpio = _MockGPIO()

        self._initialized = True
        logger.info("SlotMux initialized")

    def select_slot(self, slot_id: int) -> None:
        """
        Select a slot for UART/NRST/BOOT0 routing.

        Args:
            slot_id: 0-19
        """
        if not 0 <= slot_id < NUM_SLOTS:
            raise ValueError(f"Invalid slot ID: {slot_id}")

        bank = 0
        channel = slot_id

        # Write select lines to MCP23017 #1
        # Port A[4:7] = sel[3:0], preserve port A[0:3] (power enable 16-19)
        current_a = self._bus.read_byte_data(ADDR_GPIO_AUX, MCP23017_OLATA)
        new_a = (current_a & 0x0F) | ((channel & 0x0F) << 4)
        self._bus.write_byte_data(ADDR_GPIO_AUX, MCP23017_OLATA, new_a)

        # Port B[0] = bank select, preserve other bits
        current_b = self._bus.read_byte_data(ADDR_GPIO_AUX, MCP23017_OLATB)
        new_b = (current_b & 0xFE) | (bank & 0x01)
        self._bus.write_byte_data(ADDR_GPIO_AUX, MCP23017_OLATB, new_b)

        self._selected_slot = slot_id
        logger.debug("Mux selected slot %d (bank=%d, channel=%d)", slot_id, bank, channel)

    def deselect(self) -> None:
        """Deselect all slots (safe state for NRST/BOOT0)."""
        # Ensure NRST and BOOT0 are in safe state
        self._gpio.output(GPIO_NRST_DRIVE, 1)   # Not in reset (high)
        self._gpio.output(GPIO_BOOT0_DRIVE, 0)  # Normal boot (low)
        self._selected_slot = None

    def reset_card(self, slot_id: int, duration_ms: int = 100) -> None:
        """
        Reset a card by pulsing its NRST line low.

        Args:
            slot_id: Which slot to reset
            duration_ms: How long to hold NRST low
        """
        self.select_slot(slot_id)
        logger.info("Resetting slot %d (NRST low for %d ms)", slot_id, duration_ms)

        self._gpio.output(GPIO_NRST_DRIVE, 0)  # Assert reset
        time.sleep(duration_ms / 1000.0)
        self._gpio.output(GPIO_NRST_DRIVE, 1)  # Release reset
        time.sleep(0.050)  # 50ms for MCU to start

    def enter_bootloader(self, slot_id: int) -> None:
        """
        Put a card into STM32 ROM bootloader mode.
        Drives BOOT0 high, then pulses NRST to restart into bootloader.

        Args:
            slot_id: Which slot to put in bootloader mode
        """
        self.select_slot(slot_id)
        logger.info("Entering bootloader on slot %d", slot_id)

        # Drive BOOT0 high (bootloader entry)
        self._gpio.output(GPIO_BOOT0_DRIVE, 1)
        time.sleep(0.010)  # 10ms for signal to settle through mux

        # Pulse NRST to restart MCU (will enter bootloader due to BOOT0=1)
        self._gpio.output(GPIO_NRST_DRIVE, 0)
        time.sleep(0.100)  # 100ms reset pulse
        self._gpio.output(GPIO_NRST_DRIVE, 1)

        # Wait for bootloader to initialize
        time.sleep(0.200)
        logger.info("Slot %d should now be in bootloader mode", slot_id)

    def exit_bootloader(self, slot_id: int) -> None:
        """
        Exit bootloader mode: drive BOOT0 low, then reset to boot normally.

        Args:
            slot_id: Which slot to exit bootloader
        """
        self.select_slot(slot_id)
        logger.info("Exiting bootloader on slot %d", slot_id)

        # Drive BOOT0 low (normal boot)
        self._gpio.output(GPIO_BOOT0_DRIVE, 0)
        time.sleep(0.010)

        # Reset to boot into firmware
        self._gpio.output(GPIO_NRST_DRIVE, 0)
        time.sleep(0.100)
        self._gpio.output(GPIO_NRST_DRIVE, 1)
        time.sleep(0.050)

    def get_uart_device(self) -> str:
        """
        Get the serial device path for the muxed UART.
        The CM5's UART connected to the slot mux.

        Returns:
            Serial device path (e.g., '/dev/ttyAMA1')
        """
        # TODO: determine actual CM5 UART device for slot mux
        return "/dev/ttyAMA1"

    def close(self) -> None:
        """Cleanup GPIO."""
        self.deselect()
        if self._gpio is not None and hasattr(self._gpio, "cleanup"):
            self._gpio.cleanup()


class _MockGPIO:
    """Mock GPIO for development without hardware."""

    HIGH = 1
    LOW = 0
    OUT = 0
    BCM = 11

    def setmode(self, mode):
        pass

    def setup(self, pin, direction, initial=None):
        pass

    def output(self, pin, value):
        logger.debug("Mock GPIO %d = %d", pin, value)

    def cleanup(self):
        pass
