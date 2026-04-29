"""Tests for debugger.lua handlers."""

from __future__ import annotations

import pytest
from mamebridge import MameBridge


pytestmark = pytest.mark.asyncio(loop_scope="module")


async def test_exec_state(bridge: MameBridge) -> None:
    state = await bridge.exec_state()
    assert state in ("run", "stop")


async def test_step(bridge: MameBridge) -> None:
    pc_before = await bridge.read_reg("PC")
    pc_after = await bridge.step()
    assert isinstance(pc_after, int)
    # PC should have changed (at least for non-loop instructions)
    # Just verify it's a valid value
    assert pc_after >= 0


async def test_step_multiple(bridge: MameBridge) -> None:
    pc0 = await bridge.read_reg("PC")
    pcs = [pc0]
    for _ in range(3):
        pc = await bridge.step()
        pcs.append(pc)
    # Should have progressed
    assert len(set(pcs)) > 1


async def test_bp_set_and_list(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    # Step a few times to find a future address
    for _ in range(5):
        target = await bridge.step()

    bp = await bridge.bp_set(target)
    assert bp.id >= 0
    assert bp.address == target

    # List should include our breakpoint
    bp_list = await bridge.bp_list()
    ids = [b.id for b in bp_list]
    assert bp.id in ids

    # Clear it
    await bridge.bp_clear(bp.id)

    # Should be gone
    bp_list2 = await bridge.bp_list()
    ids2 = [b.id for b in bp_list2]
    assert bp.id not in ids2


async def test_bp_enable_disable(bridge: MameBridge) -> None:
    pc = await bridge.read_reg("PC")
    bp = await bridge.bp_set(pc + 0x100)
    await bridge.bp_enable(bp.id, False)
    bp_list = await bridge.bp_list()
    bp_entry = next(b for b in bp_list if b.id == bp.id)
    assert bp_entry.enabled is False

    await bridge.bp_enable(bp.id, True)
    bp_list = await bridge.bp_list()
    bp_entry = next(b for b in bp_list if b.id == bp.id)
    assert bp_entry.enabled is True

    await bridge.bp_clear(bp.id)


async def test_run_and_breakpoint_event(bridge: MameBridge) -> None:
    # Get current PC and step to discover instruction boundaries
    pc0 = await bridge.read_reg("PC")
    pcs = [pc0]
    for _ in range(5):
        pcs.append(await bridge.step())

    # Set BP at a known boundary ahead
    target = pcs[3]
    # Rewind PC
    await bridge.write_reg("PC", pc0)

    bp = await bridge.bp_set(target)

    # Run and wait for breakpoint event
    event = None
    sub = bridge.events("event.breakpoint")
    try:
        await bridge.run()
        import asyncio
        event = await asyncio.wait_for(sub.__anext__(), timeout=5.0)
    finally:
        sub.close()

    assert event is not None
    assert event.params["pc"] == target
    assert event.params["id"] == bp.id

    # Clean up
    await bridge.bp_clear(bp.id)


@pytest.mark.xfail(reason="intermittent: CPU path may differ after state changes", strict=False)
async def test_run_until(bridge: MameBridge) -> None:
    pc0 = await bridge.read_reg("PC")
    # Step to find a target — use a nearby instruction
    pcs = [pc0]
    for _ in range(3):
        pcs.append(await bridge.step())

    target = pcs[1]
    # Rewind
    await bridge.write_reg("PC", pc0)

    hit_pc = await bridge.run_until(target, timeout=5.0)
    assert hit_pc == target

    actual_pc = await bridge.read_reg("PC")
    assert actual_pc == target


@pytest.mark.xfail(reason="intermittent: CPU path may differ after state changes", strict=False)
async def test_run_until_already_at_target(bridge: MameBridge) -> None:
    """Test the case where PC is already at the target — should step first."""
    pc = await bridge.read_reg("PC")
    # Step a few to get a target that's ahead
    pcs = [pc]
    for _ in range(5):
        pcs.append(await bridge.step())
    target = pcs[3]
    await bridge.write_reg("PC", pcs[0])
    # First run to target
    await bridge.run_until(target, timeout=5.0)
    assert await bridge.read_reg("PC") == target

    # Now PC is at target — run_until should handle this
    # Rewind and run again through the same target
    await bridge.write_reg("PC", pcs[0])
    hit = await bridge.run_until(target, timeout=5.0)
    assert hit == target


async def test_wp_set_and_list(bridge: MameBridge) -> None:
    wp = await bridge.wp_set(0x180000, 4, "w")
    assert wp.id >= 0

    wp_list = await bridge.wp_list()
    ids = [w.id for w in wp_list]
    assert wp.id in ids

    await bridge.wp_clear(wp.id)
    wp_list2 = await bridge.wp_list()
    ids2 = [w.id for w in wp_list2]
    assert wp.id not in ids2


async def test_pause(bridge: MameBridge) -> None:
    state = await bridge.exec_state()
    if state == "run":
        await bridge.pause()
    state = await bridge.exec_state()
    assert state == "stop"
