"""
CellSim v2 — Application State

Central state container stored on FastAPI's app.state.
Provides typed access to all subsystem managers and avoids global singletons.
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import dataclass, field
from functools import partial
from typing import Any

from cellsim.card_manager import CardManager
from cellsim.slot_power import SlotPowerManager
from cellsim.slot_mux import SlotMux
from cellsim.models import NUM_SLOTS

logger = logging.getLogger(__name__)


@dataclass
class AppState:
    """Typed container for all subsystem managers."""

    slot_power: SlotPowerManager = field(default_factory=SlotPowerManager)
    slot_mux: SlotMux = field(default_factory=SlotMux)
    card_manager: CardManager = field(default_factory=CardManager)
    bringup_task: asyncio.Task | None = None
    _executor_loop: asyncio.AbstractEventLoop | None = None

    async def startup(self) -> None:
        """Initialize all hardware managers."""
        self._executor_loop = asyncio.get_running_loop()

        # Initialize slot power (blocking I2C calls → run in executor)
        await self._run_sync(self.slot_power.init)

        # Initialize slot mux (shares I2C bus from slot_power)
        await self._run_sync(self.slot_mux.init, self.slot_power._bus)

        # Start card discovery and measurement listener
        await self.card_manager.start_discovery()
        await self.card_manager.start_measurement_listener()

        logger.info("AppState initialized: %d slots configured", NUM_SLOTS)

    async def shutdown(self) -> None:
        """Clean up all managers."""
        if self.bringup_task and not self.bringup_task.done():
            self.bringup_task.cancel()
            try:
                await self.bringup_task
            except asyncio.CancelledError:
                pass

        await self._run_sync(self.slot_mux.close)
        await self._run_sync(self.slot_power.close)
        logger.info("AppState shutdown complete")

    async def _run_sync(self, fn, *args, **kwargs) -> Any:
        """Run a blocking function in the default executor."""
        loop = self._executor_loop or asyncio.get_running_loop()
        if kwargs:
            return await loop.run_in_executor(None, partial(fn, *args, **kwargs))
        return await loop.run_in_executor(None, partial(fn, *args))
