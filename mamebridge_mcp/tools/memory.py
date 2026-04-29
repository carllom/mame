"""Memory tools: read, write, disassemble, registers, disassemble_around_pc."""

from __future__ import annotations

from typing import Any

from mamebridge.errors import MameBridgeError
from mamebridge_mcp._app import mcp
from mamebridge_mcp.errors import raise_as_tool_error
from mamebridge_mcp.schemas import coerce_address, coerce_bytes, hex_format
from mamebridge_mcp.session import session


@mcp.tool()
async def read_memory(
    address: int | str,
    length: int = 1,
    device: str = ":maincpu",
    space: str = "program",
    unit_size: int = 1,
    format: str = "hex",
) -> dict[str, Any]:
    """Read bytes from a device address space.

    Args:
        address: Start address. Accepts integers or strings: '0x1234', '1234h',
                 or plain decimal. Never use bare hex like '1a2b' (ambiguous).
        length: Number of unit_size units to read (default 1 byte).
        device: Device tag (default ':maincpu').
        space: Address space name (default 'program'). Use list_address_spaces
               to find available spaces — reading the wrong one returns garbage.
        unit_size: Read granularity in bytes: 1 (byte), 2 (word), 4 (dword).
        format: 'hex' returns a compact lowercase hex string ("a9 00 8d").
                'int_array' returns a list of integers. Default: 'hex'.

    Returns: {"address": int, "length": int, "data": str | list[int]}.
    """
    bridge = session.require()
    addr = coerce_address(address)
    try:
        raw = await bridge.read_mem(
            addr,
            length,
            device=device,
            space=space,
            unit_size=unit_size,
        )
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, space=space, address=addr)

    if format == "int_array":
        data: str | list[int] = raw
    else:
        data = hex_format(raw)

    return {"address": addr, "length": length, "data": data}


@mcp.tool()
async def write_memory(
    address: int | str,
    data: list[int] | str,
    device: str = ":maincpu",
    space: str = "program",
) -> dict[str, int]:
    """Write bytes to a device address space.

    Args:
        address: Target address. Accepts '0x1234', '1234h', or decimal.
        data: Bytes to write. Either a list of ints (e.g. [0xa9, 0x00]) or a
              hex string with optional spaces (e.g. "a9 00 8d" or "a9008d").
        device: Device tag (default ':maincpu').
        space: Address space name (default 'program').

    Returns: {"written": int} — number of bytes written.
    """
    bridge = session.require()
    addr = coerce_address(address)
    payload = coerce_bytes(data)
    try:
        written = await bridge.write_mem(addr, payload, device=device, space=space)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device, space=space, address=addr)
    return {"written": written}


@mcp.tool()
async def disassemble(
    address: int | str,
    count: int = 10,
    device: str = ":maincpu",
) -> dict[str, Any]:
    """Disassemble instructions starting from an address.

    Args:
        address: Start address. Accepts '0x1234', '1234h', or decimal.
        count: Number of instructions to disassemble (default 10).
        device: Device tag (default ':maincpu').

    Returns: [{"address": int, "text": str}, ...] — one entry per instruction.
    """
    bridge = session.require()
    addr = coerce_address(address)
    try:
        lines = await bridge.disassemble(addr, count, device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"lines": [{"address": line.address, "text": line.text} for line in lines]}


@mcp.tool()
async def read_register(
    name: str,
    device: str = ":maincpu",
) -> dict[str, Any]:
    """Read a single CPU register by name.

    Args:
        name: Register name, e.g. 'PC', 'SP', 'D0'. Use dump_registers to
              discover available names — they are case-sensitive on some cores.
        device: Device tag (default ':maincpu').

    Returns: {"name": str, "value": int}.
    """
    bridge = session.require()
    try:
        value = await bridge.read_reg(name, device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"name": name, "value": value}


@mcp.tool()
async def write_register(
    name: str,
    value: int | str,
    device: str = ":maincpu",
) -> dict[str, Any]:
    """Write a value to a CPU register.

    Args:
        name: Register name, e.g. 'PC', 'D0'.
        value: New value. Accepts an integer or hex string ('0x1234').
        device: Device tag (default ':maincpu').

    Returns: {"name": str, "value": int} — the value as stored.
    """
    bridge = session.require()
    v = coerce_address(value)
    try:
        await bridge.write_reg(name, v, device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"name": name, "value": v}


@mcp.tool()
async def dump_registers(device: str = ":maincpu") -> dict[str, Any]:
    """Return all CPU registers and their current values.

    When to use: to see the full register state after a breakpoint hit, or
    to discover register names before calling read_register/write_register.

    Args:
        device: Device tag (default ':maincpu').

    Returns: {"registers": [{"name": str, "value": int, "size_bits": int}, ...]}.
    """
    bridge = session.require()
    try:
        regs = await bridge.list_regs(device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)
    return {"registers": [{"name": r.name, "value": r.value, "size_bits": r.size_bits} for r in regs]}


@mcp.tool()
async def disassemble_around_pc(
    before: int = 4,
    after: int = 12,
    device: str = ":maincpu",
) -> dict[str, Any]:
    """Disassemble a window of instructions centred on the current PC.

    Shows context before and after the current instruction. Useful after any
    breakpoint hit to understand what the CPU is about to execute.

    Implementation note: 'before' instructions are estimated by disassembling
    from pc - before*4 and trimming forward; this is a heuristic. Max instruction
    size varies by CPU family (up to 10 bytes for 68k). The result may include
    slightly more or fewer instructions before PC than requested.

    Args:
        before: Approximate number of instructions to show before PC (default 4).
        after: Number of instructions to show after PC (default 12).
        device: Device tag (default ':maincpu').

    Returns: {"pc": int, "lines": [{"address": int, "text": str,
              "is_current": bool}, ...]}.
    """
    bridge = session.require()
    try:
        pc = await bridge.read_reg("PC", device=device)
        start = max(0, pc - before * 4)
        total = before + after + 2
        raw_lines = await bridge.disassemble(start, total, device=device)
    except MameBridgeError as exc:
        raise_as_tool_error(exc, device=device)

    lines = []
    seen_pc = False
    for line in raw_lines:
        is_current = line.address == pc
        if is_current:
            seen_pc = True
        lines.append({
            "address": line.address,
            "text": line.text,
            "is_current": is_current,
        })

    if not seen_pc:
        try:
            exact = await bridge.disassemble(pc, after + 1, device=device)
            lines = [{"address": l.address, "text": l.text, "is_current": l.address == pc}
                     for l in exact]
        except MameBridgeError:
            pass

    return {"pc": pc, "lines": lines}
