# Phase 2 Implementation Plan — MAME MCP Server

This document is the instruction set for a coder agent. Read it top to bottom before writing any code. Then read [.claude/CLAUDE.md](.claude/CLAUDE.md) and [mamebridge/client.py](mamebridge/client.py) so you know the surface you're wrapping.

---

## What you're building

An MCP (Model Context Protocol) server that exposes the existing Phase 1 `mamebridge` async client as MCP tools, so an LLM agent can drive MAME for emulation driver development. Stdio transport. Single MAME instance per session. Target user: someone iterating on the `pm3585` (Philips CD-i, SCC68070) driver, but no driver-specific assumptions in the code.

The Phase 1 layers (Lua plugin and Python client) are done and stable. **Do not modify them** except where this plan explicitly says to (one small additive change to `driver_info` in Step 5).

Use the python virtual environment `source .venv/bin/activate` for all python activities (running/tests/packages).

---

## Decisions already made — do not relitigate

These were settled in conversation. If you find yourself wanting to change one, stop and ask.

1. **Package layout:** new sibling package `mamebridge_mcp/` next to `mamebridge/`. Keeps `mamebridge` dep-free; `mamebridge_mcp` brings in the `mcp` SDK.
2. **MCP framework:** the official `mcp` Python SDK, using `FastMCP` (decorator-based). Stdio transport.
3. **Lifecycle:** server owns one MAME instance per session. Two ways to attach: `start_session(driver, ...)` launches MAME via `launch_mame`, `attach_session(host, port)` connects to a MAME the user already started. Tools other than session/status return an MCP error if no session is live.
4. **Tap result format:** flat list of `{pc, addr, value, frame}` dicts. `group_by` is *not* a parameter in Phase 2.
5. **Memory read format:** default `format="hex"` returns a compact lowercase hex string with single-space separators (`"a9 00 8d 00 02"`). `format="int_array"` returns the raw `list[int]`. Hex is the default because it's token-cheap and copy-pastable.
6. **Address inputs:** accept JSON integers; coerce strings like `"0xd800"` and `"d800h"` in a shared validator. Document hex examples in tool descriptions.
7. **Defaults:** `device=":maincpu"`, `space="program"`, `unit_size=1`. Tool descriptions tell the agent these are usually correct and how to override.
8. **Screenshots:** return MCP `ImageContent` (base64 PNG inline). No filesystem path option.
9. **Errors:** catch `MameBridgeError` subclasses and re-raise as MCP tool errors with agent-tuned messages (no Python tracebacks, no error codes in the user-facing message).
10. **Skill format:** separate skill file at `skills/mame-driver-dev/SKILL.md`. Not an inline system prompt.
11. **Wire protocol version:** stays at 1. Phase 2 does not bump `api_version`.
12. **Driver flags surfacing:** one small Lua-side patch to `plugins/mcp/handlers/machine.lua` to add `flags` to `driver_info`. This is the only Phase 1 code change in scope.

---

## What is *not* in scope

- Multi-client / multi-session support.
- Authentication.
- Streaming or chunked tool responses.
- Any C++ patch to MAME.
- Any change to the JSON-RPC wire protocol.
- Any change to `mamebridge/` other than what's needed to make it importable from `mamebridge_mcp` (it already is).
- `group_by` on tap results.
- Path-based screenshots.
- Authorization for the `exec_mame_command` escape hatch (just document the risk in its description).

---

## Repository layout you'll create

```
mamebridge_mcp/
  __init__.py
  __main__.py             # python -m mamebridge_mcp entrypoint
  server.py               # FastMCP instance, tool registration, run()
  session.py              # holds the live MameBridge; start/attach/stop/status
  schemas.py              # shared validators (address coercion, hex bytes)
  errors.py               # MameBridgeError → MCP error message mapping
  tools/
    __init__.py
    session.py
    inspection.py
    execution.py
    memory.py
    breakpoints.py
    taps.py
    state.py
    raw.py
  tests/
    __init__.py
    conftest.py           # MCP in-process client + real MAME fixture
    test_session.py
    test_inspection.py
    test_execution.py
    test_memory.py
    test_breakpoints.py
    test_taps.py
    test_state.py
    test_end_to_end.py
skills/
  mame-driver-dev/
    SKILL.md
```

