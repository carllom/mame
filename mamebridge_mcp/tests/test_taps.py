"""Tests for tap tools: install_read_tap, install_write_tap, read_tap_buffer,
remove_tap, tap_and_run, find_writes_to, find_reads_of.
"""

from __future__ import annotations

import pytest

from mamebridge_mcp.tests.conftest import (
    MAME_DRIVER,
    MAME_PORT,
    get_result,
    make_mcp_client,
)


pytestmark = pytest.mark.asyncio


async def test_install_read_tap_and_remove(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            tap = get_result(await client.call_tool("install_read_tap", {
                "address": "0x0", "length": 16,
            }))
            assert "tap_id" in tap
            tap_id = tap["tap_id"]

            remove = get_result(await client.call_tool("remove_tap", {"tap_id": tap_id}))
            assert remove["removed"] is True
        finally:
            await client.call_tool("stop_session", {})


async def test_install_write_tap_and_read_buffer(mame_binary):
    """Write to memory, verify tap captures the access."""
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            # Watch a RAM address
            tap = get_result(await client.call_tool("install_write_tap", {
                "address": "0x10000", "length": 4,
            }))
            tap_id = tap["tap_id"]

            # Generate a write by running a few frames (boot writes to RAM)
            get_result(await client.call_tool("run_for_frames", {"frames": 3, "timeout": 30.0}))

            buf = get_result(await client.call_tool("read_tap_buffer", {"tap_id": tap_id}))
            assert "entries" in buf
            assert "drops" in buf
            assert "remaining" in buf

            get_result(await client.call_tool("remove_tap", {"tap_id": tap_id}))
        finally:
            await client.call_tool("stop_session", {})


async def test_tap_and_run_write(mame_binary):
    """tap_and_run returns entries + drops + frames_run."""
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("tap_and_run", {
                "address": "0x10000", "length": 64, "frames": 3, "kind": "write",
            })
            data = get_result(result)
            assert "entries" in data
            assert "drops" in data
            assert "frames_run" in data
            assert isinstance(data["entries"], list)
            assert data["frames_run"] >= 3
        finally:
            await client.call_tool("stop_session", {})


async def test_find_writes_to(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("find_writes_to", {
                "address": "0x10000", "length": 64, "frames": 3,
            })
            data = get_result(result)
            assert "entries" in data
            assert "drops" in data
            assert "frames_run" in data
        finally:
            await client.call_tool("stop_session", {})


async def test_find_reads_of(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("find_reads_of", {
                "address": "0x0", "length": 8, "frames": 3,
            })
            data = get_result(result)
            assert "entries" in data
            assert "drops" in data
        finally:
            await client.call_tool("stop_session", {})


async def test_tap_and_run_invalid_kind(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            from mamebridge_mcp.tests.conftest import get_error
            result = await client.call_tool("tap_and_run", {
                "address": "0x0", "length": 4, "frames": 1, "kind": "execute",
            })
            err = get_error(result)
            assert err
        finally:
            await client.call_tool("stop_session", {})
