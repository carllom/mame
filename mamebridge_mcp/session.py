"""Session lifecycle — holds the single live MameBridge for the server."""

from __future__ import annotations

import contextlib
from typing import Any

from mamebridge.client import MameBridge
from mamebridge.launcher import launch_mame
from mcp.server.fastmcp.exceptions import ToolError


class Session:
    """Manages the single MAME session for this server process."""

    def __init__(self) -> None:
        self._bridge: MameBridge | None = None
        self._exit_stack: contextlib.AsyncExitStack | None = None
        self._driver: str | None = None
        self._attached: bool = False

    async def start(
        self,
        driver: str,
        flags: list[str] | None = None,
        port: int | None = None,
        mame_binary: str = "mame",
        rom_path: str | None = None,
    ) -> None:
        if self._bridge is not None:
            raise ToolError(
                "A session is already active. Call stop_session first."
            )
        stack = contextlib.AsyncExitStack()
        bridge = await stack.enter_async_context(
            launch_mame(
                driver,
                mame_binary=mame_binary,
                rom_path=rom_path,
                extra_flags=flags or [],
                port=port,
            )
        )
        self._exit_stack = stack
        self._bridge = bridge
        self._driver = driver
        self._attached = False

    async def attach(self, host: str = "127.0.0.1", port: int = 8080) -> None:
        if self._bridge is not None:
            raise ToolError(
                "A session is already active. Call stop_session first."
            )
        self._bridge = await MameBridge.connect(host, port)
        self._driver = None
        self._attached = True

    async def stop(self) -> None:
        if self._exit_stack is not None:
            await self._exit_stack.aclose()
            self._exit_stack = None
        elif self._bridge is not None:
            await self._bridge.close()
        self._bridge = None
        self._driver = None
        self._attached = False

    async def status(self) -> dict[str, Any]:
        if self._bridge is None:
            return {
                "alive": False,
                "driver": None,
                "attached": False,
                "frame": 0,
                "exec_state": None,
                "pc": None,
            }
        try:
            state = await self._bridge.exec_state()
            frame = await self._bridge.frame_number()
            try:
                pc = await self._bridge.read_reg("PC")
            except Exception:
                pc = None
            driver = self._driver
            if driver is None:
                info = await self._bridge.driver_info()
                driver = info.shortname
            return {
                "alive": True,
                "driver": driver,
                "attached": self._attached,
                "frame": frame,
                "exec_state": state,
                "pc": pc,
            }
        except Exception as exc:
            return {
                "alive": False,
                "driver": self._driver,
                "attached": self._attached,
                "frame": 0,
                "exec_state": None,
                "pc": None,
                "error": str(exc),
            }

    def require(self) -> MameBridge:
        """Return the active bridge or raise a ToolError."""
        if self._bridge is None:
            raise ToolError(
                "No active MAME session. Call start_session to launch MAME, "
                "or attach_session to connect to a running instance."
            )
        return self._bridge


session = Session()
