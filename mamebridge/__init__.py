"""MCP Bridge — async Python client for the MAME JSON-RPC bridge."""

from mamebridge.client import MameBridge
from mamebridge.launcher import launch_mame
from mamebridge.errors import (
    MameBridgeError,
    ConnectionError,
    ProtocolError,
    MethodNotFound,
    InvalidParams,
    DebuggerNotEnabled,
    DeviceNotFound,
    InvalidAddressSpace,
    BreakpointNotFound,
    SystemNotRunning,
    InvalidAddress,
    TapNotFound,
    TimeoutError,
    MameDied,
)
from mamebridge.protocol import Event

__all__ = [
    "MameBridge",
    "launch_mame",
    "Event",
    "MameBridgeError",
    "ConnectionError",
    "ProtocolError",
    "MethodNotFound",
    "InvalidParams",
    "DebuggerNotEnabled",
    "DeviceNotFound",
    "InvalidAddressSpace",
    "BreakpointNotFound",
    "SystemNotRunning",
    "InvalidAddress",
    "TapNotFound",
    "TimeoutError",
    "MameDied",
]
