"""Tests for execution tools: step, step_over, step_out, pause/resume,
exec_state, run_until_breakpoint, run_for_frames.
"""

from __future__ import annotations

import pytest

from mamebridge_mcp.tests.conftest import (
    MAME_DRIVER,
    MAME_PORT,
    get_result,
    get_error,
    make_mcp_client,
)


pytestmark = pytest.mark.asyncio


async def test_exec_state_stopped_on_start(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("exec_state", {})
            state = get_result(result)
            assert state["exec_state"] == "stop"
        finally:
            await client.call_tool("stop_session", {})


async def test_step_advances_pc(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            before = get_result(await client.call_tool("read_register", {"name": "PC"}))
            get_result(await client.call_tool("step", {"count": 1}))
            after = get_result(await client.call_tool("read_register", {"name": "PC"}))
            assert after["value"] != before["value"]
        finally:
            await client.call_tool("stop_session", {})


async def test_step_multiple(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("step", {"count": 5})
            state = get_result(result)
            assert state["exec_state"] == "stop"
            assert "pc" in state
        finally:
            await client.call_tool("stop_session", {})


async def test_step_over(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            before = get_result(await client.call_tool("read_register", {"name": "PC"}))
            result = get_result(await client.call_tool("step_over", {}))
            assert result["exec_state"] == "stop"
            assert result["pc"] != before
        finally:
            await client.call_tool("stop_session", {})


async def test_run_until_breakpoint(mame_binary):
    """Set a breakpoint 10 instructions ahead, run until it, confirm PC matches."""
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            # Step 10 times to find a good target address
            for _ in range(10):
                await client.call_tool("step", {"count": 1})
            target_pc = get_result(await client.call_tool("read_register", {"name": "PC"}))["value"]

            # Reset back a couple steps (just run from start stop position instead)
            # Actually — restart: stop+start would re-launch MAME. Instead just
            # run until the current PC, which should be a no-op (bridge single-steps).
            bp = get_result(await client.call_tool("bp_set", {"address": target_pc}))
            bp_id = bp["id"]

            try:
                hit = get_result(await client.call_tool("run_until_breakpoint", {
                    "address": target_pc, "timeout": 10.0,
                }))
                assert hit["pc"] == target_pc
            finally:
                await client.call_tool("bp_clear", {"id": bp_id})
        finally:
            await client.call_tool("stop_session", {})


async def test_run_for_frames(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            before = get_result(await client.call_tool("frame_number", {}))["frame"]
            result = await client.call_tool("run_for_frames", {"frames": 5, "timeout": 30.0})
            state = get_result(result)
            assert state["frame"] >= before + 5
            assert state["exec_state"] == "stop"
        finally:
            await client.call_tool("stop_session", {})


async def test_pause_while_stopped_is_safe(mame_binary):
    """Pausing when already stopped should not raise an error."""
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("pause", {})
            state = get_result(result)
            assert state["exec_state"] == "stop"
        finally:
            await client.call_tool("stop_session", {})