Update [pyproject.toml](pyproject.toml):
- Add `mamebridge_mcp` to `tool.setuptools.packages.find` include list.
- Add an optional dependency group `mcp = ["mcp>=1.0"]` (verify the current minimum version when you install it).
- Add a `[project.scripts]` entry `mamebridge-mcp = "mamebridge_mcp.__main__:main"`.

---

## Step plan

Each step has a scope, files, acceptance criteria, and a "stop here and confirm with the user" gate. **Do not skip the gates.** Show the user the diff and the test output before moving on.

---

### Step 1 — Package skeleton + session lifecycle

**Scope:** Stand up the package, get FastMCP running over stdio, implement `start_session` / `attach_session` / `stop_session` / `session_status`. No other tools yet.

**Files:**
- `mamebridge_mcp/__init__.py` — empty or version string only.
- `mamebridge_mcp/__main__.py` — `def main(): from .server import mcp; mcp.run()`. Plus `if __name__ == "__main__": main()`.
- `mamebridge_mcp/session.py`:
  - `class Session` holds `bridge: MameBridge | None`, `proc_ctx: AsyncContextManager | None` (the `launch_mame` cm), `driver: str | None`, `attached: bool`.
  - Methods: `async def start(driver, flags=None, port=None, mame_binary="mame", rom_path=None)` (uses `launch_mame`), `async def attach(host="127.0.0.1", port=8080)`, `async def stop()`, `async def status() -> dict`, `def require() -> MameBridge` (raises if no session).
  - Module-level `session = Session()` singleton.
- `mamebridge_mcp/errors.py`:
  - `def mcp_error_from(exc: MameBridgeError) -> str` returning agent-friendly messages (see "Error messages" below).
  - `def tool(...)` decorator or context manager that catches `MameBridgeError` and re-raises as the MCP SDK's tool-error type with the friendly message. (Check the current SDK — `FastMCP` may surface raised exceptions as tool errors automatically; if so, just translate the message in the exception.)
- `mamebridge_mcp/server.py`:
  - `from mcp.server.fastmcp import FastMCP`
  - `mcp = FastMCP("mame-bridge")`
  - Import and register session tools.
- `mamebridge_mcp/tools/session.py` — the four session tools.
- `mamebridge_mcp/tests/conftest.py`:
  - `mame_binary` fixture (env var `MAME_BINARY` or default `mame`; pytest.skip if not on PATH).
  - `mcp_client` fixture: spin up an in-process MCP client connected to our FastMCP server via the SDK's memory transport. **Verify the exact API in the installed SDK version** — at time of writing it's `mcp.shared.memory.create_connected_server_and_client_session` or similar; check before relying on it.

**Tool surface for this step:**

```
start_session(driver: str, flags: list[str] = [], port: int | None = None,
              mame_binary: str = "mame", rom_path: str | None = None) -> dict
  # Launches MAME with the bridge plugin. flags are passed through to the MAME
  # command line (e.g. ["-debug", "-window", "-nomaximize"]). Returns
  # {"driver": ..., "port": ..., "frame": ..., "exec_state": ...}.

attach_session(host: str = "127.0.0.1", port: int = 8080) -> dict
  # Connect to a MAME instance you started yourself (e.g. with -console for
  # interactive debugging). Same return shape as start_session.

stop_session() -> dict
  # Graceful shutdown. Returns {"stopped": true}.

session_status() -> dict
  # {"alive": bool, "driver": str | null, "attached": bool, "frame": int,
  #  "exec_state": "run" | "stop", "pc": int}
```

**Tool descriptions (this is real Phase 2 work — write them properly):** each tool's docstring becomes its MCP description. Style:
- One-sentence purpose, present tense.
- "When to use:" line if it's non-obvious.
- "Returns:" line summarizing the dict shape.
- Gotchas / common mistakes if any.

Example:

