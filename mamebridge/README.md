# mamebridge

Async Python client for controlling MAME via the MCP JSON-RPC bridge.

The system has two parts:

1. **Lua plugin** (`plugins/mcp/`) — runs inside MAME and exposes a JSON-RPC 2.0
   server over TCP.
2. **Python package** (`mamebridge/`) — connects to that server and provides a
   typed async API.

## Requirements

- Python 3.11+
- MAME built with Lua plugin support
- The `mcp` plugin installed in MAME's `plugins/` directory

## Installation

From the repository root:

```bash
pip install -e '.[dev]'
```

## Quick start

### Launching MAME and connecting

```python
import asyncio
from mamebridge import launch_mame

async def main():
    async with launch_mame("pm3585", mame_binary="./mame") as bridge:
        info = await bridge.ping()
        print(info["mame_version"])

        pc = await bridge.read_reg("PC")
        print(f"PC = 0x{pc:08x}")

        data = await bridge.read_mem(pc, 16)
        print(f"bytes at PC: {data}")

asyncio.run(main())
```

`launch_mame` spawns MAME as a subprocess, waits for the bridge socket to
accept connections, and yields a connected `MameBridge`. On context-manager
exit it sends `machine:exit()` for a clean shutdown, falling back to
SIGTERM/SIGKILL if needed.

### Connecting to an already-running MAME

```python
from mamebridge import MameBridge

async def main():
    async with MameBridge.connect("127.0.0.1", 8080) as bridge:
        await bridge.step()
```

### Synchronous wrapper

For REPLs and simple scripts that don't need asyncio:

```python
from mamebridge.sync import SyncMameBridge

bridge = SyncMameBridge.connect()
print(bridge.ping())
bridge.close()
```

## Configuration

| Variable | Default | Description |
|---|---|---|
| `MAME_MCP_PORT` | `8080` | TCP port the Lua plugin listens on |
| `MAME_BINARY` | `mame` | Path to MAME executable (used by tests) |
| `MAME_DRIVER` | `pm3585` | System driver to launch (used by tests) |

Pass `port=` to `launch_mame()` or `MameBridge.connect()` to override
the port in code. `launch_mame` sets `MAME_MCP_PORT` in the subprocess
environment automatically.

MAME must be started with `-plugin mcp` (the launcher does this for you).
Debugger methods (`step`, `bp_set`, `wp_set`, etc.) require `-debug`.

## API reference

All methods are `async`. Parameters with defaults are keyword-only.

### Machine

| Method | Returns | Description |
|---|---|---|
| `ping()` | `dict` | Heartbeat — returns API version and MAME version |
| `list_devices()` | `list[dict]` | All devices in the emulated system |
| `list_spaces(device)` | `list[dict]` | Address spaces for a device |
| `driver_info()` | `DriverInfo` | System metadata (name, manufacturer, year) |
| `list_handlers()` | `list[str]` | All registered JSON-RPC method names |
| `quit()` | `None` | Ask MAME to exit gracefully |

### Memory

| Method | Returns | Description |
|---|---|---|
| `read_mem(address, length, *, device, space, unit_size)` | `list[int]` | Read memory (unit_size: 1/2/4/8) |
| `write_mem(address, data, *, device, space)` | `int` | Write bytes, returns count written |
| `search_mem(address, length, pattern, *, device, space)` | `list[int]` | Search for byte pattern, returns matching addresses |
| `install_read_tap(address, length, *, device, space, capacity)` | `TapInfo` | Install a passive read tap |
| `install_write_tap(address, length, *, device, space, capacity)` | `TapInfo` | Install a passive write tap |
| `read_tap_buffer(tap_id, *, max_entries, drain)` | `TapBuffer` | Read captured tap entries |
| `remove_tap(tap_id)` | `None` | Remove a tap |

### CPU / Registers

| Method | Returns | Description |
|---|---|---|
| `read_reg(name, *, device)` | `int` | Read a CPU register |
| `write_reg(name, value, *, device)` | `None` | Write a CPU register |
| `list_regs(*, device)` | `list[Register]` | All registers with current values |
| `disassemble(address, count, *, device)` | `list[DisassemblyLine]` | Disassemble instructions |

### Debugger (requires `-debug`)

