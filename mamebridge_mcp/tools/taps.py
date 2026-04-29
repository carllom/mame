"""Tap tools: low-level install/read/remove, plus composite tap_and_run patterns."""

from __future__ import annotations

from typing import Any

from mamebridge.errors import MameBridgeError
from mamebridge_mcp._app import mcp
from mamebridge_mcp.errors import raise_as_tool_error
from mamebridge_mcp.schemas import coerce_address
from mamebridge_mcp.session import session


@mcp.tool()
async def install_read_tap(
    address: int | str,
    length: int,
    device: str = ":maincpu",
    space: str = "program",
    capacity: int | None = None,
) -> dict[str, int]:
    """Install a read tap on a memory range.

    A read tap records every read access to the range without stopping execution.
    Use read_tap_buffer to drain the collected entries, and remove_tap to clean up.

    For the common pattern of "watch X for N frames", use find_reads_of instead.

    Args:
        address: Start address of the watched range.
        length: Number of bytes to watch.
        device: Device tag (default ':maincpu').
        space: Address space (default 'program').
        capacity: Maximum number of entries before dropping (default: bridge default).

    Returns: {"tap_id": int} — use this ID for read_tap_buffer and remove_tap.
    """
    bridge = session.require()
    addr = coerce_address(address)
    try:
        info = await bridge.install_read_tap(
            addr, length, device=device, space=space, capacity=capacity
        )
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, space=space, address=addr)
    return {"tap_id": info.tap_id}


@mcp.tool()
async def install_write_tap(
    address: int | str,
    length: int,
    device: str = ":maincpu",
    space: str = "program",
    capacity: int | None = None,
) -> dict[str, int]:
    """Install a write tap on a memory range.

    A write tap records every write access to the range without stopping
    execution. Use read_tap_buffer to drain entries, remove_tap to clean up.

    For the common pattern of "what writes to X over N frames", use
    find_writes_to instead — it handles the full lifecycle automatically.

    Args:
        address: Start address of the watched range.
        length: Number of bytes to watch.
        device: Device tag (default ':maincpu').
        space: Address space (default 'program').
        capacity: Maximum entries before dropping (default: bridge default).

    Returns: {"tap_id": int}.
    """
    bridge = session.require()
    addr = coerce_address(address)
    try:
        info = await bridge.install_write_tap(
            addr, length, device=device, space=space, capacity=capacity
        )
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, space=space, address=addr)
    return {"tap_id": info.tap_id}


@mcp.tool()
async def read_tap_buffer(
    tap_id: int,
    max_entries: int | None = None,
    drain: bool = True,
) -> dict[str, Any]:
    """Read entries collected by a tap.

    Args:
        tap_id: Tap ID from install_read_tap or install_write_tap.
        max_entries: Maximum entries to return (default: all available).
        drain: If true, entries are removed from the buffer after reading.
               Set false to peek without consuming (default: true).

    Returns:
        entries: List of {"pc", "addr", "value", "frame"} dicts.
        drops: Number of entries that were discarded due to buffer overflow.
               If > 0, consider reducing length, frames, or increasing capacity.
        remaining: Entries still in the buffer after this read.
    """
    bridge = session.require()
    try:
        buf = await bridge.read_tap_buffer(
            tap_id, max_entries=max_entries, drain=drain
        )
    except MameBridgeError as exc:
        raise_as_tool_error(exc, id=tap_id)
    return {
        "entries": buf.entries,
        "drops": buf.drops,
        "remaining": buf.remaining,
    }


@mcp.tool()
async def remove_tap(tap_id: int) -> dict[str, Any]:
    """Remove a tap by its ID, freeing resources.

    Returns: {"tap_id": int, "removed": true}.
    """
    bridge = session.require()
    try:
        await bridge.remove_tap(tap_id)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, id=tap_id)
    return {"tap_id": tap_id, "removed": True}


