"""Inspection tools: list devices, spaces, registers; driver info; current state."""

from __future__ import annotations

from typing import Any

from mamebridge.errors import MameBridgeError
from mamebridge_mcp._app import mcp
from mamebridge_mcp.errors import raise_as_tool_error
from mamebridge_mcp.session import session


@mcp.tool()
async def list_devices() -> dict[str, Any]:
    """Return all devices present in the running MAME system.

    When to use: to discover device tags before calling tools that accept
    a 'device' parameter. Most tools default to ':maincpu', which is
    correct for single-CPU systems.

    Returns: {"devices": [{"tag": str, "type": str}, ...]}.
    """
    bridge = session.require()
    try:
        devices = await bridge.list_devices()
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {"devices": devices}


@mcp.tool()
async def list_address_spaces(device: str = ":maincpu") -> dict[str, Any]:
    """Return the address spaces exposed by a device.

    When to use: before reading or writing memory, to confirm the space
    name and address range. Common space names are 'program', 'data', 'io';
    not all devices have all three. Reading the wrong space silently returns
    garbage — always verify with this tool on unfamiliar hardware.

    Args:
        device: Device tag (default ':maincpu'). Use list_devices to find tags.

    Returns: {"spaces": [{"name": "program", "start": 0, "end": 0xffffff, ...}]}.
    """
    bridge = session.require()
    try:
        spaces = await bridge.list_spaces(device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"spaces": spaces}


@mcp.tool()
async def list_registers(device: str = ":maincpu") -> dict[str, Any]:
    """Return all registers and their current values for a device.

    Args:
        device: Device tag (default ':maincpu').

    Returns: {"registers": [{"name": "PC", "value": 12345, "size_bits": 32}, ...]}.
    """
    bridge = session.require()
    try:
        regs = await bridge.list_regs(device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"registers": [{"name": r.name, "value": r.value, "size_bits": r.size_bits} for r in regs]}


@mcp.tool()
async def get_driver_info() -> dict[str, Any]:
    """Return metadata about the currently running MAME driver.

    When to use: once per session, to get the driver name, manufacturer,
    year, and status flags. Read the 'flags' dict to understand the driver's
    known-broken areas before debugging.

    Flag meanings (all bool):
        not_working: treat all emulation output as suspect — basic functionality broken.
        supports_save: savestate support works (True = safe to save/load).
        no_cocktail: cocktail mode not supported.
        is_bios_root: this is a BIOS/system ROM, not a game.
        requires_artwork: requires external artwork files to be useful.
        unofficial: unofficial or prototype driver.
        no_sound_hw: no sound hardware in this system.
        mechanical: mechanical game (pinball, etc.) — most digital logic tools don't apply.
        is_incomplete: driver is known incomplete.

    Returns: {"shortname", "description", "manufacturer", "year", "parent",
              "is_bios", "flags": {flag_name: bool, ...}}.
    """
    bridge = session.require()
    try:
        info = await bridge.driver_info()
        return {
            "shortname": info.shortname,
            "description": info.description,
            "manufacturer": info.manufacturer,
            "year": info.year,
            "parent": info.parent,
            "is_bios": info.is_bios,
            "flags": info.flags,
        }
    except MameBridgeError as exc:
        raise_as_tool_error(exc)


@mcp.tool()
async def current_state() -> dict[str, Any]:
    """Return execution state, PC, frame count, and driver in one call.

    When to use: at any point to orient yourself — especially at the start
    of a session after start_session, and after run_until_breakpoint to see
    where execution stopped. Cheaper than chaining exec_state + list_registers
    + frame_number separately.

    Returns: {"exec_state": "run"|"stop", "pc": int, "frame": int, "driver": str}.
    """
    bridge = session.require()
    try:
        state = await bridge.exec_state()
        frame = await bridge.frame_number()
        try:
            pc = await bridge.read_reg("PC")
        except Exception:
            pc = None
        info = await bridge.driver_info()
        return {
            "exec_state": state,
            "pc": pc,
            "frame": frame,
            "driver": info.shortname,
        }
    except MameBridgeError as exc:
        raise_as_tool_error(exc)


@mcp.tool()
async def summarize_memory_map() -> dict[str, Any]:
    """Return the address space layout for all devices that expose spaces.

    When to use: once at the start of a session to learn the memory topology
    before reading or writing. Tells you which device has which spaces and
    their address ranges.

    Returns:
        {"devices": [{"tag": ":maincpu",
                       "spaces": [{"name":"program","start":0,"end":0xffffff,...}]},
                     ...]}
    """
    bridge = session.require()
    try:
        devices = await bridge.list_devices()
        result = []
        for dev in devices:
            tag = dev.get("tag", "")
            try:
                spaces = await bridge.list_spaces(device=tag)
                if spaces:
                    result.append({"tag": tag, "spaces": spaces})
            except MameBridgeError:
                pass
        return {"devices": result}
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
