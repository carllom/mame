"""Tests for state.lua handlers and raw.lua."""

from __future__ import annotations

import pytest
from mamebridge import MameBridge


pytestmark = pytest.mark.asyncio(loop_scope="module")


async def test_frame_number(bridge: MameBridge) -> None:
    frame = await bridge.frame_number()
    assert isinstance(frame, int)
    assert frame >= 0


async def test_screenshot(bridge: MameBridge) -> None:
    png_data = await bridge.screenshot()
    assert isinstance(png_data, bytes)
    # PNG magic number
    assert png_data[:4] == b"\x89PNG"


async def test_save_and_load_state(bridge: MameBridge) -> None:
    name = await bridge.save_state("test_save")
    assert name == "test_save"
    # Load it back
    await bridge.load_state("test_save")
    # Should not raise


async def test_exec_command(bridge: MameBridge) -> None:
    output = await bridge.exec_command("print 1+1")
    assert "2" in output


async def test_exec_command_disassemble(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    output = await bridge.exec_command(f"u {pc:x},3")
    assert len(output) > 0
