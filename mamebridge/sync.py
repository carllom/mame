"""Synchronous facade for MameBridge.

Wraps the async API so it works in REPLs and simple scripts without
asyncio boilerplate.

Usage::

    from mamebridge.sync import SyncMameBridge

    bridge = SyncMameBridge.connect("127.0.0.1", 8080)
    print(bridge.ping())
    bridge.close()
"""

from __future__ import annotations

import asyncio
from typing import Any

from mamebridge.client import (
    MameBridge,
    Breakpoint,
    Watchpoint,
    Register,
    DisassemblyLine,
    DriverInfo,
    TapInfo,
    TapBuffer,
)


class SyncMameBridge:
    """Synchronous wrapper around MameBridge."""

    def __init__(self, bridge: MameBridge, loop: asyncio.AbstractEventLoop):
        self._bridge = bridge
        self._loop = loop

    @classmethod
    def connect(cls, host: str = "127.0.0.1", port: int = 8080) -> SyncMameBridge:
        loop = asyncio.new_event_loop()
        bridge = loop.run_until_complete(MameBridge.connect(host, port))
        return cls(bridge, loop)

    def close(self) -> None:
        self._loop.run_until_complete(self._bridge.close())
        self._loop.close()

    def __enter__(self) -> SyncMameBridge:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def call(self, method: str, **params: Any) -> Any:
        return self._loop.run_until_complete(self._bridge.call(method, **params))

    # ── Convenience methods ──────────────────────────────────────────

    def ping(self) -> dict[str, Any]:
        return self._loop.run_until_complete(self._bridge.ping())

    def list_devices(self) -> list[dict[str, Any]]:
        return self._loop.run_until_complete(self._bridge.list_devices())

    def list_spaces(self, device: str = ":maincpu") -> list[dict[str, Any]]:
        return self._loop.run_until_complete(self._bridge.list_spaces(device))

    def driver_info(self) -> DriverInfo:
        return self._loop.run_until_complete(self._bridge.driver_info())

    def list_handlers(self) -> list[str]:
        return self._loop.run_until_complete(self._bridge.list_handlers())

    def read_mem(self, address: int, length: int = 1, *, device: str = ":maincpu",
                 space: str = "program", unit_size: int = 1) -> list[int]:
        return self._loop.run_until_complete(
            self._bridge.read_mem(address, length, device=device, space=space, unit_size=unit_size))

    def write_mem(self, address: int, data: list[int] | bytes, *,
                  device: str = ":maincpu", space: str = "program") -> int:
        return self._loop.run_until_complete(
            self._bridge.write_mem(address, data, device=device, space=space))

    def read_reg(self, name: str, *, device: str = ":maincpu") -> int:
        return self._loop.run_until_complete(self._bridge.read_reg(name, device=device))

    def write_reg(self, name: str, value: int, *, device: str = ":maincpu") -> None:
        self._loop.run_until_complete(self._bridge.write_reg(name, value, device=device))

    def list_regs(self, *, device: str = ":maincpu") -> list[Register]:
        return self._loop.run_until_complete(self._bridge.list_regs(device=device))

    def disassemble(self, address: int, count: int = 10, *,
                    device: str = ":maincpu") -> list[DisassemblyLine]:
        return self._loop.run_until_complete(
            self._bridge.disassemble(address, count, device=device))

    def exec_state(self) -> str:
        return self._loop.run_until_complete(self._bridge.exec_state())

    def step(self, count: int = 1, *, device: str = ":maincpu") -> int:
        return self._loop.run_until_complete(self._bridge.step(count, device=device))

    def run(self) -> None:
        self._loop.run_until_complete(self._bridge.run())

    def pause(self) -> None:
        self._loop.run_until_complete(self._bridge.pause())

    def bp_set(self, address: int, *, device: str = ":maincpu",
               condition: str = "", action: str = "", oneshot: bool = False) -> Breakpoint:
        return self._loop.run_until_complete(
            self._bridge.bp_set(address, device=device, condition=condition,
                                action=action, oneshot=oneshot))

    def bp_clear(self, bp_id: int) -> None:
        self._loop.run_until_complete(self._bridge.bp_clear(bp_id))

    def bp_list(self, *, device: str | None = None) -> list[Breakpoint]:
        return self._loop.run_until_complete(self._bridge.bp_list(device=device))

    def wp_set(self, address: int, length: int, type: str = "rw", *,
               device: str = ":maincpu", space: str = "program") -> Watchpoint:
        return self._loop.run_until_complete(
            self._bridge.wp_set(address, length, type, device=device, space=space))

    def wp_clear(self, wp_id: int) -> None:
        self._loop.run_until_complete(self._bridge.wp_clear(wp_id))

    def wp_list(self, *, device: str | None = None) -> list[Watchpoint]:
        return self._loop.run_until_complete(self._bridge.wp_list(device=device))

    def save_state(self, name: str = "mcp_save") -> str:
        return self._loop.run_until_complete(self._bridge.save_state(name))

    def load_state(self, name: str) -> None:
        self._loop.run_until_complete(self._bridge.load_state(name))

    def screenshot(self, *, screen: str | None = None) -> bytes:
        return self._loop.run_until_complete(self._bridge.screenshot(screen=screen))

    def frame_number(self) -> int:
        return self._loop.run_until_complete(self._bridge.frame_number())

    def exec_command(self, command: str) -> str:
        return self._loop.run_until_complete(self._bridge.exec_command(command))

    def run_until(self, address: int, *, device: str = ":maincpu",
                  timeout: float = 10.0) -> int:
        return self._loop.run_until_complete(
            self._bridge.run_until(address, device=device, timeout=timeout))
