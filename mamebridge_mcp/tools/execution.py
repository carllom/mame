"""Execution control tools: step, step_over, step_out, pause, resume, exec_state."""

from __future__ import annotations

from typing import Any

from mamebridge.errors import MameBridgeError
from mamebridge_mcp._app import mcp
from mamebridge_mcp.errors import raise_as_tool_error
from mamebridge_mcp.schemas import coerce_address
from mamebridge_mcp.session import session


@mcp.tool()
async def step(count: int = 1, device: str = ":maincpu") -> dict[str, Any]:
    """Step the CPU by one or more instructions, then return the new PC.

    Execution must be stopped (exec_state='stop') before calling this.

    Args:
        count: Number of instructions to step (default 1).
        device: Device tag (default ':maincpu').

    Returns: {"exec_state": "stop", "pc": int}.
    """
    bridge = session.require()
    try:
        pc = await bridge.step(count, device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"exec_state": "stop", "pc": pc}


@mcp.tool()
async def step_over(device: str = ":maincpu") -> dict[str, Any]:
    """Step over one instruction (treats calls as single steps), then return PC.

    Unlike step, step_over does not enter subroutines — it sets a breakpoint
    after the current instruction and runs to it. Execution must be stopped.

    Args:
        device: Device tag (default ':maincpu').

    Returns: {"exec_state": "stop", "pc": int}.
    """
    import asyncio

    bridge = session.require()
    try:
        await bridge.step_over(device=device)
        # step_over fires MAME's async 'over' command — poll until stopped
        for _ in range(200):
            await asyncio.sleep(0.025)
            state = await bridge.exec_state()
            if state == "stop":
                break
        pc = await bridge.read_reg("PC", device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"exec_state": "stop", "pc": pc}


@mcp.tool()
async def step_out(device: str = ":maincpu") -> dict[str, Any]:
    """Run until the current subroutine returns, then return PC.

    Execution must be stopped inside a subroutine.

    Args:
        device: Device tag (default ':maincpu').

    Returns: {"exec_state": "stop", "pc": int}.
    """
    import asyncio

    bridge = session.require()
    try:
        await bridge.step_out(device=device)
        # step_out fires MAME's async 'out' command — poll until stopped
        for _ in range(400):
            await asyncio.sleep(0.025)
            state = await bridge.exec_state()
            if state == "stop":
                break
        pc = await bridge.read_reg("PC", device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"exec_state": "stop", "pc": pc}


@mcp.tool()
async def pause() -> dict[str, Any]:
    """Pause execution if it is running.

    Safe to call when already paused (no-op). Use this to stop a running
    system so you can inspect state.

    Returns: {"exec_state": "stop", "pc": int}.
    """
    bridge = session.require()
    try:
        await bridge.pause()
        pc = await bridge.read_reg("PC")
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {"exec_state": "stop", "pc": pc}


@mcp.tool()
async def resume() -> dict[str, str]:
    """Resume execution if it is paused.

    Returns immediately — execution continues asynchronously. Use
    run_until_breakpoint if you want to wait for a specific address.

    Returns: {"exec_state": "run"}.
    """
    bridge = session.require()
    try:
        await bridge.run()
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {"exec_state": "run"}


@mcp.tool()
async def exec_state() -> dict[str, str]:
    """Return the current execution state without changing anything.

    Returns: {"exec_state": "run" | "stop"}.

    When to use: as a quick sanity check. Prefer current_state() if you
    also need PC and frame number.
    """
    bridge = session.require()
    try:
        state = await bridge.exec_state()
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {"exec_state": state}


@mcp.tool()
async def run_until_breakpoint(
    address: int | str,
    device: str = ":maincpu",
    timeout: float = 10.0,
) -> dict[str, Any]:
    """Set a one-shot breakpoint, run, wait for the hit, then return PC.

    The preferred way to run to a specific address. Handles the edge case
    where PC is already at the target (steps one instruction first, then
    sets the breakpoint). Cleans up the breakpoint automatically on hit or
    timeout.

    Args:
        address: Target address. Accepts '0x1234', '1234h', or decimal.
        device: Device tag (default ':maincpu').
        timeout: Seconds to wait for the breakpoint hit (default 10.0). Raise
                 this if the target is deep in a long loop.

    Returns: {"pc": int, "stopped": true}.

    Raises a tool error on timeout — the CPU never reached the target address
    within the timeout period.
    """
    bridge = session.require()
    addr = coerce_address(address)
    try:
        pc = await bridge.run_until(addr, device=device, timeout=timeout)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, address=addr)
    return {"pc": pc, "stopped": True}


@mcp.tool()
async def run_for_frames(frames: int, timeout: float | None = None) -> dict[str, Any]:
    """Resume execution for approximately N video frames, then pause.

    Useful for advancing the emulation a fixed amount of time (e.g. to let
    boot code run, or to gather tap data over a short window) without
    specifying a target address.

    Frame counts are at the screen's native rate (~50/60 Hz). For 1 second
    of emulated time, pass frames=60.

    Args:
        frames: Number of frames to advance.
        timeout: Maximum wall-clock seconds to wait (default frames*0.05 + 5.0).

    Returns: {"frame": int, "exec_state": "stop"}.
    """
    import asyncio

    bridge = session.require()
    if timeout is None:
        timeout = frames * 0.05 + 5.0

    try:
        start_frame = await bridge.frame_number()
        target_frame = start_frame + frames
        await bridge.run()

        deadline = asyncio.get_event_loop().time() + timeout
        while asyncio.get_event_loop().time() < deadline:
            await asyncio.sleep(0.05)
            current = await bridge.frame_number()
            if current >= target_frame:
                break

        await bridge.pause()
        final_frame = await bridge.frame_number()
    except MameBridgeError as exc:
        raise_as_tool_error(exc)

    return {"frame": final_frame, "exec_state": "stop"}
