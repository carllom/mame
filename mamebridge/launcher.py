"""Launcher — spawn MAME as a subprocess and connect the bridge."""

from __future__ import annotations

import asyncio
import os
import signal
from contextlib import asynccontextmanager
from typing import AsyncIterator

from mamebridge.client import MameBridge
from mamebridge.errors import ConnectionError, MameDied


@asynccontextmanager
async def launch_mame(
    driver: str,
    *,
    mame_binary: str = "mame",
    rom_path: str | None = None,
    extra_flags: list[str] | None = None,
    port: int | None = None,
    startup_timeout: float = 30.0,
    host: str = "127.0.0.1",
) -> AsyncIterator[MameBridge]:
    """Launch a MAME instance and yield a connected MameBridge.

    Args:
        driver: System driver name (e.g. "pm3585").
        mame_binary: Path to the MAME executable.
        rom_path: Optional ROM path override.
        extra_flags: Additional command-line flags (e.g. ["-debug", "-window"]).
        port: Bridge TCP port. Defaults to 8080 if not set.
        startup_timeout: Seconds to wait for MAME to start and accept connections.
        host: Host address for bridge connection.
    """
    if port is None:
        port = 8080
    if extra_flags is None:
        extra_flags = []

    env = os.environ.copy()
    env["MAME_MCP_PORT"] = str(port)

    cmd = [mame_binary, driver, "-plugin", "mcp", "-skip_gameinfo"]
    if rom_path:
        cmd.extend(["-rompath", rom_path])
    cmd.extend(extra_flags)

    stderr_buf = []

    proc = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        env=env,
    )

    bridge: MameBridge | None = None
    try:
        # Start collecting stderr in background
        async def _read_stderr() -> None:
            assert proc.stderr is not None
            while True:
                line = await proc.stderr.readline()
                if not line:
                    break
                stderr_buf.append(line.decode(errors="replace"))

        stderr_task = asyncio.create_task(_read_stderr())

        # Wait for the bridge socket to accept connections
        connected = False
        deadline = asyncio.get_event_loop().time() + startup_timeout
        while asyncio.get_event_loop().time() < deadline:
            # Check if MAME exited
            if proc.returncode is not None:
                await stderr_task
                stderr_text = "".join(stderr_buf)
                raise MameDied(
                    f"MAME exited with code {proc.returncode} before accepting connections",
                    returncode=proc.returncode,
                    stderr=stderr_text,
                )
            try:
                bridge = await MameBridge.connect(host, port)
                connected = True
                break
            except (ConnectionError, OSError):
                await asyncio.sleep(0.25)

        if not connected:
            await stderr_task
            stderr_text = "".join(stderr_buf)
            raise ConnectionError(
                f"timed out connecting to MAME on {host}:{port} after {startup_timeout}s. "
                f"stderr: {stderr_text[:500]}"
            )

        yield bridge

    finally:
        # Clean up — try graceful quit via bridge, then signals
        if bridge is not None:
            if proc.returncode is None:
                try:
                    await bridge.quit()
                    await asyncio.wait_for(proc.wait(), timeout=3.0)
                except (asyncio.TimeoutError, Exception):
                    pass  # Fall through to signal-based shutdown
            await bridge.close()

        if proc.returncode is None:
            proc.terminate()
            try:
                await asyncio.wait_for(proc.wait(), timeout=3.0)
            except asyncio.TimeoutError:
                proc.kill()
                await proc.wait()
