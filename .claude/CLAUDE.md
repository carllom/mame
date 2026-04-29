# MAME MCP Bridge — Project Handoff

## What this project is

An MCP (Model Context Protocol) server that lets an LLM agent drive MAME for **emulation driver development**. The agent gets tools for execution control, breakpoints, watchpoints, memory/register access, disassembly, taps, screenshots, and savestates — the same surface a human driver developer uses, exposed as MCP tools.

**First user / target:** Philips CD-i driver development (`pm3585`, SCC68070 CPU). The bridge must not hardcode CPU-family or driver-specific assumptions.

## Architecture (decided, do not relitigate)

Three layers, top to bottom:

1. **MCP server (`mamebridge_mcp/`)** — ✅ Phase 2 done. FastMCP-based, stdio transport, 46 tools across session/inspection/memory/execution/breakpoints/state/taps/raw. Skill at `skills/mame-driver-dev/SKILL.md`.
2. **Python client library (`mamebridge`)** — ✅ Phase 1 done. Async API over JSON-RPC, dataclass return types, full handler coverage, pytest suite, launcher for subprocess management.
3. **Lua bridge plugin (`plugins/mcp/`)** — ✅ Phase 1 done. Lives inside MAME, registered handlers dispatch from JSON-RPC over TCP. Uses `emu.file` for socket (not LuaSocket).

**Why this split, not a C++ patch into MAME:** MAME's Lua API already covers ~95% of what we need (debugger commands, memory, registers, taps, screenshots, savestates). Forking MAME would mean tracking monthly upstream releases. Lua plugin + Python adapter lets us iterate fast with no MAME rebuild per change. If something is genuinely missing from the Lua API, the right fix is a small upstream patch to MAME's bindings, not C++ in our repo.

**Why the MCP / client-library split:** The Python client is independently testable (no LLM in the loop) and useful standalone for scripted regression tests / headless driver fuzzing. MCP-shaped tool design (descriptions, schemas tuned for an LLM) is genuinely different work and benefits from being its own phase.

## Status

- ✅ **Phase 0 (spike)** — proved sockets work, periodic callbacks fire while paused, breakpoint round-trip works end-to-end. Throwaway code.
- ✅ **Phase 1 (protocol + Python client)** — bridge plugin promoted from autoboot script to proper plugin, JSON-RPC 2.0 wire protocol, full handler catalog, async Python client with typed dataclasses, pytest suite passing, launcher with subprocess management.
- ✅ **Phase 2 (MCP server)** — FastMCP server (`mamebridge_mcp/`) wrapping the Phase 1 client as 46 MCP tools. Composites (`run_until_breakpoint`, `tap_and_run`, `find_writes_to`, `summarize_memory_map`, etc.) sit alongside thin 1:1 wraps. Skill at `skills/mame-driver-dev/SKILL.md` teaches MAME conventions. End-to-end test exercises the full agent-style flow against pm3585.
- ⏭ **Phase 3 (agent UX iteration)** — next. Use the MCP server on real driver tasks (pm3585 first, then MV-30 LP-1 work) and iterate on tool descriptions, error messages, and the skill based on where the agent stumbles.
- ⏭ **Phase 4 (upstream patches if needed)** — only when Phase 3 surfaces a capability gap that can't be wrapped from existing Lua bindings. **Known candidate:** add PC (and ideally frame counter) to MAME's Lua tap callback so `tap_and_run` results can attribute writes to specific code without falling back to `wp_set`.

## Key technical facts (validated against MAME 0.287, custom `mametinyd` build)

These came out of Phase 0/1 implementation. Don't rediscover them.

**Launch flags:**
- `-debug` is **required** for the debugger half of the API. Without it, `manager.machine.debugger` is nil.
- `-debug` also bypasses the gameinfo / WIP-warning screens automatically (UI checks `DEBUG_FLAG_ENABLED` and short-circuits). Belt-and-suspenders: also pass `-skip_gameinfo`.
- `-console` (Lua REPL) is **not** needed at runtime, useful only for interactive bridge development.
- Standard production launch: `mame <driver> -debug -window -nomaximize -skip_gameinfo`. The plugin auto-loads via `plugin.json` `start=true`.

