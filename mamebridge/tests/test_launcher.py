"""Tests for launcher module."""

from __future__ import annotations

import pytest
from mamebridge import launch_mame, MameDied
import os

MAME_BINARY = os.environ.get("MAME_BINARY", "./mametinyd")
MAME_DRIVER = os.environ.get("MAME_DRIVER", "pm3585")


pytestmark = pytest.mark.asyncio


async def test_launcher_bad_driver() -> None:
    """Launching with a non-existent driver should raise MameDied."""
    with pytest.raises((MameDied, Exception)):
        async with launch_mame(
            driver="nonexistent_driver_xyz",
            mame_binary=MAME_BINARY,
            extra_flags=["-window", "-nomaximize"],
            port=8099,
            startup_timeout=10.0,
        ) as _bridge:
            pass
