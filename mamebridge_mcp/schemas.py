"""Shared input validators and formatters."""

from __future__ import annotations

import re


def coerce_address(value: int | str) -> int:
    """Convert an address to an integer.

    Accepted formats:
        - int: used as-is.
        - "0x1234" or "0X1234": hex with explicit prefix.
        - "1234h" or "1234H": hex with h-suffix.
        - "1234": decimal (no implicit hex — ambiguous addresses must use a prefix).

    Raises ValueError for plain hex strings like "1a2b" (ambiguous with decimal).
    """
    if isinstance(value, int):
        return value

    s = value.strip()
    if s.lower().startswith("0x"):
        try:
            return int(s, 16)
        except ValueError:
            raise ValueError(f"invalid hex address: {value!r}")

    if s.lower().endswith("h"):
        try:
            return int(s[:-1], 16)
        except ValueError:
            raise ValueError(f"invalid hex address: {value!r}")

    if re.fullmatch(r"[0-9]+", s):
        return int(s, 10)

    raise ValueError(
        f"ambiguous address {value!r}: use '0x' prefix or 'h' suffix for hex, "
        "or a plain decimal integer."
    )


def coerce_bytes(value: list[int] | str) -> list[int]:
    """Convert a bytes specification to a list of ints (0–255 each).

    Accepted formats:
        - list[int]: used as-is (values clamped to 0–255).
        - "a9 00 8d" or "a9008d": hex string, whitespace optional.
    """
    if isinstance(value, list):
        return [int(b) & 0xFF for b in value]

    s = value.replace(" ", "").replace("\t", "").replace("\n", "")
    if not s:
        return []
    if len(s) % 2 != 0:
        raise ValueError(
            f"hex byte string must have an even number of digits, got {value!r}"
        )
    try:
        return [int(s[i : i + 2], 16) for i in range(0, len(s), 2)]
    except ValueError:
        raise ValueError(f"invalid hex byte string: {value!r}")


def hex_format(data: list[int]) -> str:
    """Format a list of byte values as lowercase hex with single-space separators.

    Example: [0xa9, 0x00, 0x8d] → "a9 00 8d"
    """
    return " ".join(f"{b:02x}" for b in data)
