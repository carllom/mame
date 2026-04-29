"""Tests for event delivery."""

from __future__ import annotations

import asyncio
import pytest
from mamebridge import MameBridge


pytestmark = pytest.mark.asyncio(loop_scope="module")


async def test_concurrent_subscribers(bridge: MameBridge) -> None:
    """Two subscribers to the same event type both receive each event."""
    pc0 = await bridge.read_reg("PC")
    pcs = [pc0]
    for _ in range(5):
        pcs.append(await bridge.step())

    target = pcs[3]
    await bridge.write_reg("PC", pc0)

    bp = await bridge.bp_set(target)
    sub1 = bridge.events("event.breakpoint")
    sub2 = bridge.events("event.breakpoint")

    try:
        await bridge.run()
        e1 = await asyncio.wait_for(sub1.__anext__(), timeout=5.0)
        e2 = await asyncio.wait_for(sub2.__anext__(), timeout=5.0)
        assert e1.params["pc"] == target
        assert e2.params["pc"] == target
    finally:
        sub1.close()
        sub2.close()
        await bridge.bp_clear(bp.id)
