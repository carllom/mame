---
name: mame-driver-dev
description: Use this skill when working on a MAME driver via the mamebridge MCP server. Covers MAME-specific conventions: device tags, address spaces, expression syntax, breakpoint vs watchpoint semantics, and the tap_and_run pattern for tracing memory access.
---

# MAME Driver Development with mamebridge

You are debugging or improving a MAME emulation driver. MAME is running with the bridge plugin active. You have structured tools for execution control, memory access, disassembly, breakpoints, watchpoints, and memory taps. Use them in the order and combinations described here.

---

## 1. Start every session with these two calls

Always run these before anything else:

```
summarize_memory_map()
current_state()
```

`summarize_memory_map` gives you the address topology — which devices exist, which address spaces each has, and the full address range of each space. Without this, you will guess wrong about address ranges and silently read garbage.

`current_state` tells you where execution is right now: `exec_state` ("run" or "stop"), current `pc`, `frame` count, and `driver` name. In debug mode (`-debug` flag) MAME starts paused — `exec_state` will be `"stop"`.

Example:

```
summarize_memory_map()
→ {"devices": [{"tag": ":maincpu", "spaces": [{"name": "program", "start": 0, "end": 16777215, "width": 32}, ...]}, ...]}

current_state()
→ {"exec_state": "stop", "pc": 2162688, "frame": 0, "driver": "pm3585"}
```

---

## 2. Device tags

Every device in MAME has a tag — a colon-prefixed path like `:maincpu`, `:slave`, `:soundchip`. All tools that operate on a device accept a `device` parameter; it defaults to `":maincpu"`, which is correct for single-CPU systems.

Multi-CPU systems need explicit tags. The Philips CD-i (`pm3585`) has `:maincpu` (SCC68070) and a `:slave` MCU. If a tool returns "device not found", you have the wrong tag.

**Use `list_devices()` as the authoritative source of tags**, not the driver source code (the Lua API sees the actual instantiated devices, not what the C++ registers).

```
list_devices()
→ {"devices": [{"tag": ":maincpu", ...}, {"tag": ":slave", ...}, ...]}
```

---

## 3. Address spaces

Each device can have multiple address spaces: `"program"`, `"data"`, `"io"` are common names, but the actual names depend on the CPU family. Not all devices have all three. Reading from the wrong space silently returns whatever bytes happen to map there — you will not get an error.

**Always verify the space name with `list_address_spaces(device)` before reading memory on hardware you haven't worked with before.**

```
list_address_spaces(device=":maincpu")
→ {"spaces": [{"name": "program", "address_width": 24, "data_width": 32, ...}]}
```

On Harvard-architecture CPUs (Z80, MCS-51, etc.), ROM is in `"program"` and RAM is often in `"data"`. Reading `"program"` when the runtime data is in `"data"` is one of the most common mistakes.

---

## 4. Expression syntax for breakpoint conditions

MAME debugger expressions use `device.spaceXbyte@address` notation:

- `maincpu.pb@0x4000` — read 1 byte from `:maincpu` program space at 0x4000
- `maincpu.pw@0x4000` — read 1 word (2 bytes)
- `maincpu.pd@0x4000` — read 1 dword (4 bytes)
- `maincpu.db@0x4000` — data space byte (use `d` prefix for data space)

Conditions are C-like boolean expressions:

```python
bp_set(0x8040, condition="maincpu.pb@0x200c == 0x01")
```

This breakpoint fires only when the byte at 0x200c in program space equals 0x01 at the moment the CPU reaches 0x8040.

Breakpoint actions (the `action` param) are semicolon-separated debugger commands run on every hit. They're useful for tracing without stopping:

```python
bp_set(0x8040, action="print maincpu.pb@0x200c; go")
```

---

## 5. Breakpoint vs watchpoint — when to use which

**`bp_set(address)`** — stops when the CPU *executes the instruction at this address*. Use for: "I want to intercept a function call", "I want to see CPU state when this code path runs."

**`wp_set(address, length, type="rw")`** — stops when *this memory range is read or written*. Use for: "I want to catch any access to a hardware register", "I want to see when a variable changes." Stops execution every time the access happens, which can be very slow on hot paths.

**`find_writes_to(address, length, frames)`** — runs for N frames collecting every write to the range via a tap, then returns the full list of `{addr, value, mask}` entries without ever stopping execution. Use this to see *what values* are being written and *which sub-addresses* in the range are touched, over time, without halting the machine. **Tap entries do not include the writer's PC** — that's a constraint of MAME's Lua tap binding (would need a Phase 4 patch). To find the *code* that performs a write, use `wp_set` and read PC when it hits.

Rule of thumb:
- "Is address X being written, and with what values?" → `find_writes_to(X, length, frames)`
- "What code writes to address X?" → `wp_set(X, length, type="w")` + `resume()` + read PC when it stops
- "What is the CPU state when function F is called?" → `bp_set` + `run_until_breakpoint`
- "Watch a hot register and stop on every access" → `wp_set` (but expect it to be slow)

---

## 6. The tap_and_run pattern

Taps are the primary tool for non-intrusive memory tracing. A tap installs a callback on a memory range; every read or write is recorded in a ring buffer without stopping the CPU. Run MAME for N frames, then drain the buffer.