@mcp.tool()
async def tap_and_run(
    address: int | str,
    length: int,
    frames: int,
    kind: str = "write",
    device: str = ":maincpu",
    space: str = "program",
    capacity: int | None = None,
) -> dict[str, Any]:
    """Install a tap, run for N frames, collect results, and clean up.

    The workhorse composite for memory access tracing. Handles the full
    tap lifecycle (install → run → collect → remove) automatically.

    Args:
        address: Start address to watch.
        length: Number of bytes to watch.
        frames: Number of video frames to run (~60 per second).
        kind: 'read' or 'write' (default 'write').
        device: Device tag (default ':maincpu').
        space: Address space (default 'program').
        capacity: Buffer capacity before entries are dropped.

    Returns:
        entries: List of {"pc", "addr", "value", "frame"} dicts.
        drops: Number of dropped entries due to buffer overflow.
        frames_run: Actual number of frames advanced.
        warning: Present only if drops > 0.
    """
    import asyncio

    bridge = session.require()
    addr = coerce_address(address)

    if kind == "read":
        install = bridge.install_read_tap
    elif kind == "write":
        install = bridge.install_write_tap
    else:
        from mcp.server.fastmcp.exceptions import ToolError
        raise ToolError(f"kind must be 'read' or 'write', got {kind!r}")

    try:
        if capacity is not None:
            tap_info = await install(addr, length, device=device, space=space, capacity=capacity)
        else:
            tap_info = await install(addr, length, device=device, space=space)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, space=space, address=addr)

    tap_id = tap_info.tap_id
    try:
        start_frame = await bridge.frame_number()
        target_frame = start_frame + frames
        timeout = frames * 0.05 + 5.0

        await bridge.run()
        deadline = asyncio.get_event_loop().time() + timeout
        while asyncio.get_event_loop().time() < deadline:
            await asyncio.sleep(0.05)
            current = await bridge.frame_number()
            if current >= target_frame:
                break
        await bridge.pause()
        final_frame = await bridge.frame_number()

        buf = await bridge.read_tap_buffer(tap_id, drain=True)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    finally:
        try:
            await bridge.remove_tap(tap_id)
        except Exception:
            pass

    result: dict[str, Any] = {
        "entries": buf.entries,
        "drops": buf.drops,
        "frames_run": final_frame - start_frame,
    }
    if buf.drops > 0:
        result["warning"] = (
            f"Buffer overflowed ({buf.drops} drops). "
            "Reduce frames or length, or pass a larger capacity."
        )
    return result


@mcp.tool()
async def find_writes_to(
    address: int | str,
    length: int,
    frames: int,
    device: str = ":maincpu",
    space: str = "program",
) -> dict[str, Any]:
    """Find all PCs that write to a memory range over N frames.

    The primary tool for answering "what code writes to address X?". Runs
    the emulation for the specified number of frames while recording all write
    accesses, then returns the collected entries.

    Typical workflow:
        1. find_writes_to(0xd800, 0x20, 60)  → see which PCs write here
        2. disassemble each unique PC to understand the write site

    Args:
        address: Start of the memory range to watch.
        length: Number of bytes to watch.
        frames: Number of video frames to run (~60 per second).
        device: Device tag (default ':maincpu').
        space: Address space (default 'program').

    Returns: same shape as tap_and_run.
    """
    bridge = session.require()
    addr = coerce_address(address)

    import asyncio

    try:
        start_frame = await bridge.frame_number()
        target_frame = start_frame + frames
        timeout = frames * 0.05 + 5.0

        tap_info = await bridge.install_write_tap(addr, length, device=device, space=space)
        tap_id = tap_info.tap_id
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, space=space, address=addr)

    try:
        await bridge.run()
        deadline = asyncio.get_event_loop().time() + timeout
        while asyncio.get_event_loop().time() < deadline:
            await asyncio.sleep(0.05)
            current = await bridge.frame_number()
            if current >= target_frame:
                break
        await bridge.pause()
        final_frame = await bridge.frame_number()
        buf = await bridge.read_tap_buffer(tap_id, drain=True)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    finally:
        try:
            await bridge.remove_tap(tap_id)
        except Exception:
            pass

    result: dict[str, Any] = {
        "entries": buf.entries,
        "drops": buf.drops,
        "frames_run": final_frame - start_frame,
    }
    if buf.drops > 0:
        result["warning"] = (
            f"Buffer overflowed ({buf.drops} drops). "
            "Reduce frames or length, or pass a larger capacity."
        )
    return result


@mcp.tool()
async def find_reads_of(
    address: int | str,
    length: int,
    frames: int,
    device: str = ":maincpu",
    space: str = "program",
) -> dict[str, Any]:
    """Find all PCs that read from a memory range over N frames.

    Use this when you know a piece of data exists in memory and want to find
    the code that consumes it, without stopping execution.

    Args:
        address: Start of the memory range to watch.
        length: Number of bytes to watch.
        frames: Number of video frames to run (~60 per second).
        device: Device tag (default ':maincpu').
        space: Address space (default 'program').

    Returns: same shape as tap_and_run.
    """
    import asyncio

    bridge = session.require()
    addr = coerce_address(address)

    try:
        start_frame = await bridge.frame_number()
        target_frame = start_frame + frames
        timeout = frames * 0.05 + 5.0

        tap_info = await bridge.install_read_tap(addr, length, device=device, space=space)
        tap_id = tap_info.tap_id
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, space=space, address=addr)

    try:
        await bridge.run()
        deadline = asyncio.get_event_loop().time() + timeout
        while asyncio.get_event_loop().time() < deadline:
            await asyncio.sleep(0.05)
            current = await bridge.frame_number()
            if current >= target_frame:
                break
        await bridge.pause()
        final_frame = await bridge.frame_number()
        buf = await bridge.read_tap_buffer(tap_id, drain=True)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    finally:
        try:
            await bridge.remove_tap(tap_id)
        except Exception:
            pass

    result: dict[str, Any] = {
        "entries": buf.entries,
        "drops": buf.drops,
        "frames_run": final_frame - start_frame,
    }
    if buf.drops > 0:
        result["warning"] = (
            f"Buffer overflowed ({buf.drops} drops). "
            "Reduce frames or length, or pass a larger capacity."
        )
    return result
