"""MameBridge — async client for the MAME JSON-RPC bridge."""

from __future__ import annotations

import asyncio
import base64
from dataclasses import dataclass
from typing import Any, AsyncIterator

from mamebridge.errors import (
    ConnectionError,
    ProtocolError,
    MameBridgeError,
    raise_for_error,
)
from mamebridge.events import EventBus, _Subscription
from mamebridge.protocol import (
    API_VERSION,
    Event,
    decode_line,
    encode_request,
    is_notification,
    is_response,
)


@dataclass
class Breakpoint:
    id: int
    address: int | None = None
    device: str = ":maincpu"
    condition: str = ""
    action: str = ""
    enabled: bool = True


@dataclass
class Watchpoint:
    id: int
    address: int | None = None
    length: int | None = None
    type: str = "rw"
    device: str = ":maincpu"
    space: str = "program"
    condition: str = ""
    action: str = ""
    enabled: bool = True


@dataclass
class Register:
    name: str
    value: int
    size_bits: int = 0


@dataclass
class DisassemblyLine:
    address: int
    text: str


@dataclass
class DriverInfo:
    shortname: str
    description: str
    manufacturer: str
    year: str
    parent: str
    is_bios: bool


@dataclass
class TapInfo:
    tap_id: int


@dataclass
class TapBuffer:
    entries: list[dict[str, Any]]
    drops: int
    remaining: int


