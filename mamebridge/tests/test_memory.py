"""Tests for memory.lua handlers."""

from __future__ import annotations

import pytest
from mamebridge import MameBridge


pytestmark = pytest.mark.asyncio(loop_scope="module")


async def test_read_mem(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    data = await bridge.read_mem(pc, 16)
    assert isinstance(data, list)
    assert len(data) == 16
    assert all(0 <= b <= 255 for b in data)


async def test_read_mem_unit_size_2(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    data = await bridge.read_mem(pc, 4, unit_size=2)
    assert len(data) == 4
    assert all(0 <= w <= 0xFFFF for w in data)


async def test_read_mem_unit_size_4(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    data = await bridge.read_mem(pc, 2, unit_size=4)
    assert len(data) == 2
    assert all(0 <= d <= 0xFFFFFFFF for d in data)


async def test_write_mem(bridge: MameBridge) -> None:
    # Write to a RAM area — A7/SP is at 0x180000 for pm3585
    # Read current value, write it back, verify
    addr = 0x180000
    original = await bridge.read_mem(addr, 4)
    written = await bridge.write_mem(addr, original)
    assert written == 4
    readback = await bridge.read_mem(addr, 4)
    assert readback == original


async def test_search_mem(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    # Read first 2 bytes at PC, then search for that pattern
    pattern = await bridge.read_mem(pc, 2)
    matches = await bridge.search_mem(pc, 16, pattern)
    assert pc in matches


async def test_read_mem_bad_device(bridge: MameBridge) -> None:
    from mamebridge.errors import DeviceNotFound
    with pytest.raises(DeviceNotFound):
        await bridge.read_mem(0, 1, device=":badcpu")


async def test_read_mem_bad_space(bridge: MameBridge) -> None:
    from mamebridge.errors import InvalidAddressSpace
    with pytest.raises(InvalidAddressSpace):
        await bridge.read_mem(0, 1, space="nonexistent")


async def test_install_and_read_tap(bridge: MameBridge) -> None:
    # Install a read tap on the first few bytes of ROM
    pc = await bridge.read_reg("PC")
    tap = await bridge.install_read_tap(pc, 16)
    assert tap.tap_id > 0

    # Step to trigger reads
    await bridge.step()

    # Read the tap buffer
    buf = await bridge.read_tap_buffer(tap.tap_id)
    assert isinstance(buf.entries, list)
    assert buf.drops == 0

    # Remove tap
    await bridge.remove_tap(tap.tap_id)


async def test_remove_bad_tap(bridge: MameBridge) -> None:
    from mamebridge.errors import TapNotFound
    with pytest.raises(TapNotFound):
        await bridge.remove_tap(99999)