**`find_writes_to(address, length, frames)`** is the one-liner:

```
find_writes_to(0x200000, 0x100, 60)
→ {"entries": [{"addr": 0x200010, "value": 255, "mask": 65535}, ...], "drops": 0}
```

Each entry tells you *which sub-address* in the range was touched, *what value* was written, and the *byte-enable mask* (which bytes of the value are valid for variable-width buses). What it does **not** tell you is *which instruction* performed the write — MAME's Lua tap callback does not expose the writer's PC. To get that, use `wp_set` instead (see Section 5).

Use taps when you want to:
- Confirm an address range is being touched at all (much faster than a watchpoint).
- See the pattern of values being written (e.g. "is the audio chip getting silence or noise?").
- Identify *which* sub-addresses in a range are hot.

**`find_reads_of(address, length, frames)`** is the read equivalent.

**`tap_and_run(address, length, frames, kind="write")`** is the underlying composite if you need more control (e.g. a custom capacity).

If `"drops"` is non-zero in the result, the ring buffer overflowed — reduce `frames`, reduce `length`, or pass `capacity=<larger_number>` to `tap_and_run` directly.

---

## 7. Driver flags

Call `get_driver_info()` once per session and read the `flags` dict:

```
get_driver_info()
→ {"shortname": "pm3585", "flags": {"not_working": false, "supports_save": true,
    "no_sound_hw": false, "mechanical": false, ...}}
```

Flag meanings:

| Flag | Meaning |
|---|---|
| `not_working` | Basic functionality is broken. Treat all emulation output as suspect. Don't spend time debugging symptoms — the driver itself is the bug. |
| `supports_save` | Safe to call `save_state`/`load_state`. If false, savestates may corrupt state. |
| `no_sound_hw` | No sound hardware in this system. Audio silence is expected. |
| `mechanical` | Pinball or similar mechanical game. Most digital logic tools don't apply. |
| `is_incomplete` | Driver is known unfinished. |
| `unofficial` | Prototype or unofficial driver. |

`imperfect_graphics` and `imperfect_sound` are not exposed by the Lua API in this MAME build — absence of those flags does not mean graphics/sound are perfect.

---

## 8. Common mistakes

**Breakpoint at current PC + resume doesn't re-trigger.**
If `current_state()` shows `pc=0x8040` and you call `bp_set(0x8040)` then `resume()`, the breakpoint does not fire — the CPU is already sitting on that instruction. Use `run_until_breakpoint(0x8040)` instead; it handles this by stepping first.

**Reading program space when data is in data space.**
`read_memory(0x200, 4)` reads from `space="program"` by default. On a Harvard CPU, runtime variables live in `space="data"`. If you read registers and they look wrong, check the space.

**Forgetting `-debug` in `start_session` flags.**
Breakpoints, watchpoints, step, and step_over require the MAME debugger (`-debug`). Without it, those tools return "MAME was not launched with -debug." Always pass `flags=["-debug", "-window", "-nomaximize"]`.

**`load_state` is asynchronous.**
`load_state("name")` schedules the restore for MAME's next frame — the PC visible immediately after the call is still the old PC. Call `run_for_frames(1)` then `current_state()` to see the restored state.

**Taking too many screenshots.**
Each screenshot is 50–500 KB inline base64. One or two per session is fine; don't loop over them.

**`wp_set` on a hot address.**
A watchpoint fires on every single access. A memory-mapped I/O register accessed thousands of times per frame will halt the machine constantly. Use `find_writes_to` or `find_reads_of` instead — but remember those don't give you the writer's PC; if you need the PC, you have to use `wp_set` and accept the slowness, or narrow the address range first with a tap.

**Expecting tap entries to include PC or frame.**
They don't. `find_writes_to` / `find_reads_of` / `tap_and_run` return `{addr, value, mask}` only. This is a MAME Lua-binding constraint, not a bridge bug. To attribute a write to specific code, use `wp_set` + read PC at the hit.

---

## 9. When to use `exec_mame_command`

Almost never. It sends a raw string to the MAME debugger console and returns unstructured text that you must parse. Use it only for:
- MAME debugger commands with no structured tool equivalent (e.g. `history`, `traceover`, `logerror`).
- One-off diagnostics during exploration where you don't need a structured result.

Do not use it to implement something a structured tool already does — `exec_mame_command("g")` instead of `resume()` means you lose the typed return value and error handling.

---

## Quick-start checklist

1. `start_session(driver, flags=["-debug", "-window", "-nomaximize"])`
2. `summarize_memory_map()` — learn the topology
3. `current_state()` — confirm paused at boot
4. `get_driver_info()` — check flags before diving in
5. `find_writes_to(suspect_address, length, 60)` — confirm the address is being touched, see the values
6. `wp_set(suspect_address, length, type="w")` then `resume()` — stop when something writes, capture the writer's PC via `current_state()`
7. `run_until_breakpoint(writer_pc)` after restart — stop on the writer in a fresh run
8. `disassemble_around_pc()` — see context
9. `dump_registers()` — inspect CPU state
10. `stop_session()`