class MameBridge:
    """Async client for the MAME MCP bridge.

    Usage::

        async with MameBridge.connect("127.0.0.1", 8080) as bridge:
            info = await bridge.ping()
            regs = await bridge.list_regs()
    """

    def __init__(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
    ):
        self._reader = reader
        self._writer = writer
        self._next_id = 1
        self._pending: dict[int, asyncio.Future[dict[str, Any]]] = {}
        self._event_bus = EventBus()
        self._reader_task: asyncio.Task[None] | None = None
        self._closed = False

    @classmethod
    async def connect(cls, host: str = "127.0.0.1", port: int = 8080) -> MameBridge:
        """Connect to a running MAME bridge instance."""
        try:
            reader, writer = await asyncio.open_connection(host, port)
        except OSError as e:
            raise ConnectionError(f"failed to connect to {host}:{port}: {e}") from e
        bridge = cls(reader, writer)
        bridge._reader_task = asyncio.create_task(bridge._read_loop())
        return bridge

    async def close(self) -> None:
        """Close the connection."""
        if self._closed:
            return
        self._closed = True
        if self._reader_task:
            self._reader_task.cancel()
            try:
                await self._reader_task
            except asyncio.CancelledError:
                pass
        self._writer.close()
        try:
            await self._writer.wait_closed()
        except OSError:
            pass
        # Cancel any pending futures
        for fut in self._pending.values():
            if not fut.done():
                fut.set_exception(ConnectionError("connection closed"))
        self._pending.clear()

    async def __aenter__(self) -> MameBridge:
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.close()

    # ── Low-level RPC ────────────────────────────────────────────────

    async def call(self, method: str, **params: Any) -> Any:
        """Send a JSON-RPC request and wait for the response.

        Returns the 'result' field on success.
        Raises the appropriate MameBridgeError subclass on error.
        """
        if self._closed:
            raise ConnectionError("connection closed")
        req_id = self._next_id
        self._next_id += 1
        fut: asyncio.Future[dict[str, Any]] = asyncio.get_event_loop().create_future()
        self._pending[req_id] = fut
        data = encode_request(req_id, method, params)
        self._writer.write(data)
        await self._writer.drain()
        try:
            msg = await fut
        except asyncio.CancelledError:
            self._pending.pop(req_id, None)
            raise
        if "error" in msg:
            err = msg["error"]
            raise_for_error(err.get("code", -32603), err.get("message", "unknown"), err.get("data"))
        return msg.get("result")

    def events(self, name: str | None = None) -> _Subscription:
        """Subscribe to server-sent event notifications.

        Args:
            name: Filter by event method name (e.g. "event.breakpoint").
                  None receives all events.
        """
        return self._event_bus.subscribe(name)

    async def wait_for(
        self,
        name: str,
        *,
        timeout: float = 10.0,
        filter: Any = None,
    ) -> Event:
        """Wait for a single matching event."""
        return await self._event_bus.wait_for(name, timeout=timeout, filter=filter)

    # ── Background reader ────────────────────────────────────────────

    async def _read_loop(self) -> None:
        """Background task: read lines, demux into responses vs notifications."""
        try:
            while not self._closed:
                line = await self._reader.readline()
                if not line:
                    break
                line = line.strip()
                if not line:
                    continue
                try:
                    msg = decode_line(line)
                except ProtocolError:
                    continue

                if is_notification(msg):
                    event = Event(
                        method=msg["method"],
                        params=msg.get("params", {}),
                    )
                    self._event_bus.push(event)
                elif is_response(msg):
                    req_id = msg["id"]
                    fut = self._pending.pop(req_id, None)
                    if fut and not fut.done():
                        fut.set_result(msg)
        except asyncio.CancelledError:
            pass
        except Exception:
            pass
        finally:
            # Connection lost — fail all pending requests
            for fut in self._pending.values():
                if not fut.done():
                    fut.set_exception(ConnectionError("connection lost"))
            self._pending.clear()

    # ── Convenience methods ──────────────────────────────────────────

    # machine.lua
    async def ping(self) -> dict[str, Any]:
        return await self.call("ping")

    async def list_devices(self) -> list[dict[str, Any]]:
        return await self.call("list_devices")

    async def list_spaces(self, device: str = ":maincpu") -> list[dict[str, Any]]:
        return await self.call("list_spaces", device=device)

    async def driver_info(self) -> DriverInfo:
        r = await self.call("driver_info")
        return DriverInfo(
            shortname=r["shortname"],
            description=r["description"],
            manufacturer=r["manufacturer"],
            year=r["year"],
            parent=r.get("parent", ""),
            is_bios=r.get("is_bios", False),
        )

    async def list_handlers(self) -> list[str]:
        return await self.call("list_handlers")

    # memory.lua
    async def read_mem(
        self,
        address: int,
        length: int = 1,
        *,
        device: str = ":maincpu",
        space: str = "program",
        unit_size: int = 1,
    ) -> list[int]:
        r = await self.call("read_mem", address=address, length=length,
                            device=device, space=space, unit_size=unit_size)
        return r["bytes"]

    async def write_mem(
        self,
        address: int,
        data: list[int] | bytes,
        *,
        device: str = ":maincpu",
        space: str = "program",
    ) -> int:
        r = await self.call("write_mem", address=address, bytes=list(data),
                            device=device, space=space)
        return r["written"]

    async def search_mem(
        self,
        address: int,
        length: int,
        pattern: list[int],
        *,
        device: str = ":maincpu",
        space: str = "program",
    ) -> list[int]:
        r = await self.call("search_mem", address=address, length=length,
                            pattern=pattern, device=device, space=space)
        return r["matches"]

    async def install_read_tap(
        self,
        address: int,
        length: int,
        *,
        device: str = ":maincpu",
        space: str = "program",
        capacity: int | None = None,
    ) -> TapInfo:
        kwargs: dict[str, Any] = dict(address=address, length=length, device=device, space=space)
        if capacity is not None:
            kwargs["capacity"] = capacity
        r = await self.call("install_read_tap", **kwargs)
        return TapInfo(tap_id=r["tap_id"])

    async def install_write_tap(
        self,
        address: int,
        length: int,
        *,
        device: str = ":maincpu",
        space: str = "program",
        capacity: int | None = None,
    ) -> TapInfo:
        kwargs: dict[str, Any] = dict(address=address, length=length, device=device, space=space)
        if capacity is not None:
            kwargs["capacity"] = capacity
        r = await self.call("install_write_tap", **kwargs)
        return TapInfo(tap_id=r["tap_id"])

    async def read_tap_buffer(
        self,
        tap_id: int,
        *,
        max_entries: int | None = None,
        drain: bool = True,
    ) -> TapBuffer:
        kwargs: dict[str, Any] = dict(tap_id=tap_id, drain=drain)
        if max_entries is not None:
            kwargs["max_entries"] = max_entries
        r = await self.call("read_tap_buffer", **kwargs)
        return TapBuffer(entries=r["entries"], drops=r["drops"], remaining=r["remaining"])

    async def remove_tap(self, tap_id: int) -> None:
        await self.call("remove_tap", tap_id=tap_id)

    # cpu.lua
    async def read_reg(self, name: str, *, device: str = ":maincpu") -> int:
        r = await self.call("read_reg", name=name, device=device)
        return r["value"]

    async def write_reg(self, name: str, value: int, *, device: str = ":maincpu") -> None:
        await self.call("write_reg", name=name, value=value, device=device)

    async def list_regs(self, *, device: str = ":maincpu") -> list[Register]:
        r = await self.call("list_regs", device=device)
        return [Register(name=e["name"], value=e["value"], size_bits=e.get("size_bits", 0)) for e in r]

    async def disassemble(
        self,
        address: int,
        count: int = 10,
        *,
        device: str = ":maincpu",
    ) -> list[DisassemblyLine]:
        r = await self.call("disassemble", address=address, count=count, device=device)
        return [DisassemblyLine(address=e["address"], text=e["text"]) for e in r]

    # debugger.lua
    async def exec_state(self) -> str:
        r = await self.call("exec_state")
        return r["state"]

    async def step(self, count: int = 1, *, device: str = ":maincpu") -> int:
        r = await self.call("step", count=count, device=device)
        return r["pc"]

    async def step_over(self, *, device: str = ":maincpu") -> None:
        await self.call("step_over", device=device)

    async def step_out(self, *, device: str = ":maincpu") -> None:
        await self.call("step_out", device=device)

    async def run(self) -> None:
        await self.call("run")

    async def pause(self) -> None:
        await self.call("pause")

    async def bp_set(
        self,
        address: int,
        *,
        device: str = ":maincpu",
        condition: str = "",
        action: str = "",
        oneshot: bool = False,
    ) -> Breakpoint:
        r = await self.call("bp_set", address=address, device=device,
                            condition=condition, action=action, oneshot=oneshot)
        return Breakpoint(id=r["id"], address=address, device=device,
                          condition=condition, action=action)

    async def bp_clear(self, bp_id: int) -> None:
        await self.call("bp_clear", id=bp_id)

    async def bp_enable(self, bp_id: int, enabled: bool = True) -> None:
        await self.call("bp_enable", id=bp_id, enabled=enabled)

    async def bp_list(self, *, device: str | None = None) -> list[Breakpoint]:
        kwargs: dict[str, Any] = {}
        if device is not None:
            kwargs["device"] = device
        r = await self.call("bp_list", **kwargs)
        return [
            Breakpoint(
                id=b["id"], address=b["address"], device=b.get("device", ":maincpu"),
                condition=b.get("condition", ""), action=b.get("action", ""),
                enabled=b.get("enabled", True),
            )
            for b in r
        ]

    async def wp_set(
        self,
        address: int,
        length: int,
        type: str = "rw",
        *,
        device: str = ":maincpu",
        space: str = "program",
        condition: str = "",
        action: str = "",
    ) -> Watchpoint:
        r = await self.call("wp_set", address=address, length=length, type=type,
                            device=device, space=space, condition=condition, action=action)
        return Watchpoint(id=r["id"], address=address, length=length, type=type,
                          device=device, space=space, condition=condition, action=action)

    async def wp_clear(self, wp_id: int) -> None:
        await self.call("wp_clear", id=wp_id)

    async def wp_enable(self, wp_id: int, enabled: bool = True) -> None:
        await self.call("wp_enable", id=wp_id, enabled=enabled)

    async def wp_list(self, *, device: str | None = None) -> list[Watchpoint]:
        kwargs: dict[str, Any] = {}
        if device is not None:
            kwargs["device"] = device
        r = await self.call("wp_list", **kwargs)
        return [
            Watchpoint(
                id=w["id"], address=w["address"], length=w.get("length"),
                type=w.get("type", "rw"), device=w.get("device", ":maincpu"),
                space=w.get("space", "program"),
                condition=w.get("condition", ""), action=w.get("action", ""),
                enabled=w.get("enabled", True),
            )
            for w in r
        ]

    # state.lua
    async def save_state(self, name: str = "mcp_save") -> str:
        r = await self.call("save_state", name=name)
        return r["name"]

    async def load_state(self, name: str) -> None:
        await self.call("load_state", name=name)

    async def screenshot(self, *, screen: str | None = None) -> bytes:
        """Take a screenshot, return PNG data as bytes."""
        kwargs: dict[str, Any] = {}
        if screen is not None:
            kwargs["screen"] = screen
        r = await self.call("screenshot", **kwargs)
        return base64.b64decode(r["data"])

    async def frame_number(self) -> int:
        r = await self.call("frame_number")
        return r["frame"]

    async def quit(self) -> None:
        """Ask MAME to exit gracefully via machine:exit()."""
        try:
            await self.call("quit")
        except Exception:
            pass  # Connection may drop before we get a response

    # raw.lua
    async def exec_command(self, command: str) -> str:
        r = await self.call("exec_command", command=command)
        return r["output"]

    # ── High-level helpers ───────────────────────────────────────────

    async def run_until(
        self,
        address: int,
        *,
        device: str = ":maincpu",
        timeout: float = 10.0,
    ) -> int:
        """Run until the given address is hit. Returns PC at stop.

        Handles the "PC already at target" case by stepping first.
        Sets a temporary breakpoint, runs, waits for the event, cleans up.
        """
        from mamebridge.errors import TimeoutError as BridgeTimeout

        # If PC is already at the target, step past it first
        current_pc = await self.read_reg("PC", device=device)
        if current_pc == address:
            await self.step(device=device)

        # Set temporary breakpoint
        bp = await self.bp_set(address, device=device, oneshot=True)

        # Subscribe to breakpoint events before running
        sub = self.events("event.breakpoint")
        try:
            await self.run()
            # Wait for the breakpoint hit
            try:
                event = await asyncio.wait_for(sub.__anext__(), timeout=timeout)
                return event.params.get("pc", address)
            except asyncio.TimeoutError:
                # Timeout — pause and clean up
                await self.pause()
                try:
                    await self.bp_clear(bp.id)
                except Exception:
                    pass
                raise BridgeTimeout(f"run_until 0x{address:x} timed out after {timeout}s")
        finally:
            sub.close()