| Method | Returns | Description |
|---|---|---|
| `exec_state()` | `str` | `"run"` or `"stop"` |
| `step(count, *, device)` | `int` | Step instruction(s), returns new PC |
| `step_over(*, device)` | `None` | Step over subroutine call |
| `step_out(*, device)` | `None` | Step out of current subroutine |
| `run()` | `None` | Resume execution |
| `pause()` | `None` | Break into debugger |
| `bp_set(address, *, device, condition, action, oneshot)` | `Breakpoint` | Set a breakpoint |
| `bp_clear(bp_id)` | `None` | Remove a breakpoint |
| `bp_enable(bp_id, enabled)` | `None` | Enable/disable a breakpoint |
| `bp_list(*, device)` | `list[Breakpoint]` | List breakpoints |
| `wp_set(address, length, type, *, device, space, condition, action)` | `Watchpoint` | Set a watchpoint (`"r"`, `"w"`, or `"rw"`) |
| `wp_clear(wp_id)` | `None` | Remove a watchpoint |
| `wp_enable(wp_id, enabled)` | `None` | Enable/disable a watchpoint |
| `wp_list(*, device)` | `list[Watchpoint]` | List watchpoints |

### State

| Method | Returns | Description |
|---|---|---|
| `save_state(name)` | `str` | Save emulation state |
| `load_state(name)` | `None` | Load emulation state |
| `screenshot(*, screen)` | `bytes` | Capture screen as PNG |
| `frame_number()` | `int` | Current video frame number |

### Raw

| Method | Returns | Description |
|---|---|---|
| `exec_command(command)` | `str` | Execute a debugger console command, returns output |

### High-level helpers

| Method | Returns | Description |
|---|---|---|
| `run_until(address, *, device, timeout)` | `int` | Run until PC hits address (sets temp breakpoint) |

## Events

The bridge emits notifications when breakpoints are hit. Subscribe with
`events()`:

```python
sub = bridge.events("event.breakpoint")

await bridge.bp_set(0x1000)
await bridge.run()

async for event in sub:
    print(f"hit breakpoint {event.params['id']} at PC=0x{event.params['pc']:x}")
    break

sub.close()
```

Or wait for a single event:

```python
event = await bridge.wait_for("event.breakpoint", timeout=5.0)
```

## Error handling

All errors are subclasses of `MameBridgeError`:

| Exception | JSON-RPC code | When |
|---|---|---|
| `MethodNotFound` | -32601 | Unknown RPC method |
| `InvalidParams` | -32602 | Missing/wrong parameter types |
| `DebuggerNotEnabled` | -32001 | Debugger method called without `-debug` |
| `DeviceNotFound` | -32002 | Bad device tag |
| `InvalidAddressSpace` | -32003 | Bad address space name |
| `BreakpointNotFound` | -32004 | Bad breakpoint/watchpoint ID |
| `SystemNotRunning` | -32005 | Operation requires running system |
| `InvalidAddress` | -32006 | Address out of range |
| `TapNotFound` | -32007 | Bad tap ID |
| `ConnectionError` | — | TCP connection failed or lost |
| `TimeoutError` | — | `run_until` timed out |
| `MameDied` | — | MAME process exited unexpectedly |

```python
from mamebridge.errors import DeviceNotFound

try:
    await bridge.read_reg("PC", device=":nonexistent")
except DeviceNotFound as e:
    print(e)
```

## Running tests

```bash
pip install -e '.[dev]'
pytest mamebridge/tests/ -v
```

Tests require a MAME binary and ROMs for the configured driver. Set
`MAME_BINARY` and `MAME_DRIVER` environment variables if the defaults
don't match your setup.

## Protocol

The bridge uses newline-delimited JSON-RPC 2.0 over a single TCP
connection on the configured port. One connection at a time — MAME's Lua
socket accepts a single client.

Request:
```json
{"jsonrpc": "2.0", "id": 1, "method": "read_reg", "params": {"name": "PC"}}
```

Response:
```json
{"jsonrpc": "2.0", "id": 1, "result": {"value": 65536}}
```

Notification (server → client):
```json
{"jsonrpc": "2.0", "method": "event.breakpoint", "params": {"id": 1, "pc": 4096}}
```