**API gotchas:**
- `cpu.debug:bpset(addr)` requires all 3 args explicitly: `bpset(addr, "", "")` — Sol3 doesn't propagate C++ defaults. Always pass condition and action even when empty.
- `cpu.debug:go()` requires a target PC arg. Use `manager.machine.debugger:command("go")` instead.
- `cpu.debug:step(1)` works fine with explicit count.
- Setting a breakpoint at the current PC and resuming does **not** re-trigger — CPU is sitting on the instruction. The bridge's `run_until` handles this correctly by single-stepping if PC == target before resuming. Document this in tool descriptions for Phase 2.
- `register_periodic` fires while system is **stopped** at a breakpoint — confirmed. The whole architecture relies on this.
- `execution_state` returns `"run"` or `"stop"` as strings.
- JSON library: `require("json")` works (dkjson, bundled with MAME).
- Socket mechanism: `emu.file("", 7)` + `open("socket.127.0.0.1:PORT")` creates a listening socket. `emu.file` is MAME-internal, more stable than depending on LuaSocket. Read is non-blocking, returns `""` when no data.


**Wire protocol:**
- JSON-RPC 2.0, newline-delimited, single message per line.
- Error codes: standard JSON-RPC range (-32700 to -32603) plus app range -32000 to -32099 (-32000 debugger not enabled, -32001 device not found, -32002 invalid address space, -32003 breakpoint not found, -32004 system not running).
- Notifications use method names like `event.breakpoint`, `event.watchpoint`. No `id` field.
- Single client only for now. Multi-client is Phase 3+ if ever.

**Breakpoint event mechanism:**
- Implementation pattern: bp action writes a sentinel; periodic tick checks for it and emits `event.breakpoint` with full payload (id, pc, device).
- Phase 0's "poll execution_state" approach was a hack that produced false positives — Phase 1 replaced it. Don't regress.

**Tap callback fields:**
- MAME's Lua `install_read_tap` / `install_write_tap` callbacks expose only `{addr, value, mask}`. **No PC, no frame counter.** This propagates all the way up through `read_tap_buffer`, `tap_and_run`, `find_writes_to`, `find_reads_of`.
- Practical consequence: taps tell you *what* and *where*, never *who*. To attribute a write to specific code, use `wp_set` (slow but accurate) instead.
- Adding PC to tap entries is a Phase 4 trigger — it requires patching MAME's `luaengine.cpp` tap binding to pass `device.state.curpc` (or equivalent) into the callback. Don't try to fake it from `read_reg("PC")` at dispatch time; the CPU has moved on by then.

## Repository layout (post-Phase-2)

```
plugins/mcp/
  plugin.json
  init.lua
  rpc.lua
  handlers/
    cpu.lua        # registers, disassembly
    debugger.lua   # bp/wp, step/run/pause, run_until
    machine.lua    # ping, version, list_devices, list_spaces, driver_info (incl. flags)
    memory.lua     # read/write/search/taps
    raw.lua        # exec_command escape hatch
    state.lua      # save/load state, screenshot

mamebridge/        # Python client library (pip-installable)
  client.py        # MameBridge async context manager
  errors.py        # exception hierarchy mapped to error codes
  events.py        # async event subscription
  launcher.py      # launch_mame async context manager
  protocol.py      # JSON-RPC framing
  py.typed
  tests/           # pytest suite, real MAME instance per module
  README.md

mamebridge_mcp/    # MCP server (Phase 2)
  __main__.py      # python -m mamebridge_mcp
  server.py        # FastMCP instance + tool registration
  session.py       # singleton Session: start/attach/stop/status
  schemas.py       # address coercion, hex bytes
  errors.py        # MameBridgeError → agent-friendly messages
  tools/           # session, inspection, memory, execution, breakpoints, taps, state, raw
  tests/           # in-process MCP client + real MAME, incl. test_end_to_end.py

skills/
  mame-driver-dev/SKILL.md   # MAME conventions for the agent

pyproject.toml     # `pip install -e .[mcp]` for the MCP extra
```

## What Phase 2 shipped

The full MCP server lives in `mamebridge_mcp/`. See [mcp_phase_2.md](../mcp_phase_2.md) for the original plan; the implementation followed it with one substantive deviation (tap entries omit PC/frame — see "Tap callback fields" above).

