"""Breakpoint and watchpoint tools."""

from __future__ import annotations

from typing import Any

from mamebridge.errors import MameBridgeError
from mamebridge_mcp._app import mcp
from mamebridge_mcp.errors import raise_as_tool_error
from mamebridge_mcp.schemas import coerce_address
from mamebridge_mcp.session import session


@mcp.tool()
async def bp_set(
    address: int | str,
    device: str = ":maincpu",
    condition: str = "",
    action: str = "",
    oneshot: bool = False,
) -> dict[str, Any]:
    """Set a breakpoint that stops execution when the CPU reaches an address.

    Breakpoints stop execution when the CPU is *about to execute* the instruction
    at the given PC. To catch memory accesses instead, use wp_set or find_writes_to.

    Condition syntax uses MAME debugger expressions, e.g.:
        condition="maincpu.pb@0x1000 == 0xff"  (stop only when byte at 0x1000 is 0xff)
        condition="D0 > 0x100"

    Args:
        address: Breakpoint address. Accepts '0x1234', '1234h', or decimal.
        device: Device tag (default ':maincpu').
        condition: MAME debugger expression that must be true to trigger (optional).
        action: Debugger command to run on hit, e.g. 'printf "hit\\n"' (optional).
        oneshot: If true, the breakpoint is removed automatically after first hit.

    Returns: {"id": int, "address": int, "device": str}.
    """
    bridge = session.require()
    addr = coerce_address(address)
    try:
        bp = await bridge.bp_set(
            addr, device=device, condition=condition, action=action, oneshot=oneshot
        )
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, address=addr)
    return {"id": bp.id, "address": addr, "device": device}


@mcp.tool()
async def bp_clear(id: int) -> dict[str, Any]:
    """Remove a breakpoint by its ID.

    Args:
        id: Breakpoint ID returned by bp_set or bp_list.

    Returns: {"id": int, "cleared": true}.
    """
    bridge = session.require()
    try:
        await bridge.bp_clear(id)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, id=id)
    return {"id": id, "cleared": True}


@mcp.tool()
async def bp_enable(id: int, enabled: bool = True) -> dict[str, Any]:
    """Enable or disable a breakpoint without removing it.

    Args:
        id: Breakpoint ID.
        enabled: True to enable, False to disable (default True).

    Returns: {"id": int, "enabled": bool}.
    """
    bridge = session.require()
    try:
        await bridge.bp_enable(id, enabled)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, id=id)
    return {"id": id, "enabled": enabled}


@mcp.tool()
async def bp_list(device: str | None = None) -> dict[str, Any]:
    """List all active breakpoints, optionally filtered by device.

    Args:
        device: Filter by device tag (default: all devices).

    Returns: {"breakpoints": [{"id", "address", "device", "condition", "action", "enabled"}, ...]}.
    """
    bridge = session.require()
    try:
        bps = await bridge.bp_list(device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {
        "breakpoints": [
            {
                "id": bp.id,
                "address": bp.address,
                "device": bp.device,
                "condition": bp.condition,
                "action": bp.action,
                "enabled": bp.enabled,
            }
            for bp in bps
        ]
    }


@mcp.tool()
async def wp_set(
    address: int | str,
    length: int,
    type: str = "rw",
    device: str = ":maincpu",
    space: str = "program",
    condition: str = "",
    action: str = "",
) -> dict[str, Any]:
    """Set a watchpoint that stops execution when a memory range is accessed.

    Watchpoints stop execution when the specified memory range is read or
    written. For non-stop observation of memory traffic, use find_writes_to
    or find_reads_of (tap-based), which collect entries without pausing.

    Args:
        address: Start address of the watched range.
        length: Number of bytes to watch.
        type: Access type to watch: 'r' (read), 'w' (write), 'rw' (both, default).
        device: Device tag (default ':maincpu').
        space: Address space (default 'program').
        condition: MAME debugger expression (optional).
        action: Debugger command on hit (optional).

    Returns: {"id": int, "address": int, "length": int, "type": str}.
    """
    bridge = session.require()
    addr = coerce_address(address)
    try:
        wp = await bridge.wp_set(
            addr, length, type,
            device=device, space=space, condition=condition, action=action,
        )
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, space=space, address=addr)
    return {"id": wp.id, "address": addr, "length": length, "type": type}


@mcp.tool()
async def wp_clear(id: int) -> dict[str, Any]:
    """Remove a watchpoint by its ID.

    Returns: {"id": int, "cleared": true}.
    """
    bridge = session.require()
    try:
        await bridge.wp_clear(id)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, id=id)
    return {"id": id, "cleared": True}


@mcp.tool()
async def wp_enable(id: int, enabled: bool = True) -> dict[str, Any]:
    """Enable or disable a watchpoint without removing it.

    Returns: {"id": int, "enabled": bool}.
    """
    bridge = session.require()
    try:
        await bridge.wp_enable(id, enabled)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, id=id)
    return {"id": id, "enabled": enabled}


@mcp.tool()
async def wp_list(device: str | None = None) -> dict[str, Any]:
    """List all active watchpoints, optionally filtered by device.

    Returns: {"watchpoints": [{"id", "address", "length", "type", "device", "space",
               "condition", "action", "enabled"}, ...]}.
    """
    bridge = session.require()
    try:
        wps = await bridge.wp_list(device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {
        "watchpoints": [
            {
                "id": wp.id,
                "address": wp.address,
                "length": wp.length,
                "type": wp.type,
                "device": wp.device,
                "space": wp.space,
                "condition": wp.condition,
                "action": wp.action,
                "enabled": wp.enabled,
            }
            for wp in wps
        ]
    }
