"""Raw escape hatch: run an arbitrary MAME debugger command."""

from __future__ import annotations

from mamebridge.errors import MameBridgeError
from mamebridge_mcp._app import mcp
from mamebridge_mcp.errors import raise_as_tool_error
from mamebridge_mcp.session import session


@mcp.tool()
async def exec_mame_command(command: str) -> dict[str, str]:
    """Run an arbitrary MAME debugger command and return its text output.

    Escape hatch — use only when no structured tool fits. Output is unstructured
    text that the agent must parse. Avoid commands that change state in
    non-obvious ways (e.g. 'reset', 'exit'). There is no confirmation prompt.

    Useful for obscure debugger commands like 'history', 'traceover',
    or 'dasm <file> <addr> <length>' when other tools are insufficient.

    Args:
        command: Debugger command string, e.g. 'dasm out.txt,0x1000,0x100'.

    Returns: {"output": str} — raw text output from MAME.
    """
    bridge = session.require()
    try:
        output = await bridge.exec_command(command)
    except MameBridgeError as exc:
        raise_as_tool_error(exc)
    return {"output": output}
