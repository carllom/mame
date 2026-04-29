"""JSON-RPC 2.0 framing and event types."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any


API_VERSION = 1


@dataclass
class Event:
    """A server-sent JSON-RPC notification."""
    method: str
    params: dict[str, Any] = field(default_factory=dict)

    def __getattr__(self, name: str) -> Any:
        try:
            return self.params[name]
        except KeyError:
            raise AttributeError(f"Event has no attribute '{name}'")


def encode_request(id: int, method: str, params: dict[str, Any]) -> bytes:
    """Encode a JSON-RPC 2.0 request as a newline-delimited bytes line."""
    msg = {"jsonrpc": "2.0", "id": id, "method": method, "params": params}
    return json.dumps(msg).encode() + b"\n"


def decode_line(line: bytes) -> dict[str, Any]:
    """Decode a single JSON-RPC line. Raises ProtocolError on failure."""
    from mamebridge.errors import ProtocolError
    try:
        obj = json.loads(line)
    except json.JSONDecodeError as e:
        raise ProtocolError(f"invalid JSON: {e}") from e
    if not isinstance(obj, dict):
        raise ProtocolError("expected JSON object")
    return obj


def is_notification(msg: dict[str, Any]) -> bool:
    """Check if a decoded message is a notification (no 'id' field)."""
    return "id" not in msg and "method" in msg


def is_response(msg: dict[str, Any]) -> bool:
    """Check if a decoded message is a response (has 'id' field)."""
    return "id" in msg
