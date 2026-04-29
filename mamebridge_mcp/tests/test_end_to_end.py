"""End-to-end acceptance test.

Exercises the full tool chain that a competent agent would use against pm3585
in one continuous session: orient, trace memory, hit a breakpoint, inspect,
clean up. No manual intervention required between runs.

This test is intentionally a single long function so that failures show exactly
which step broke, and so MAME is launched only once (each start_session takes
~3-4 s).
"""

from __future__ import annotations

import pytest

from mamebridge_mcp.tests.conftest import (
    MAME_DRIVER,
    MAME_PORT,
    get_result,
    make_mcp_client,
)


pytestmark = pytest.mark.asyncio


async def test_full_session(mame_binary):
    """Scripted agent walkthrough: orient → trace → breakpoint → inspect → stop."""
    async with make_mcp_client() as client:

        # ── Step 1: start session ────────────────────────────────────────────
        start = get_result(await client.call_tool("start_session", {
            "driver": MAME_DRIVER,
            "flags": ["-debug", "-window", "-nomaximize"],
            "port": MAME_PORT,
            "mame_binary": mame_binary,
        }))
        assert start["driver"] == MAME_DRIVER

        try:

            # ── Step 2: summarize memory map ─────────────────────────────────
            mem_map = get_result(await client.call_tool("summarize_memory_map", {}))
            assert "devices" in mem_map
            tags = [d["tag"] for d in mem_map["devices"]]
            assert ":maincpu" in tags, f"':maincpu' not in {tags}"
            maincpu = next(d for d in mem_map["devices"] if d["tag"] == ":maincpu")
            space_names = [s["name"] for s in maincpu["spaces"]]
            assert "program" in space_names, f"'program' not in {space_names}"

            # ── Step 3: driver info ──────────────────────────────────────────
            info = get_result(await client.call_tool("get_driver_info", {}))
            assert info["shortname"] == MAME_DRIVER
            assert isinstance(info["flags"], dict)
            assert len(info["flags"]) > 0

            # ── Step 4: confirm paused at boot ───────────────────────────────
            state = get_result(await client.call_tool("current_state", {}))
            assert state["exec_state"] == "stop", (
                f"Expected 'stop' after start_session in debug mode, got {state['exec_state']!r}"
            )
            assert state["driver"] == MAME_DRIVER
            assert isinstance(state["frame"], int)
            frame_before = state["frame"]

            # ── Step 5: find writes to stack area during the first 30 frames ─
            # Boot SP=0x180000; the first subroutine call writes the return
            # address at 0x17fffc. Run the tap from frame 0 so it catches
            # those early pushes before the stack grows deeper.
            writes = get_result(await client.call_tool("find_writes_to", {
                "address": "0x17ff00", "length": 0x100, "frames": 30,
            }))
            assert "entries" in writes
            assert "drops" in writes
            assert isinstance(writes["entries"], list)
            assert len(writes["entries"]) > 0, (
                "Expected stack writes in 0x17ff00–0x17ffff during first 30 boot frames"
            )
            for entry in writes["entries"]:
                assert "addr" in entry
                assert "value" in entry
                assert "mask" in entry
                assert 0x17ff00 <= entry["addr"] <= 0x17ffff, (
                    f"Expected addr in tap range, got {entry['addr']:#x}"
                )

            # ── Step 6: advance 10 more frames ──────────────────────────────
            run_result = get_result(await client.call_tool("run_for_frames", {
                "frames": 10, "timeout": 30.0,
            }))
            assert run_result["frame"] >= frame_before + 10
            assert run_result["exec_state"] == "stop"

            # ── Step 7: registers — PC is non-zero ───────────────────────────
            regs = get_result(await client.call_tool("dump_registers", {}))
            assert "registers" in regs
            assert len(regs["registers"]) > 0
            pc_reg = next(
                (r for r in regs["registers"] if r["name"] in ("PC", "pc", "curpc")),
                None,
            )
            assert pc_reg is not None, "No PC register found in dump_registers output"
            assert pc_reg["value"] != 0

            # ── Step 8: disassemble around PC ────────────────────────────────
            dasm = get_result(await client.call_tool("disassemble_around_pc", {
                "before": 2, "after": 8,
            }))
            assert "lines" in dasm
            assert len(dasm["lines"]) >= 10, (
                f"Expected ≥10 disassembly lines, got {len(dasm['lines'])}"
            )
            current_lines = [l for l in dasm["lines"] if l.get("is_current")]
            assert len(current_lines) == 1, (
                f"Expected exactly one is_current=True line, got {len(current_lines)}"
            )

            # ── Step 9: set a breakpoint and run until it ────────────────────
            # Step forward several instructions to land at a stable PC in ROM,
            # then use that address as the breakpoint target. run_until_breakpoint
            # handles the "already at target" case by stepping first, so the CPU
            # will re-enter the address naturally (boot code is mostly loops).
            for _ in range(5):
                await client.call_tool("step", {"count": 1})
            target_pc = get_result(
                await client.call_tool("read_register", {"name": "PC"})
            )["value"]

            bp = get_result(await client.call_tool("bp_set", {"address": target_pc}))
            bp_id = bp["id"]
            try:
                hit = get_result(await client.call_tool("run_until_breakpoint", {
                    "address": target_pc, "timeout": 10.0,
                }))
                assert hit["pc"] == target_pc, (
                    f"Expected PC={target_pc:#x}, got {hit['pc']:#x}"
                )
            finally:
                # ── Step 10: clean up breakpoint ─────────────────────────────
                await client.call_tool("bp_clear", {"id": bp_id})

        finally:
            await client.call_tool("stop_session", {})
