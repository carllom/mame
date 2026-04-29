"""Tests for memory tools: read_memory, write_memory, disassemble, registers,
disassemble_around_pc. Also tests address coercion (schemas.py).
"""

from __future__ import annotations

import pytest

from mamebridge_mcp.schemas import coerce_address, coerce_bytes, hex_format
from mamebridge_mcp.tests.conftest import (
    MAME_DRIVER,
    MAME_PORT,
    get_result,
    make_mcp_client,
)


pytestmark = pytest.mark.asyncio

# ── Unit tests for schemas (no MAME needed) ──────────────────────────────────


def test_coerce_address_int():
    assert coerce_address(0x1234) == 0x1234


def test_coerce_address_0x_prefix():
    assert coerce_address("0x1234") == 0x1234
    assert coerce_address("0X1A2B") == 0x1A2B


def test_coerce_address_h_suffix():
    assert coerce_address("1234h") == 0x1234
    assert coerce_address("FFh") == 0xFF
    assert coerce_address("FFH") == 0xFF


def test_coerce_address_decimal():
    assert coerce_address("1000") == 1000
    assert coerce_address("0") == 0


def test_coerce_address_ambiguous_hex_rejected():
    with pytest.raises(ValueError, match="ambiguous"):
        coerce_address("1a2b")


def test_coerce_address_invalid():
    with pytest.raises(ValueError):
        coerce_address("0xGGGG")


def test_coerce_bytes_list():
    assert coerce_bytes([0xa9, 0x00, 0x8d]) == [0xa9, 0x00, 0x8d]


def test_coerce_bytes_hex_string_spaced():
    assert coerce_bytes("a9 00 8d") == [0xa9, 0x00, 0x8d]


def test_coerce_bytes_hex_string_compact():
    assert coerce_bytes("a9008d") == [0xa9, 0x00, 0x8d]


def test_coerce_bytes_odd_length_rejected():
    with pytest.raises(ValueError, match="even"):
        coerce_bytes("a90")


def test_hex_format():
    assert hex_format([0xa9, 0x00, 0x8d]) == "a9 00 8d"
    assert hex_format([0]) == "00"
    assert hex_format([0xFF]) == "ff"


# ── Integration tests (need MAME) ────────────────────────────────────────────


async def test_read_memory_hex_format(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("read_memory", {
                "address": "0x0", "length": 4,
            })
            data = get_result(result)
            assert data["address"] == 0
            assert data["length"] == 4
            assert isinstance(data["data"], str)
            parts = data["data"].split(" ")
            assert len(parts) == 4
        finally:
            await client.call_tool("stop_session", {})


async def test_read_memory_int_array_format(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("read_memory", {
                "address": "0x0", "length": 4, "format": "int_array",
            })
            data = get_result(result)
            assert isinstance(data["data"], list)
            assert len(data["data"]) == 4
        finally:
            await client.call_tool("stop_session", {})


async def test_read_memory_h_suffix_address(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("read_memory", {
                "address": "0h", "length": 1,
            })
            data = get_result(result)
            assert data["address"] == 0
        finally:
            await client.call_tool("stop_session", {})


async def test_write_and_read_memory(mame_binary):
    """Write a known pattern to RAM and read it back."""
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            # pm3585 RAM is at 0x10000 in program space
            addr = "0x10000"
            payload = [0xDE, 0xAD, 0xBE, 0xEF]

            w = get_result(await client.call_tool("write_memory", {
                "address": addr, "data": payload,
            }))
            assert w["written"] == 4

            r = get_result(await client.call_tool("read_memory", {
                "address": addr, "length": 4, "format": "int_array",
            }))
            assert r["data"] == payload
        finally:
            await client.call_tool("stop_session", {})


async def test_disassemble(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            pc_result = get_result(await client.call_tool("read_register", {"name": "PC"}))
            pc = pc_result["value"]

            result = await client.call_tool("disassemble", {"address": pc, "count": 5})
            lines = get_result(result)["lines"]
            assert isinstance(lines, list)
            assert len(lines) == 5
            assert lines[0]["address"] == pc
            assert isinstance(lines[0]["text"], str)
        finally:
            await client.call_tool("stop_session", {})


async def test_read_write_register(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            original = get_result(await client.call_tool("read_register", {"name": "PC"}))
            assert "value" in original

            get_result(await client.call_tool("write_register", {
                "name": "PC", "value": original["value"],
            }))
            readback = get_result(await client.call_tool("read_register", {"name": "PC"}))
            assert readback["value"] == original["value"]
        finally:
            await client.call_tool("stop_session", {})


async def test_dump_registers(mame_binary):
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("dump_registers", {})
            regs = get_result(result)["registers"]
            assert isinstance(regs, list)
            names = [r["name"] for r in regs]
            assert "PC" in names
        finally:
            await client.call_tool("stop_session", {})


async def test_disassemble_around_pc(mame_binary):
    """disassemble_around_pc returns a list with exactly one is_current=True entry."""
    async with make_mcp_client() as client:
        await client.call_tool("start_session", {
            "driver": MAME_DRIVER, "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT, "mame_binary": mame_binary,
        })
        try:
            result = await client.call_tool("disassemble_around_pc", {
                "before": 2, "after": 8,
            })
            data = get_result(result)
            assert "pc" in data
            assert "lines" in data
            lines = data["lines"]
            assert len(lines) >= 10
            current_lines = [l for l in lines if l.get("is_current")]
            assert len(current_lines) == 1
            assert current_lines[0]["address"] == data["pc"]
        finally:
            await client.call_tool("stop_session", {})
