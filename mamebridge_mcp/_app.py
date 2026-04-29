"""FastMCP application singleton.

All tool modules import `mcp` from here. `server.py` then imports those
modules to register their tools.
"""

from mcp.server.fastmcp import FastMCP

mcp = FastMCP("mame-bridge")
