"""Tests for cpu.lua handlers."""

from __future__ import annotations

import pytest
from mamebridge import MameBridge


pytestmark = pytest.mark.asyncio(loop_scope="module")


async def test_read_reg(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    assert isinstance(pc, int)
    assert pc >= 0


async def test_read_reg_bad_name(bridge: MameBridge) -> None:
    from mamebridge.errors import InvalidParams
    with pytest.raises(InvalidParams):
        await bridge.read_reg("NONEXISTENT_REG")


async def test_write_reg(bridge: MameBridge) -> None:
    original_pc = await bridge.read_reg("PC")
    # Write a known value to D0, then read it back
    await bridge.write_reg("D0", 0x12345678)
    val = await bridge.read_reg("D0")
    assert val == 0x12345678
    # Restore
    await bridge.write_reg("D0", 0)


async def test_list_regs(bridge: MameBridge) -> None:
    regs = await bridge.list_regs()
    assert isinstance(regs, list)
    assert len(regs) > 0
    names = [r.name for r in regs]
    assert "PC" in names
    assert "D0" in names
    assert "A0" in names
    assert "SR" in names


async def test_disassemble(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    lines = await bridge.disassemble(pc, 5)
    assert isinstance(lines, list)
    assert len(lines) > 0
    assert lines[0].address == pc
    assert len(lines[0].text) > 0
