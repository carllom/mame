"""Shared fixtures for mamebridge_mcp tests."""

from __future__ import annotations

import asyncio
import json
import os
import shutil
from contextlib import asynccontextmanager
from typing import Any, AsyncIterator

import pytest
from mcp.client.session import ClientSession
from mcp.shared.memory import create_connected_server_and_client_session

from mamebridge_mcp._app import mcp

MAME_BINARY = os.environ.get("MAME_BINARY", "./mametinyd")
MAME_DRIVER = os.environ.get("MAME_DRIVER", "pm3585")
MAME_PORT = int(os.environ.get("MAME_MCP_PORT", "8100"))
MAME_ATTACH_PORT = MAME_PORT + 1


@pytest.fixture(scope="session")
def mame_binary() -> str:
    binary = MAME_BINARY
    if not shutil.which(binary) and not os.path.isfile(binary):
        pytest.skip(f"MAME binary not found: {binary!r}")
    return binary


@asynccontextmanager
async def make_mcp_client() -> AsyncIterator[ClientSession]:
    """In-process MCP client connected to the FastMCP server.

    Used as an async context manager directly in test functions so that
    setup and teardown stay in the same asyncio task (anyio cancel scopes
    must be entered and exited from the same task).

    Usage::

        async def test_something():
            async with make_mcp_client() as client:
                result = await client.call_tool("session_status", {})
    """
    import mamebridge_mcp.server  # noqa: F401 — ensure tool registration

    async with create_connected_server_and_client_session(
        mcp, raise_exceptions=False
    ) as client:
        yield client


def get_result(call_result: Any) -> Any:
    """Extract the parsed JSON result from a CallToolResult.

    Raises AssertionError with the error text if isError is True.
    """
    assert not call_result.isError, call_result.content[0].text
    return json.loads(call_result.content[0].text)


def get_error(call_result: Any) -> str:
    """Extract the error message from a failed CallToolResult."""
    assert call_result.isError, "Expected tool to fail but it succeeded"
    return call_result.content[0].text


@asynccontextmanager
async def bare_mame(
    mame_binary: str,
    port: int = MAME_ATTACH_PORT,
) -> AsyncIterator[None]:
    """Start MAME at a given port and yield once the bridge is ready to accept
    its FIRST connection.

    Readiness is detected by watching for the "[mcp] listening" line on stdout
    rather than probing via TCP, because the bridge only accepts one connection
    at a time. This ensures attach_session is the first (and only) client.
    """
    env = os.environ.copy()
    env["MAME_MCP_PORT"] = str(port)

    cmd = [
        mame_binary,
        MAME_DRIVER,
        "-plugin", "mcp",
        "-skip_gameinfo",
        "-debug",
        "-window",
        "-nomaximize",
    ]
    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.DEVNULL,
        env=env,
    )

    assert proc.stdout is not None
    ready = False
    try:
        async def _drain_stdout() -> None:
            assert proc.stdout is not None
            while True:
                line = await proc.stdout.readline()
                if not line:
                    break

        # Read stdout until the bridge announces it's listening.
        deadline = asyncio.get_event_loop().time() + 30.0
        while asyncio.get_event_loop().time() < deadline:
            if proc.returncode is not None:
                raise RuntimeError(f"MAME exited early with code {proc.returncode}")
            try:
                line = await asyncio.wait_for(proc.stdout.readline(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            if b"[mcp] listening" in line:
                # The socket is open, but the machine reset notifier (which
                # enables the debugger) fires shortly after. Give MAME a moment
                # to complete initialization so that the first attach_session
                # call can read exec_state without hitting DebuggerNotEnabled.
                await asyncio.sleep(3.0)
                ready = True
                break

        if not ready:
            raise RuntimeError(f"MAME bridge did not announce readiness on port {port} within 30s")

        # Drain remaining stdout in background so the process doesn't block.
        drain_task = asyncio.create_task(_drain_stdout())
        try:
            yield
        finally:
            drain_task.cancel()
            try:
                await drain_task
            except (asyncio.CancelledError, Exception):
                pass
    finally:
        if proc.returncode is None:
            proc.terminate()
            try:
                await asyncio.wait_for(proc.wait(), timeout=3.0)
            except asyncio.TimeoutError:
                proc.kill()
                await proc.wait()