```python
@mcp.tool()
async def start_session(
    driver: str,
    flags: list[str] | None = None,
    port: int | None = None,
    mame_binary: str = "mame",
    rom_path: str | None = None,
) -> dict:
    """Launch a MAME instance with the bridge plugin and connect to it.

    When to use: at the start of a debugging session, when you want the server
    to own the MAME process. If MAME is already running and you launched it
    yourself, use attach_session instead.

    The bridge auto-passes -plugin mcp and -skip_gameinfo. You should usually
    pass flags=["-debug", "-window", "-nomaximize"]; -debug is required for
    breakpoints, watchpoints, and step control.

    Returns: {"driver", "port", "frame", "exec_state"}.
    """
```

**Acceptance criteria:**
- `python -m mamebridge_mcp` starts and serves stdio without crashing (test by piping `{"jsonrpc":"2.0",...}` initialize handshake — or just rely on the in-process test).
- `pytest mamebridge_mcp/tests/test_session.py` passes:
  - `start_session("pm3585", flags=["-debug","-window","-nomaximize"])` succeeds and returns a dict with `exec_state == "stop"` (debug mode starts paused).
  - `session_status()` returns sensible values.
  - `stop_session()` cleanly tears down.
  - `attach_session` test: launch MAME via `launch_mame` directly in the test, then call `attach_session` against it, verify status, stop_session detaches without killing the externally-owned process. (For the `attach` path, `stop_session` must NOT terminate MAME — it must only close the connection.)
- All tools other than session-management raise a useful "no active session" error if called without one. Test this with one tool stub (e.g. add a temporary `_test_requires_session` tool, or wait for Step 2).

**Gate:** show user diff + `pytest mamebridge_mcp/tests/test_session.py -v` output. Do not proceed.

---

### Step 2 — 1:1 wrap tools (inspection, memory, execution, breakpoints, state, raw)

**Scope:** Wrap the Phase 1 client methods that are useful as-is, with good descriptions and the schema choices from "Decisions already made". One file per category. **No composites yet** — those are Step 3.

**Files and tools:**

`tools/inspection.py`:
- `list_devices() -> list[dict]`
- `list_address_spaces(device: str = ":maincpu") -> list[dict]`
- `list_registers(device: str = ":maincpu") -> list[dict]`
- `get_driver_info() -> dict` — wraps `driver_info()`. Flags field added in Step 5.

`tools/memory.py`:
- `read_memory(address: int | str, length: int = 1, device: str = ":maincpu", space: str = "program", unit_size: int = 1, format: str = "hex") -> dict`
  - Returns `{"address": int, "length": int, "data": str | list[int]}`.
  - `format="hex"` → space-separated lowercase hex; `format="int_array"` → list of ints.
- `write_memory(address: int | str, data: list[int] | str, device: str = ":maincpu", space: str = "program") -> dict`
  - Accept `data` as `list[int]` or hex string (`"a9 00 8d"` or `"a9008d"`).
  - Returns `{"written": int}`.
- `disassemble(address: int | str, count: int = 10, device: str = ":maincpu") -> list[dict]`
  - `[{"address": int, "text": str}, ...]`.
- `read_register(name: str, device: str = ":maincpu") -> dict` → `{"name": ..., "value": int}`.
- `write_register(name: str, value: int | str, device: str = ":maincpu") -> dict` → `{"name": ..., "value": int}`.
- `dump_registers(device: str = ":maincpu") -> list[dict]` — wraps `list_regs()` for parity with the agent's mental model ("dump" reads better than "list" here).

`tools/execution.py`:
- `step(count: int = 1, device: str = ":maincpu") -> dict` → `{"pc": int}`.
- `step_over(device: str = ":maincpu") -> dict` → `{"pc": int}` (read PC after).
- `step_out(device: str = ":maincpu") -> dict` → `{"pc": int}`.
- `pause() -> dict` → `{"exec_state": "stop", "pc": int}`.
- `resume() -> dict` → `{"exec_state": "run"}`. (Names it `resume` for the agent; underlying call is `bridge.run()`.)
- `exec_state() -> dict` → `{"state": "run" | "stop"}`. (Provided as a low-level escape hatch; `current_state` from Step 3 is the preferred tool.)

