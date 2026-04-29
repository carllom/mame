"""Exception hierarchy for MCP bridge errors."""

from __future__ import annotations


class MameBridgeError(Exception):
    """Base exception for all MCP bridge errors."""


class ConnectionError(MameBridgeError):
    """Failed to connect or lost connection."""


class ProtocolError(MameBridgeError):
    """Malformed JSON-RPC message."""


class MethodNotFound(MameBridgeError):
    """JSON-RPC -32601: method not found."""


class InvalidParams(MameBridgeError):
    """JSON-RPC -32602: invalid parameters."""


class InternalError(MameBridgeError):
    """JSON-RPC -32603: internal error on bridge side."""


class DebuggerNotEnabled(MameBridgeError):
    """JSON-RPC -32000: MAME was not launched with -debug."""


class DeviceNotFound(MameBridgeError):
    """JSON-RPC -32001: device tag does not exist."""


class InvalidAddressSpace(MameBridgeError):
    """JSON-RPC -32002: address space not found on device."""


class BreakpointNotFound(MameBridgeError):
    """JSON-RPC -32003: breakpoint/watchpoint ID not found."""


class SystemNotRunning(MameBridgeError):
    """JSON-RPC -32004: operation requires a running system."""


class InvalidAddress(MameBridgeError):
    """JSON-RPC -32005: address out of range or invalid."""


class TapNotFound(MameBridgeError):
    """JSON-RPC -32006: tap ID not found."""


class TimeoutError(MameBridgeError):
    """Client-side timeout waiting for response or event."""


class MameDied(MameBridgeError):
    """MAME subprocess exited unexpectedly."""

    def __init__(self, message: str, returncode: int | None = None, stderr: str = ""):
        super().__init__(message)
        self.returncode = returncode
        self.stderr = stderr


# Map JSON-RPC error codes to exception classes
ERROR_CODE_MAP: dict[int, type[MameBridgeError]] = {
    -32700: ProtocolError,       # parse error
    -32600: ProtocolError,       # invalid request
    -32601: MethodNotFound,
    -32602: InvalidParams,
    -32603: InternalError,
    -32000: DebuggerNotEnabled,
    -32001: DeviceNotFound,
    -32002: InvalidAddressSpace,
    -32003: BreakpointNotFound,
    -32004: SystemNotRunning,
    -32005: InvalidAddress,
    -32006: TapNotFound,
}


def raise_for_error(code: int, message: str, data: object = None) -> None:
    """Raise the appropriate exception for a JSON-RPC error code."""
    exc_class = ERROR_CODE_MAP.get(code, MameBridgeError)
    detail = message
    if data is not None:
        detail = f"{message}: {data}"
    raise exc_class(detail)
