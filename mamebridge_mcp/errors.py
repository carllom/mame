"""MameBridgeError → agent-friendly MCP tool error messages."""

from __future__ import annotations

from typing import Any

from mamebridge.errors import (
    BreakpointNotFound,
    ConnectionError,
    DebuggerNotEnabled,
    DeviceNotFound,
    InvalidAddress,
    InvalidAddressSpace,
    MameBridgeError,
    MameDied,
    SystemNotRunning,
    TapNotFound,
    TimeoutError,
)
from mcp.server.fastmcp.exceptions import ToolError


def mcp_error_message(exc: MameBridgeError, **ctx: Any) -> str:
    if isinstance(exc, DebuggerNotEnabled):
        return (
            'MAME was not launched with -debug. '
            'Restart with start_session(driver, flags=["-debug", ...]).'
        )
    if isinstance(exc, DeviceNotFound):
        device = ctx.get("device", "?")
        return (
            f"Device {device!r} not found. "
            "Use list_devices to see what's available."
        )
    if isinstance(exc, InvalidAddressSpace):
        space = ctx.get("space", "?")
        device = ctx.get("device", "?")
        return (
            f"Address space {space!r} not found on {device!r}. "
            "Use list_address_spaces to see what's available."
        )
    if isinstance(exc, BreakpointNotFound):
        bp_id = ctx.get("id", "?")
        return (
            f"Breakpoint id {bp_id} not found. "
            "Use bp_list to see active breakpoints."
        )
    if isinstance(exc, SystemNotRunning):
        return (
            "The system is not in a running/stopped state yet (still booting?). "
            "Try again in a frame or two."
        )
    if isinstance(exc, InvalidAddress):
        addr = ctx.get("address")
        addr_str = f"0x{addr:x}" if isinstance(addr, int) else str(addr)
        space = ctx.get("space", "?")
        return f"Address {addr_str} is invalid for the {space!r} space."
    if isinstance(exc, TapNotFound):
        tap_id = ctx.get("id", "?")
        return (
            f"Tap id {tap_id} not found. "
            "It may have been removed or never installed."
        )
    if isinstance(exc, TimeoutError):
        return (
            "Operation timed out. "
            "The CPU may be in a tight loop or never reaching the target."
        )
    if isinstance(exc, (MameDied, ConnectionError)):
        return (
            "Lost connection to MAME. "
            "Use session_status to check, then start_session or attach_session again."
        )
    return str(exc)


def raise_as_tool_error(exc: MameBridgeError, **ctx: Any) -> None:
    raise ToolError(mcp_error_message(exc, **ctx)) from exc
