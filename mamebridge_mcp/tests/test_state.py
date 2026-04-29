"""Tests for state tools: save_state, load_state, screenshot, frame_number."""

from __future__ import annotations

import pytest

from mamebridge_mcp.tests.conftest import (
    MAME_DRIVER,
    MAME_PORT,
    get_result,
    make_mcp_client,
)


pytestmark = pytest.mark.asyncio


async def test_frame_number(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("frame_number", {})
            data = get_result(result)
            assert isinstance(data["frame"], int)
            assert data["frame"] >= 0
        finally:
            await client.call_tool("stop_session", {})


async def test_save_and_load_state(mame_binary):
    """Save and load state round-trip completes without errors."""
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            save = get_result(await client.call_tool("save_state", {"name": "pytest_save"}))
            assert save["name"] == "pytest_save"

            # Advance a few instructions so machine state differs
            get_result(await client.call_tool("step", {"count": 5}))

            # load_state schedules the restore for the next frame — just check no error
            load = get_result(await client.call_tool("load_state", {"name": "pytest_save"}))
            assert load["name"] == "pytest_save"
        finally:
            await client.call_tool("stop_session", {})


async def test_screenshot_returns_image(mame_binary):
    """screenshot should return an MCP ImageContent with PNG data."""
    from mcp.types import ImageContent

    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("screenshot", {})
            assert not result.isError
            content = result.content
            assert len(content) >= 1
            assert isinstance(content[0], ImageContent)
            assert content[0].mimeType == "image/png"
            assert len(content[0].data) > 0
        finally:
            await client.call_tool("stop_session", {})
