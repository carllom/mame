"""Tests for session lifecycle tools: start_session, attach_session,
stop_session, session_status.
"""

from __future__ import annotations

import pytest

from mamebridge_mcp.tests.conftest import (
    MAME_DRIVER,
    MAME_PORT,
    MAME_ATTACH_PORT,
    bare_mame,
    get_error,
    get_result,
    make_mcp_client,
)


pytestmark = pytest.mark.asyncio


# ── no-session behavior ──────────────────────────────────────────────────────


async def test_requires_session_raises_when_no_session():
    """_test_requires_session must error when no session is active."""
    async with make_mcp_client() as client:
        result = await client.call_tool("_test_requires_session", {})
        assert result.isError
        msg = get_error(result)
        assert "No active MAME session" in msg


async def test_session_status_no_session():
    """session_status returns alive=false when no session is active."""
    async with make_mcp_client() as client:
        result = await client.call_tool("session_status", {})
        data = get_result(result)
        assert data["alive"] is False
        assert data["driver"] is None


# ── start_session / status / stop_session ────────────────────────────────────


async def test_start_stop_session(mame_binary):
    """start_session launches MAME, status reflects it, stop_session tears down."""
    async with make_mcp_client() as client:
        result = await client.call_tool(
            "start_session",
            {
                "driver": MAME_DRIVER,
                "flags": ["-debug", "-window", "-nomaximize"],
                "port": MAME_PORT,
                "mame_binary": mame_binary,
            },
        )
        data = get_result(result)
        assert data["driver"] == MAME_DRIVER
        assert data["exec_state"] == "stop"  # -debug starts paused

        status = get_result(await client.call_tool("session_status", {}))
        assert status["alive"] is True
        assert status["driver"] == MAME_DRIVER
        assert status["exec_state"] == "stop"
        assert status["attached"] is False

        stop = get_result(await client.call_tool("stop_session", {}))
        assert stop["stopped"] is True

        after = get_result(await client.call_tool("session_status", {}))
        assert after["alive"] is False


async def test_start_session_twice_fails(mame_binary):
    """Starting a second session without stopping first raises a ToolError."""
    async with make_mcp_client() as client:
        await client.call_tool(
            "start_session",
            {
                "driver": MAME_DRIVER,
                "flags": ["-debug", "-window", "-nomaximize"],
                "port": MAME_PORT,
                "mame_binary": mame_binary,
            },
        )
        try:
            result = await client.call_tool(
                "start_session",
                {
                    "driver": MAME_DRIVER,
                    "flags": ["-debug", "-window", "-nomaximize"],
                    "port": MAME_PORT + 2,
                    "mame_binary": mame_binary,
                },
            )
            assert result.isError
            assert "already active" in get_error(result)
        finally:
            await client.call_tool("stop_session", {})


async def test_stop_session_when_none_is_safe():
    """stop_session when no session is active returns stopped=true without error."""
    async with make_mcp_client() as client:
        result = await client.call_tool("stop_session", {})
        data = get_result(result)
        assert data["stopped"] is True


# ── attach_session ───────────────────────────────────────────────────────────


async def test_attach_session(mame_binary):
    """attach_session connects to an already-running MAME without owning it.

    After stop_session, MAME should remain alive (the bare_mame context
    manager would raise if it exited unexpectedly).
    """
    async with bare_mame(mame_binary, port=MAME_ATTACH_PORT):
        async with make_mcp_client() as client:
            result = await client.call_tool(
                "attach_session",
                {"host": "127.0.0.1", "port": MAME_ATTACH_PORT},
            )
            data = get_result(result)
            assert data["exec_state"] in ("run", "stop")
            assert data["port"] == MAME_ATTACH_PORT

            status = get_result(await client.call_tool("session_status", {}))
            assert status["alive"] is True
            assert status["attached"] is True

            stop = get_result(await client.call_tool("stop_session", {}))
            assert stop["stopped"] is True

            # After detach, status is dead.
            after = get_result(await client.call_tool("session_status", {}))
            assert after["alive"] is False
            # MAME is still up — bare_mame context exits cleanly (no exception).


async def test_requires_session_succeeds_when_active(mame_binary):
    """_test_requires_session returns ok=true when a session is live."""
    async with make_mcp_client() as client:
        await client.call_tool(
            "start_session",
            {
                "driver": MAME_DRIVER,
                "flags": ["-debug", "-window", "-nomaximize"],
                "port": MAME_PORT,
                "mame_binary": mame_binary,
            },
        )
        try:
            result = await client.call_tool("_test_requires_session", {})
            data = get_result(result)
            assert data["ok"] is True
        finally:
            await client.call_tool("stop_session", {})