`tools/breakpoints.py`:
- `bp_set(address: int | str, device: str = ":maincpu", condition: str = "", action: str = "", oneshot: bool = False) -> dict`
- `bp_clear(id: int) -> dict`
- `bp_enable(id: int, enabled: bool = True) -> dict`
- `bp_list(device: str | None = None) -> list[dict]`
- `wp_set(address: int | str, length: int, type: str = "rw", device: str = ":maincpu", space: str = "program", condition: str = "", action: str = "") -> dict`
- `wp_clear(id: int) -> dict`
- `wp_enable(id: int, enabled: bool = True) -> dict`
- `wp_list(device: str | None = None) -> list[dict]`

`tools/state.py`:
- `save_state(name: str = "mcp_save") -> dict` → `{"name": str}`.
- `load_state(name: str) -> dict` → `{"name": str}`.
- `screenshot(screen: str | None = None) -> ImageContent` — return MCP `ImageContent` with base64 PNG. Description warns: "Each screenshot is ~50–500KB inline; don't take many per session."
- `frame_number() -> dict` → `{"frame": int}`.

`tools/taps.py` (just the low-level escape hatches; composites in Step 3):
- `install_read_tap(address: int | str, length: int, device: str = ":maincpu", space: str = "program", capacity: int | None = None) -> dict` → `{"tap_id": int}`.
- `install_write_tap(...)` — same signature.
- `read_tap_buffer(tap_id: int, max_entries: int | None = None, drain: bool = True) -> dict`
  - Returns `{"entries": [{"pc","addr","value","frame"}, ...], "drops": int, "remaining": int}`.
  - **Flat format only.** Pass entries through unchanged from the Lua side.
- `remove_tap(tap_id: int) -> dict`.

`tools/raw.py`:
- `exec_mame_command(command: str) -> dict` → `{"output": str}`.
  - Description: "Escape hatch — runs an arbitrary MAME debugger command. Use only when no other tool fits, since output is unstructured text the agent must parse. Avoid commands that change state in non-obvious ways."

**Shared work:**

`schemas.py`:
- `def coerce_address(value: int | str) -> int` — accepts `0x...`, `...h`, decimal, plain hex without prefix is **not** allowed (ambiguous with decimal); raise a clear error if the string doesn't match.
- `def coerce_bytes(value: list[int] | str) -> list[int]` — accepts list, hex string with optional whitespace.
- `def hex_format(data: list[int]) -> str` — `"a9 00 8d"` style.

`errors.py` mappings (use these messages verbatim where applicable):
- `DebuggerNotEnabled` → `"MAME was not launched with -debug. Restart with start_session(driver, flags=[\"-debug\", ...])."`
- `DeviceNotFound` → `"Device {device!r} not found. Use list_devices to see what's available."`
- `InvalidAddressSpace` → `"Address space {space!r} not found on {device!r}. Use list_address_spaces to see what's available."`
- `BreakpointNotFound` → `"Breakpoint id {id} not found. Use bp_list to see active breakpoints."`
- `SystemNotRunning` → `"The system is not in a running/stopped state yet (still booting?). Try again in a frame or two."`
- `InvalidAddress` → `"Address {address:#x} is invalid for the {space!r} space."`
- `TapNotFound` → `"Tap id {id} not found. It may have been removed or never installed."`
- `TimeoutError` → `"Operation timed out. The CPU may be in a tight loop or never reaching the target."`
- `MameDied` / `ConnectionError` → `"Lost connection to MAME. Use session_status to check, then start_session or attach_session again."`

**Acceptance criteria:**
- One pytest module per `tools/*.py` file, each exercising every tool against a real MAME `pm3585` session.
- Hex / int address coercion has its own unit test in `test_memory.py`.
- All tool descriptions are non-empty and follow the style template.
- `pytest mamebridge_mcp/tests/ -v` passes.

**Gate:** show user the new tool list (`grep -r "@mcp.tool" mamebridge_mcp/tools/`), a sample of 2–3 tool descriptions in full, and the test output.

---

### Step 3 — Composite tools