**Tool inventory (46 tools):**
- **Session lifecycle:** `start_session`, `attach_session`, `stop_session`, `session_status`. `attach_session` does *not* kill the external MAME on `stop_session`.
- **Inspection:** `list_devices`, `list_address_spaces`, `list_registers`, `get_driver_info` (includes `flags`), `current_state`, `summarize_memory_map`.
- **Memory:** `read_memory` (default `format="hex"` → `"a9 00 8d"`), `write_memory`, `disassemble`, `disassemble_around_pc`, `read_register`, `write_register`, `dump_registers`.
- **Execution:** `step`, `step_over`, `step_out`, `pause`, `resume`, `exec_state`, `run_for_frames`, `run_until_breakpoint`.
- **Breakpoints / watchpoints:** `bp_set`, `bp_clear`, `bp_enable`, `bp_list`, `wp_set`, `wp_clear`, `wp_enable`, `wp_list`.
- **State:** `save_state`, `load_state`, `screenshot` (inline `ImageContent`), `frame_number`.
- **Taps:** `install_read_tap`, `install_write_tap`, `read_tap_buffer`, `remove_tap`, `tap_and_run`, `find_writes_to`, `find_reads_of`. Entries are `{addr, value, mask}` only.
- **Escape hatch:** `exec_mame_command`.

**Resolved decisions** (all of these are now load-bearing — don't reopen without a reason):
- Memory format default: `hex` string, `int_array` opt-in.
- Address inputs: ints + coerced strings (`"0xd800"`, `"d800h"`).
- Screenshots: inline base64 PNG, no path option.
- Skill > inline system prompt.
- Wire protocol stays at `api_version=1` (Phase 2 added no wire changes; only `driver_info` got an additive `flags` field).

**Known limitation carried into Phase 3:** tap entries don't include the writer's PC. Workaround: use `wp_set` (slow but accurate) when attribution matters. Real fix is Phase 4.

## Things to deliberately not do

- **Don't fork MAME.** All bridge changes go in `plugins/mcp/`. Capability gaps go upstream as patches.
- **Don't add a C++ component.** If you find yourself wanting to, the answer is a Lua binding patch to upstream MAME instead.
- **Don't multi-client the bridge.** Single LLM session, single MAME instance, single connection. Locality is fine.
- **Don't reimplement breakpoints on top of taps** to avoid `-debug`. Just require `-debug`. The non-debug handlers exist for completeness, not as a primary path.
- **Don't hardcode CPU/driver specifics in the bridge or library.** The CD-i target is the test case, not a constraint. Default device tag is `:maincpu`, default space is whatever the device exposes first.
- **Don't add streaming / chunked responses.** Single JSON line per message. Big tap buffers fit fine at this scale.
- **Don't grow the wire protocol without bumping `api_version`.** Currently 1. Phase 2 might bump to 2 if MCP-shaped schema fields get added; that's fine, but make it explicit.

## Open questions for Phase 3

- **Tool description quality** — the only way to find out which descriptions are unclear is to put the MCP server in front of a real agent and watch it stumble. Iterate based on observed failure modes, not speculation.
- **Composite gaps** — Phase 2 shipped the obvious composites (`run_until_breakpoint`, `tap_and_run`, etc.). New ones should be added when a workflow keeps repeating across sessions. Don't preemptively design more.
- **Skill drift** — `skills/mame-driver-dev/SKILL.md` is good for Phase 2's surface but will need updates as new composites land or as the tap-PC limitation is lifted by a Phase 4 patch.
- **MV-30 use case** — the project's other active workstream is Roland MV-30 sound emulation (see user memory). Worth a session of using the MCP server on MV-30 driver work to find conventions that don't transfer from CD-i.

## How to resume

1. Read this file.
2. Confirm Phase 2 still works: `pip install -e .[mcp]` then `pytest mamebridge_mcp/tests/`.
3. For Phase 3 work: pick a real driver task, run the MCP server against it via an agent, and capture friction points. Update tool descriptions / skill / add composites accordingly.
4. For Phase 4 work: the most likely trigger is the tap-PC limitation. The fix is a small patch to MAME's `luaengine.cpp` tap binding to thread `device.state.curpc` into the callback args.

## Reference docs

- MAME Lua scripting reference: https://docs.mamedev.org/luascript/
- `luaengine.cpp` in MAME source — canonical truth for what's actually bound (the docs lag).
- MCP SDK / spec: https://modelcontextprotocol.io/

---

*Last updated: end of Phase 2. All three layers shipped; Phase 3 is iteration based on real agent use.*
