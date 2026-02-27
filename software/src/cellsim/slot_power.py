"""
CellSim v2 — Slot Power Manager

I2C drivers for per-slot power control and current monitoring on the CM5.

Hardware:
    MCP23017 x3: GPIO expanders for slot power enable, mux select, card-present
    INA3221 x7:  Per-slot current monitors (behind TCA9548A)
    INA228 x1:   Main 24V bus voltage + current monitor

I2C Address Map:
    0x20  MCP23017 #0  SLOT_POWER_EN[0:15]
    0x21  MCP23017 #1  SLOT_POWER_EN[16:19], MUX_SEL[0:4], E_STOP
    0x22  MCP23017 #2  CARD_PRESENT[0:15]
    0x45  INA228       Main 24V bus V+I
    0x70  TCA9548A     I2C mux for INA3221
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)

# MCP23017 register addresses
MCP23017_IODIRA = 0x00
MCP23017_IODIRB = 0x01
MCP23017_GPIOA = 0x12
MCP23017_GPIOB = 0x13
MCP23017_OLATA = 0x14
MCP23017_OLATB = 0x15

# I2C addresses
ADDR_GPIO_POWER = 0x20    # MCP23017 #0: slot power enable [0:15]
ADDR_GPIO_AUX = 0x21      # MCP23017 #1: power [16:19], mux sel, e-stop
ADDR_GPIO_DETECT = 0x22   # MCP23017 #2: card present [0:15]
ADDR_INA228 = 0x45        # Main 24V bus monitor
ADDR_TCA9548A = 0x70      # I2C mux for INA3221 bank access

# INA3221 addresses (same on both mux channels, disambiguated by TCA9548A)
INA3221_ADDRS = [0x40, 0x41, 0x42, 0x43]

# INA228 register addresses
INA228_VBUS = 0x05
INA228_CURRENT = 0x07
INA228_POWER = 0x08

# INA3221 register addresses
INA3221_CH1_SHUNT = 0x01
INA3221_CH1_BUS = 0x02
INA3221_CH2_SHUNT = 0x03
INA3221_CH2_BUS = 0x04
INA3221_CH3_SHUNT = 0x05
INA3221_CH3_BUS = 0x06

NUM_SLOTS = 16


@dataclass
class SlotStatus:
    """Status of a single card slot."""
    slot_id: int
    power_enabled: bool = False
    card_present: bool = False
    bus_voltage_v: float = 0.0
    current_a: float = 0.0


@dataclass
class BusStatus:
    """Status of the main 24V bus."""
    voltage_v: float = 0.0
    current_a: float = 0.0
    power_w: float = 0.0


class SlotPowerManager:
    """Controls per-slot power switches and reads current monitors."""

    def __init__(self, i2c_bus: int = 1):
        """
        Initialize the slot power manager.

        Args:
            i2c_bus: Linux I2C bus number for CM5 I2C (default: 1)
        """
        self._bus_num = i2c_bus
        self._bus = None
        self._power_state: int = 0  # 16-bit bitmask of enabled slots
        self._initialized = False

    def init(self) -> None:
        """Initialize I2C bus and configure GPIO expanders."""
        try:
            import smbus2
            self._bus = smbus2.SMBus(self._bus_num)
        except ImportError:
            logger.warning("smbus2 not available, using mock I2C")
            self._bus = _MockSMBus()
        except FileNotFoundError:
            logger.warning("I2C bus %d not found, using mock I2C", self._bus_num)
            self._bus = _MockSMBus()

        # Configure MCP23017 #0: all outputs (slot power enable 0-15)
        self._bus.write_byte_data(ADDR_GPIO_POWER, MCP23017_IODIRA, 0x00)
        self._bus.write_byte_data(ADDR_GPIO_POWER, MCP23017_IODIRB, 0x00)
        # Start with all slots powered off
        self._bus.write_byte_data(ADDR_GPIO_POWER, MCP23017_OLATA, 0x00)
        self._bus.write_byte_data(ADDR_GPIO_POWER, MCP23017_OLATB, 0x00)

        # Configure MCP23017 #1: mixed I/O
        # Port A[0:3]=output (power 16-19), A[4:7]=output (mux sel 0-3)
        # Port B[0]=output (mux bank sel), B[1]=input (e-stop), B[2:7]=spare input
        self._bus.write_byte_data(ADDR_GPIO_AUX, MCP23017_IODIRA, 0x00)  # all output
        self._bus.write_byte_data(ADDR_GPIO_AUX, MCP23017_IODIRB, 0xFE)  # B0=out, rest=in
        self._bus.write_byte_data(ADDR_GPIO_AUX, MCP23017_OLATA, 0x00)
        self._bus.write_byte_data(ADDR_GPIO_AUX, MCP23017_OLATB, 0x00)

        # Configure MCP23017 #2: all inputs (card present 0-15)
        self._bus.write_byte_data(ADDR_GPIO_DETECT, MCP23017_IODIRA, 0xFF)
        self._bus.write_byte_data(ADDR_GPIO_DETECT, MCP23017_IODIRB, 0xFF)

        self._initialized = True
        logger.info("SlotPowerManager initialized on I2C bus %d", self._bus_num)

    def close(self) -> None:
        """Release I2C bus."""
        if self._bus is not None:
            self._bus.close()
            self._bus = None

    def enable_slot(self, slot_id: int) -> None:
        """Enable power to a slot."""
        if not 0 <= slot_id < NUM_SLOTS:
            raise ValueError(f"Invalid slot ID: {slot_id}")

        self._power_state |= (1 << slot_id)
        self._write_power_state()
        logger.info("Slot %d power ENABLED", slot_id)

    def disable_slot(self, slot_id: int) -> None:
        """Disable power to a slot."""
        if not 0 <= slot_id < NUM_SLOTS:
            raise ValueError(f"Invalid slot ID: {slot_id}")

        self._power_state &= ~(1 << slot_id)
        self._write_power_state()
        logger.info("Slot %d power DISABLED", slot_id)

    def disable_all(self) -> None:
        """Disable power to all slots (emergency stop)."""
        self._power_state = 0
        self._write_power_state()
        logger.warning("ALL slots power DISABLED")

    def is_slot_enabled(self, slot_id: int) -> bool:
        """Check if a slot's power is enabled."""
        return bool(self._power_state & (1 << slot_id))

    def get_card_present(self) -> list[bool]:
        """Read card-present status for all slots."""
        port_a = self._bus.read_byte_data(ADDR_GPIO_DETECT, MCP23017_GPIOA)
        port_b = self._bus.read_byte_data(ADDR_GPIO_DETECT, MCP23017_GPIOB)
        present = port_a | (port_b << 8)
        return [bool(present & (1 << i)) for i in range(NUM_SLOTS)]

    def get_estop_state(self) -> bool:
        """Read E-stop state from MCP23017 #1 port B bit 1."""
        port_b = self._bus.read_byte_data(ADDR_GPIO_AUX, MCP23017_GPIOB)
        return bool(port_b & 0x02)

    def read_bus_status(self) -> BusStatus:
        """Read main 24V bus voltage, current, and power from INA228."""
        status = BusStatus()

        # INA228 VBUS register (20-bit, 195.3125 uV/LSB)
        raw = self._bus.read_word_data(ADDR_INA228, INA228_VBUS)
        raw = ((raw & 0xFF) << 8) | ((raw >> 8) & 0xFF)  # swap bytes
        status.voltage_v = (raw >> 4) * 0.0001953125 * 16  # TODO: verify scaling

        # INA228 CURRENT register (scaled by calibration)
        raw = self._bus.read_word_data(ADDR_INA228, INA228_CURRENT)
        raw = ((raw & 0xFF) << 8) | ((raw >> 8) & 0xFF)
        status.current_a = raw * 0.001  # TODO: calibrate based on shunt value

        status.power_w = status.voltage_v * status.current_a
        return status

    def read_slot_current(self, slot_id: int) -> float:
        """Read current for a specific slot from INA3221 (via TCA9548A)."""
        if not 0 <= slot_id < NUM_SLOTS:
            raise ValueError(f"Invalid slot ID: {slot_id}")

        # Determine which INA3221 and channel
        if slot_id < 12:
            mux_channel = 0  # TCA9548A ch0 = bank 0
            ina_index = slot_id // 3
            ina_channel = slot_id % 3
        else:
            mux_channel = 1  # TCA9548A ch1 = bank 1
            adj = slot_id - 12
            ina_index = adj // 3
            ina_channel = adj % 3

        # Select TCA9548A channel
        self._bus.write_byte(ADDR_TCA9548A, 1 << mux_channel)

        # Read INA3221 shunt voltage register for the channel
        ina_addr = INA3221_ADDRS[ina_index]
        shunt_regs = [INA3221_CH1_SHUNT, INA3221_CH2_SHUNT, INA3221_CH3_SHUNT]
        reg = shunt_regs[ina_channel]

        raw = self._bus.read_word_data(ina_addr, reg)
        raw = ((raw & 0xFF) << 8) | ((raw >> 8) & 0xFF)

        # INA3221 shunt voltage: 40 uV/LSB, right-justified 13-bit signed
        raw_signed = raw >> 3
        if raw_signed & 0x1000:
            raw_signed -= 0x2000
        shunt_voltage = raw_signed * 0.00004  # V

        # TODO: convert to current based on shunt resistor value
        # Placeholder: assume 5mohm shunt
        current = shunt_voltage / 0.005

        # Deselect TCA9548A
        self._bus.write_byte(ADDR_TCA9548A, 0x00)

        return current

    def read_slot_voltage(self, slot_id: int) -> float:
        """Read bus voltage for a specific slot from INA3221 (via TCA9548A)."""
        if not 0 <= slot_id < NUM_SLOTS:
            raise ValueError(f"Invalid slot ID: {slot_id}")

        if slot_id < 12:
            mux_channel = 0
            ina_index = slot_id // 3
            ina_channel = slot_id % 3
        else:
            mux_channel = 1
            adj = slot_id - 12
            ina_index = adj // 3
            ina_channel = adj % 3

        self._bus.write_byte(ADDR_TCA9548A, 1 << mux_channel)

        ina_addr = INA3221_ADDRS[ina_index]
        bus_regs = [INA3221_CH1_BUS, INA3221_CH2_BUS, INA3221_CH3_BUS]
        reg = bus_regs[ina_channel]

        raw = self._bus.read_word_data(ina_addr, reg)
        raw = ((raw & 0xFF) << 8) | ((raw >> 8) & 0xFF)

        # INA3221 bus voltage: 8 mV/LSB, right-justified 13-bit
        voltage = (raw >> 3) * 0.008

        self._bus.write_byte(ADDR_TCA9548A, 0x00)
        return voltage

    def get_all_slot_status(self) -> list[SlotStatus]:
        """Get status of all slots."""
        card_present = self.get_card_present()
        statuses = []
        for i in range(NUM_SLOTS):
            s = SlotStatus(
                slot_id=i,
                power_enabled=self.is_slot_enabled(i),
                card_present=card_present[i],
            )
            if s.power_enabled:
                s.bus_voltage_v = self.read_slot_voltage(i)
                s.current_a = self.read_slot_current(i)
            statuses.append(s)
        return statuses

    def _write_power_state(self) -> None:
        """Write the 16-bit power state to MCP23017 #0."""
        # Slots 0-7 -> MCP23017 #0 port A
        self._bus.write_byte_data(ADDR_GPIO_POWER, MCP23017_OLATA,
                                  self._power_state & 0xFF)
        # Slots 8-15 -> MCP23017 #0 port B
        self._bus.write_byte_data(ADDR_GPIO_POWER, MCP23017_OLATB,
                                  (self._power_state >> 8) & 0xFF)


class _MockSMBus:
    """Mock SMBus for development without hardware."""

    def __init__(self):
        self._regs: dict[tuple[int, int], int] = {}

    def write_byte_data(self, addr: int, reg: int, value: int) -> None:
        self._regs[(addr, reg)] = value

    def read_byte_data(self, addr: int, reg: int) -> int:
        return self._regs.get((addr, reg), 0)

    def write_byte(self, addr: int, value: int) -> None:
        self._regs[(addr, -1)] = value

    def read_word_data(self, addr: int, reg: int) -> int:
        return self._regs.get((addr, reg), 0)

    def close(self) -> None:
        pass