**Scope:** The agent-leverage tools. These are why Phase 2 isn't just a generated wrapper.

**Files and tools (all in `tools/` — pick a sensible file per tool):**

- `current_state() -> dict` (in `tools/inspection.py`)
  - Returns `{"exec_state", "pc", "frame", "driver"}` in one call. Saves the agent constantly chaining 3 tools.

- `summarize_memory_map() -> dict` (in `tools/inspection.py`)
  - For each device that has address spaces: list spaces with their address ranges. Returns:
    ```
    {"devices": [{"tag": ":maincpu", "spaces": [{"name":"program","start":0,"end":0xffffff,"width":...}, ...]}, ...]}
    ```
  - Description: "Use this once at the start of a session to learn the topology before reading memory."

- `disassemble_around_pc(before: int = 4, after: int = 12, device: str = ":maincpu") -> dict` (in `tools/memory.py`)
  - Reads CURPC, disassembles a window, returns `{"pc": int, "lines": [{"address","text","is_current"}]}`.
  - Implementation note: `bridge.disassemble()` doesn't take a "before" count, so step backwards by sizing the typical instruction width for the CPU family — or just disassemble from `pc - before*max_insn_size` and trim. **Easier:** disassemble from `pc - before*4` and walk forward, marking the line whose address equals `pc`. For SCC68070 max insn is 10 bytes; for Z80 it's 4. A safe default is `before * 4` and trust the trim. Document that this is heuristic.

- `run_until_breakpoint(address: int | str, device: str = ":maincpu", timeout: float = 10.0) -> dict` (in `tools/execution.py`)
  - Wraps the existing `bridge.run_until` helper.
  - Returns `{"pc": int, "stopped": true}` on hit; raises a clear MCP error on timeout (already handled by `TimeoutError` mapping).
  - Description must mention: "Sets a one-shot breakpoint, runs, waits for the hit, then cleans up. Handles the case where PC is already at the target by stepping first."

- `run_for_frames(n: int, timeout: float | None = None) -> dict` (in `tools/execution.py`)
  - Implementation: read current frame, set timeout to `n * 0.05 + 2.0` if not provided, call `bridge.run()`, then poll `frame_number()` every 50ms until it reaches `start + n`, then `pause()`. Return `{"frame": int, "exec_state": "stop"}`.
  - Polling is fine — Phase 1 already validated periodic callbacks fire while paused, but here we want a simple "advance N frames" semantic.
  - Document: "Frame counts are at the screen's native rate (~50/60 Hz)."

- `tap_and_run(address: int | str, length: int, frames: int, kind: str = "write", device: str = ":maincpu", space: str = "program", capacity: int | None = None) -> dict` (in `tools/taps.py`)
  - `kind` is `"read"` or `"write"`.
  - Install tap → `run_for_frames(frames)` → `read_tap_buffer(drain=True)` → `remove_tap`. Use try/finally so the tap is always removed even on error.
  - Returns `{"entries": [...], "drops": int, "frames_run": int}`.
  - If `drops > 0`, append a `"warning"` field: `"Buffer overflowed (N drops). Reduce frames or length, or pass a larger capacity."`

- `find_writes_to(address: int | str, length: int, frames: int, device: str = ":maincpu", space: str = "program") -> dict` (in `tools/taps.py`)
  - Thin projection over `tap_and_run(kind="write", ...)`. Returns the same shape.
  - Exists because the *name* is what the agent searches for.

- `find_reads_of(address: int | str, length: int, frames: int, device: str = ":maincpu", space: str = "program") -> dict` (in `tools/taps.py`)
  - Same as above with `kind="read"`.

**Acceptance criteria:**
- `pytest mamebridge_mcp/tests/test_taps.py -v` exercises `tap_and_run`, `find_writes_to`, `find_reads_of` with a small write-to-RAM scenario in pm3585 (any RAM range that gets touched during boot).
- `test_execution.py` covers `run_until_breakpoint` (set bp at a known boot address, hit it, return) and `run_for_frames(5)` (frame advances by ≥5).
- `test_inspection.py` covers `current_state` and `summarize_memory_map`.
- `disassemble_around_pc` test: at a known PC, returns a list with exactly one `is_current=True` entry.

