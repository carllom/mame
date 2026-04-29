"""Shared fixtures for mamebridge tests."""

from __future__ import annotations

import asyncio
import os
import pytest
import pytest_asyncio

from mamebridge import MameBridge, launch_mame


MAME_BINARY = os.environ.get("MAME_BINARY", "./mametinyd")
MAME_DRIVER = os.environ.get("MAME_DRIVER", "pm3585")
MAME_PORT = int(os.environ.get("MAME_MCP_PORT", "8080"))


@pytest_asyncio.fixture(scope="module", loop_scope="module")
async def bridge():
    """Launch MAME with the mcp plugin and yield a connected bridge.

    Shared across all tests in a module.
    """
    async with launch_mame(
        driver=MAME_DRIVER,
        mame_binary=MAME_BINARY,
        extra_flags=["-debug", "-window", "-nomaximize"],
        port=MAME_PORT,
        startup_timeout=30.0,
    ) as b:
        yield b


@pytest_asyncio.fixture(scope="module")
async def bridge_no_debug():
    """Launch MAME without -debug for testing non-debugger handlers."""
    async with launch_mame(
        driver=MAME_DRIVER,
        mame_binary=MAME_BINARY,
        extra_flags=["-window", "-nomaximize"],
        port=MAME_PORT + 1,
        startup_timeout=30.0,
    ) as b:
        yield b
