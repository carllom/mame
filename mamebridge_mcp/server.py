"""MameBridge MCP server — registers all tools and exposes run()."""

from mamebridge_mcp._app import mcp  # noqa: F401 — re-exported for callers

# Import tool modules to trigger @mcp.tool() registration.
import mamebridge_mcp.tools.session  # noqa: F401
import mamebridge_mcp.tools.inspection  # noqa: F401
import mamebridge_mcp.tools.memory  # noqa: F401
import mamebridge_mcp.tools.execution  # noqa: F401
import mamebridge_mcp.tools.breakpoints  # noqa: F401
import mamebridge_mcp.tools.state  # noqa: F401
import mamebridge_mcp.tools.taps  # noqa: F401
import mamebridge_mcp.tools.raw  # noqa: F401


def run() -> None:
    """Start the MCP server on stdio (blocking)."""
    mcp.run()
