"""Tests for machine.lua handlers."""

from __future__ import annotations

import pytest
from mamebridge import MameBridge


pytestmark = pytest.mark.asyncio(loop_scope="module")


async def test_ping(bridge: MameBridge) -> None:
    result = await bridge.ping()
    assert result["pong"] is True
    assert result["api_version"] == 1
    assert "mame" in result["mame_version"].lower()


async def test_list_devices(bridge: MameBridge) -> None:
    devices = await bridge.list_devices()
    assert isinstance(devices, list)
    assert len(devices) > 0
    tags = [d["tag"] for d in devices]
    assert ":maincpu" in tags


async def test_list_spaces(bridge: MameBridge) -> None:
    spaces = await bridge.list_spaces(":maincpu")
    assert isinstance(spaces, list)
    assert len(spaces) > 0
    names = [s["name"] for s in spaces]
    assert "program" in names


async def test_list_spaces_bad_device(bridge: MameBridge) -> None:
    from mamebridge.errors import DeviceNotFound
    with pytest.raises(DeviceNotFound):
        await bridge.list_spaces(":nonexistent")


async def test_driver_info(bridge: MameBridge) -> None:
    info = await bridge.driver_info()
    assert info.shortname == "pm3585"
    assert info.manufacturer is not None
    assert info.year is not None


async def test_list_handlers(bridge: MameBridge) -> None:
    handlers = await bridge.list_handlers()
    assert isinstance(handlers, list)
    assert "ping" in handlers
    assert "read_mem" in handlers
    assert "bp_set" in handlers
    assert "list_handlers" in handlers
    assert len(handlers) >= 30


async def test_method_not_found(bridge: MameBridge) -> None:
    from mamebridge.errors import MethodNotFound
    with pytest.raises(MethodNotFound):
        await bridge.call("nonexistent_method")
