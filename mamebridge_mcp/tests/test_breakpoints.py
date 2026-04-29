"""Tests for breakpoint and watchpoint tools."""

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


async def test_bp_set_and_list(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            pc = get_result(await client.call_tool("read_register", {"name": "PC"}))["value"]
            bp = get_result(await client.call_tool("bp_set", {"address": pc + 4}))
            bp_id = bp["id"]

            bps = get_result(await client.call_tool("bp_list", {}))["breakpoints"]
            ids = [b["id"] for b in bps]
            assert bp_id in ids
        finally:
            await client.call_tool("stop_session", {})


async def test_bp_clear(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            pc = get_result(await client.call_tool("read_register", {"name": "PC"}))["value"]
            bp = get_result(await client.call_tool("bp_set", {"address": pc + 4}))
            bp_id = bp["id"]

            get_result(await client.call_tool("bp_clear", {"id": bp_id}))

            bps = get_result(await client.call_tool("bp_list", {}))["breakpoints"]
            ids = [b["id"] for b in bps]
            assert bp_id not in ids
        finally:
            await client.call_tool("stop_session", {})


async def test_bp_clear_nonexistent_is_silent(mame_binary):
    """Clearing a nonexistent breakpoint ID should not raise an error."""
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("bp_clear", {"id": 99999})
            # MAME silently ignores unknown bp IDs — this should succeed
            get_result(result)
        finally:
            await client.call_tool("stop_session", {})


async def test_bp_enable_disable(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            pc = get_result(await client.call_tool("read_register", {"name": "PC"}))["value"]
            bp = get_result(await client.call_tool("bp_set", {"address": pc + 4}))
            bp_id = bp["id"]

            get_result(await client.call_tool("bp_enable", {"id": bp_id, "enabled": False}))

            bps = get_result(await client.call_tool("bp_list", {}))["breakpoints"]
            entry = next((b for b in bps if b["id"] == bp_id), None)
            assert entry is not None
            assert entry["enabled"] is False

            get_result(await client.call_tool("bp_enable", {"id": bp_id, "enabled": True}))

            bps2 = get_result(await client.call_tool("bp_list", {}))["breakpoints"]
            entry2 = next((b for b in bps2 if b["id"] == bp_id), None)
            assert entry2["enabled"] is True
        finally:
            await client.call_tool("stop_session", {})


async def test_wp_set_and_list(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            wp = get_result(await client.call_tool("wp_set", {
                "address": "0x10000", "length": 4, "type": "w",
            }))
            wp_id = wp["id"]

            wps = get_result(await client.call_tool("wp_list", {}))["watchpoints"]
            ids = [w["id"] for w in wps]
            assert wp_id in ids
        finally:
            await client.call_tool("stop_session", {})


async def test_wp_clear(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            wp = get_result(await client.call_tool("wp_set", {
                "address": "0x10000", "length": 4, "type": "rw",
            }))
            wp_id = wp["id"]

            get_result(await client.call_tool("wp_clear", {"id": wp_id}))

            wps = get_result(await client.call_tool("wp_list", {}))["watchpoints"]
            ids = [w["id"] for w in wps]
            assert wp_id not in ids
        finally:
            await client.call_tool("stop_session", {})


async def test_wp_enable_disable(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            wp = get_result(await client.call_tool("wp_set", {
                "address": "0x10000", "length": 4,
            }))
            wp_id = wp["id"]

            get_result(await client.call_tool("wp_enable", {"id": wp_id, "enabled": False}))
            wps = get_result(await client.call_tool("wp_list", {}))["watchpoints"]
            entry = next((w for w in wps if w["id"] == wp_id), None)
            assert entry is not None
            assert entry["enabled"] is False

            get_result(await client.call_tool("wp_enable", {"id": wp_id, "enabled": True}))
            wps2 = get_result(await client.call_tool("wp_list", {}))["watchpoints"]
            entry2 = next((w for w in wps2 if w["id"] == wp_id), None)
            assert entry2["enabled"] is True
        finally:
            await client.call_tool("stop_session", {})