**Gate:** show user the composites' descriptions and test output.

---

### Step 4 — Driver flags (Lua-side patch + tool surfacing)

**Scope:** One small additive change to the Lua plugin so `driver_info` includes a `flags` field. Then surface it through `get_driver_info`.

**Files:**

- [plugins/mcp/handlers/machine.lua](plugins/mcp/handlers/machine.lua) — extend the `driver_info` handler to include a `flags` table. Source from `manager.machine.system` — common fields are booleans like `unemulated_protection`, `imperfect_protection`, `imperfect_graphics`, `imperfect_sound`, `no_sound`, `not_working`, `mechanical`, `is_bios_root`. **Verify the exact field names** against `luaengine.cpp` (or print the table from a test session). If the names differ, use what's actually exposed.
- [mamebridge/client.py](mamebridge/client.py) — extend the `DriverInfo` dataclass with `flags: dict[str, bool]` (default empty dict for backwards compat). Update the `driver_info()` method to populate it. This is the **only** Phase 1 client change.
- `mamebridge_mcp/tools/inspection.py` — `get_driver_info` returns a dict including `flags`. Description tells the agent how to read them: "`not_working=true` means trust nothing; `imperfect_graphics=true` means visual glitches are expected and not necessarily your bug."

**Acceptance criteria:**
- `pytest mamebridge/tests/` (the Phase 1 suite) still passes.
- A new `test_inspection.py` case asserts `get_driver_info` returns a `flags` dict with at least one boolean key.
- No wire protocol version bump — this is a pure additive field.

