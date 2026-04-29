"""State tools: save/load state, screenshot, frame number."""

from __future__ import annotations

from typing import Any

from mcp.server.fastmcp import Image

from mamebridge.errors import MameBridgeError
from mamebridge_mcp._app import mcp
from mamebridge_mcp.errors import raise_as_tool_error
from mamebridge_mcp.session import session


@mcp.tool()
async def save_state(name: str = "mcp_save") -> dict[str, str]:
    """Save the current emulation state to a named slot.

    When to use: before a risky memory write or register modification, so
    you can revert with load_state if something goes wrong.

    Args:
        name: Slot name (default 'mcp_save'). MAME saves to its state directory.

    Returns: {"name": str}.
    """
    bridge = session.require()
    try:
        saved = await bridge.save_state(name)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {"name": saved}


@mcp.tool()
async def load_state(name: str) -> dict[str, str]:
    """Load a previously saved emulation state.

    Args:
        name: Slot name used with save_state.

    Returns: {"name": str}.
    """
    bridge = session.require()
    try:
        await bridge.load_state(name)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {"name": name}


@mcp.tool()
async def screenshot(screen: str | None = None) -> Image:
    """Take a screenshot of the emulated screen and return it inline.

    Warning: each screenshot is ~50–500 KB encoded inline. Don't take many
    per session — use save_state / load_state for checkpointing instead.

    Args:
        screen: Screen device tag (e.g. ':screen'). Defaults to the first screen.

    Returns: PNG image inline (MCP ImageContent).
    """
    bridge = session.require()
    try:
        png_bytes = await bridge.screenshot(screen=screen)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return Image(data=png_bytes, format="png")


@mcp.tool()
async def frame_number() -> dict[str, int]:
    """Return the current video frame count.

    When to use: to measure elapsed emulated time, or to verify that
    run_for_frames advanced the frame counter as expected.

    Returns: {"frame": int}.
    """
    bridge = session.require()
    try:
        frame = await bridge.frame_number()
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {"frame": frame}
