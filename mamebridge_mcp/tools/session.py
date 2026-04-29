"""Session management tools: start, attach, stop, status."""

from __future__ import annotations

from typing import Any

from mamebridge_mcp._app import mcp
from mamebridge_mcp.session import session


@mcp.tool()
async def start_session(
    driver: str,
    flags: list[str] | None = None,
    port: int | None = None,
    mame_binary: str = "mame",
    rom_path: str | None = None,
) -> dict[str, Any]:
    """Launch a MAME instance with the bridge plugin and connect to it.

    When to use: at the start of a debugging session, when you want the
    server to own the MAME process. If MAME is already running and you
    launched it yourself, use attach_session instead.

    The bridge auto-passes -plugin mcp and -skip_gameinfo. You should
    usually pass flags=["-debug", "-window", "-nomaximize"]; -debug is
    required for breakpoints, watchpoints, and step control.

    Args:
        driver: MAME driver name, e.g. "pm3585" or "cdi".
        flags: Extra MAME command-line flags. Pass ["-debug", "-window",
               "-nomaximize"] for an interactive debug session.
        port: Bridge TCP port (default 8100).
        mame_binary: Path to the MAME executable (default "mame").
        rom_path: Override for the ROM search path.

    Returns: {"driver", "port", "frame", "exec_state"}.
    """
    await session.start(
        driver,
        flags=flags,
        port=port,
        mame_binary=mame_binary,
        rom_path=rom_path,
    )
    st = await session.status()
    return {
        "driver": st["driver"],
        "port": port or 8100,
        "frame": st["frame"],
        "exec_state": st["exec_state"],
    }


@mcp.tool()
async def attach_session(
    host: str = "127.0.0.1",
    port: int = 8100,
) -> dict[str, Any]:
    """Connect to a MAME instance that is already running with the bridge plugin.

    When to use: when you launched MAME yourself (e.g. from a shell with
    -console for interactive debugging) and want to drive it from here.
    The server does NOT own the process — stop_session will close the
    connection but will not terminate MAME.

    MAME must have been started with -plugin mcp. The bridge listens on
    localhost:8100 by default; set MAME_MCP_PORT to override.

    Args:
        host: Bridge host (default "127.0.0.1").
        port: Bridge TCP port (default 8100).

    Returns: {"driver", "port", "frame", "exec_state"}.
    """
    await session.attach(host, port)
    st = await session.status()
    return {
        "driver": st["driver"],
        "port": port,
        "frame": st["frame"],
        "exec_state": st["exec_state"],
    }


@mcp.tool()
async def stop_session() -> dict[str, bool]:
    """Stop the active session and clean up resources.

    If the session was started with start_session, MAME is terminated.
    If it was started with attach_session, only the connection is closed
    and MAME keeps running.

    Safe to call even if no session is active (returns {"stopped": true}).

    Returns: {"stopped": true}.
    """
    await session.stop()
    return {"stopped": True}


@mcp.tool()
async def session_status() -> dict[str, Any]:
    """Return the current session state without changing anything.

    When to use: to check whether a session is live, what driver is
    running, where execution is paused, and what the current frame is.
    Safe to call at any time.

    Returns:
        alive: false if no session is active.
        driver: MAME driver short name (e.g. "pm3585").
        attached: true if connected via attach_session (server doesn't own MAME).
        frame: current video frame count.
        exec_state: "run" or "stop" (only meaningful when alive=true).
        pc: current program counter as an integer (only when exec_state="stop").
    """
    return await session.status()


@mcp.tool()
async def _test_requires_session() -> dict[str, bool]:
    """Internal tool: verifies that session.require() raises when no session is active.

    Not intended for agent use. Returns {"ok": true} when a session is live,
    or raises a ToolError when no session is active.
    """
    session.require()
    return {"ok": True}