**Gate:** show the Lua diff and the Phase 1 test output (proof you didn't break anything).

---

### Step 5 — The skill

**Scope:** Write `skills/mame-driver-dev/SKILL.md` to teach the agent MAME conventions. This is real writing, not a stub. The skill is what makes the difference between "agent fumbles around with bp_set" and "agent knows to call summarize_memory_map first."

**File:** `skills/mame-driver-dev/SKILL.md`. Use Anthropic skill frontmatter:

```markdown
---
name: mame-driver-dev
description: Use this skill when working on a MAME driver via the mamebridge MCP server. Covers MAME-specific conventions: device tags, address spaces, expression syntax, breakpoint vs watchpoint semantics, and the tap_and_run pattern for tracing memory access.
---
```

**Sections to cover (write them, don't just outline):**

1. **Start every session with these two calls.** `summarize_memory_map()` then `current_state()`. Tells the agent the topology and where execution is.
2. **Device tags.** `:maincpu` is almost always right. Multi-CPU systems (CD-i has `:maincpu` SCC68070 and a `:slave` MCU) need explicit tags. `list_devices()` is the source of truth.
3. **Address spaces.** `program`, `data`, `io` — not all devices have all of them. `list_address_spaces(device)` shows what's there. Reading the wrong space silently returns garbage; this is a common mistake.
4. **Expression syntax for breakpoint conditions.** `maincpu.pb@0x4000` reads a byte. `pb` = program byte, `pw` = word, `pd` = dword; same for `db`/`dw`/`dd` (data space). Conditions are C-like: `maincpu.pb@0x4000 == 0xff`.
5. **Breakpoint vs watchpoint.** `bp_set` stops when *the CPU executes the instruction at this PC*. `wp_set` stops when *this address in memory is read or written*. To find "what writes to X", you almost always want `find_writes_to(X, ...)` (a tap), not a watchpoint, because a watchpoint stops execution and a tap doesn't.
6. **The tap_and_run pattern.** This is the workhorse for "what touches address X during boot/playback". `find_writes_to(0xd800, 0x20, 60)` runs for 60 frames, returns every PC that wrote into the range. Then disassemble each PC to understand the writers.
7. **Driver flags.** Read `get_driver_info().flags` once. `not_working` means treat all output as suspect. `imperfect_graphics` / `imperfect_sound` mean glitches are a known issue — don't waste time debugging them unless they're your goal.
8. **Common mistakes.**
   - Setting a breakpoint at the current PC and calling `resume()` instead of `run_until_breakpoint()` — the bp doesn't re-trigger on the instruction the CPU is already sitting on. (`run_until_breakpoint` handles this.)
   - Reading from `program` when the data is in `data` (e.g. on Harvard-architecture devices).
   - Forgetting that `start_session` must include `-debug` in `flags` for breakpoints to work.
9. **When to use `exec_mame_command`.** Almost never. It returns unstructured text. Use it only when no structured tool fits — e.g. obscure MAME debugger commands like `history` or `traceover`.

Keep total length under ~3000 words. Concrete examples > prose.

**Acceptance criteria:**
- File exists with valid frontmatter.
- Each numbered section is present and substantive.
- At least three concrete tool-call examples are inline.

**Gate:** show user the skill in full for review.

---

### Step 6 — End-to-end acceptance test

**Scope:** Prove the success criterion from CLAUDE.md: "An agent can be given a degraded `pm3585` driver and a goal like 'find what address the boot ROM checks for the CD presence' and reach a useful answer through tool calls without hand-holding."

You won't actually run an LLM here. Write a **scripted** end-to-end test that exercises the same tool sequence a competent agent would, proving every tool in the chain works against pm3585 without manual intervention.

**File:** `mamebridge_mcp/tests/test_end_to_end.py`.

**Scenario:**
1. `start_session("pm3585", flags=["-debug","-window","-nomaximize"])`.
2. `summarize_memory_map()` — assert it has at least `:maincpu` with a `program` space.
3. `get_driver_info()` — assert `shortname == "pm3585"`, `flags` is a dict.
4. `current_state()` — assert `exec_state == "stop"` (debug mode starts paused).
5. `run_for_frames(10)` — assert frame count advanced.
6. `dump_registers()` — assert PC is non-zero.
7. `disassemble_around_pc(before=2, after=8)` — assert returns ≥10 lines and one `is_current=True`.
8. `find_writes_to(<some RAM address known to be touched during boot>, 0x10, 30)` — assert returns at least one entry. (Pick an address from CD-i RAM map; if unknown, just pick the start of work RAM and assert *either* entries or a clean empty result.)
9. `bp_set(<some ROM address>)` then `run_until_breakpoint(<same address>, timeout=5.0)` — assert it hits.
10. `bp_clear(...)`, `stop_session()`.

**Acceptance criteria:**
- `pytest mamebridge_mcp/tests/test_end_to_end.py -v` passes from a clean state.
- No manual cleanup needed between runs.

**Gate:** show user the test output. Phase 2 is done.

---

## Style requirements

- **Tool descriptions are the deliverable.** A tool with a clear description is worth ten with bad ones. Re-read every description after writing it and ask: would a fresh LLM, with no other context, know when to use this and how to call it correctly? If not, fix it.
- **Type-hint every tool argument and return.** FastMCP derives schemas from hints.
- **No comments in code except the rare WHY-comment.** The CLAUDE.md project conventions apply to your code too. Tool descriptions are docstrings (those are required); inline comments are not.
- **Don't catch exceptions you can't usefully translate.** Let `MameBridgeError` propagate to the FastMCP-level handler; that's where the message translation lives.
- **No backwards-compat shims for code that doesn't exist yet.** This is greenfield.

---

## Things to deliberately not do (echo of CLAUDE.md, restated for your context)

- Don't fork or patch MAME (C++).
- Don't modify the JSON-RPC wire protocol.
- Don't add streaming.
- Don't multi-client the bridge.
- Don't hardcode pm3585 / SCC68070 / CD-i specifics outside `tests/test_end_to_end.py`.
- Don't write README.md or other docs unless the user asks.
- Don't add a "this tool is dangerous, are you sure?" confirmation layer to `exec_mame_command`. The description warning is sufficient for Phase 2.

---

## When you're done

End-of-Phase-2 deliverable for the user:
- `mamebridge_mcp/` package with all tools.
- `skills/mame-driver-dev/SKILL.md`.
- All tests passing: `pytest` from repo root.
- One-paragraph summary in your final message: which tools shipped, what the end-to-end test proves, and any items you punted on (and why).
