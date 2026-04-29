"""Tests for inspection tools: list_devices, list_address_spaces, list_registers,
get_driver_info, current_state, summarize_memory_map.
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


async def test_list_devices(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("list_devices", {})
            devices = get_result(result)["devices"]
            assert isinstance(devices, list)
            assert len(devices) > 0
            tags = [d.get("tag") for d in devices]
            assert ":maincpu" in tags
        finally:
            await client.call_tool("stop_session", {})


async def test_list_address_spaces(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("list_address_spaces", {"device": ":maincpu"})
            spaces = get_result(result)["spaces"]
            assert isinstance(spaces, list)
            names = [s.get("name") for s in spaces]
            assert "program" in names
        finally:
            await client.call_tool("stop_session", {})


async def test_list_registers(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("list_registers", {})
            regs = get_result(result)["registers"]
            assert isinstance(regs, list)
            assert len(regs) > 0
            for r in regs:
                assert "name" in r
                assert "value" in r
        finally:
            await client.call_tool("stop_session", {})


async def test_get_driver_info(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("get_driver_info", {})
            info = get_result(result)
            assert info["shortname"] == MAME_DRIVER
            assert "description" in info
            assert "flags" in info
            assert isinstance(info["flags"], dict)
            assert len(info["flags"]) > 0
            for key, val in info["flags"].items():
                assert isinstance(key, str)
                assert isinstance(val, bool)
        finally:
            await client.call_tool("stop_session", {})


async def test_current_state(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("current_state", {})
            state = get_result(result)
            assert state["exec_state"] == "stop"
            assert state["driver"] == MAME_DRIVER
            assert isinstance(state["frame"], int)
            assert "pc" in state
        finally:
            await client.call_tool("stop_session", {})


async def test_summarize_memory_map(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("summarize_memory_map", {})
            summary = get_result(result)
            assert "devices" in summary
            tags = [d["tag"] for d in summary["devices"]]
            assert ":maincpu" in tags
            maincpu = next(d for d in summary["devices"] if d["tag"] == ":maincpu")
            space_names = [s["name"] for s in maincpu["spaces"]]
            assert "program" in space_names
        finally:
            await client.call_tool("stop_session", {})
