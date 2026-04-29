-- license:BSD-3-Clause
-- handlers/debugger.lua — breakpoints, watchpoints, execution control
-- Requires MAME to be launched with -debug.
--
-- Breakpoint/watchpoint event mechanism:
-- We track execution_state transitions from "run" to "stop" in the periodic
-- tick. When a stop is detected, we check the bridge-side bp/wp metadata tables
-- to determine which bp/wp was hit by matching the current PC against known
-- breakpoint addresses, or by inspecting the consolelog for watchpoint hit
-- messages. This is Option A from the design doc — we don't have a direct
-- stop callback in the Lua bindings.

local M = {}

-- Bridge-side metadata for breakpoints and watchpoints we've set
local bp_meta = {}   -- bp_index -> {address, device_tag, oneshot}
local wp_meta = {}   -- wp_index -> {address, length, device_tag, space, type}
local was_running = false
local consolelog_cursor = 0  -- track position in consolelog for wp detection

function M.register(rpc)
    local function get_debugger()
        return rpc.get_debugger()
    end

    local function require_debugger(rpc_mod)
        local dbg = get_debugger()
        if not dbg then
            return nil, rpc_mod.rpc_error(rpc_mod.DEBUGGER_NOT_ENABLED, "debugger not enabled")
        end
        return dbg, nil
    end

    local function get_cpu_debug(rpc_mod, params)
        local dbg, err = require_debugger(rpc_mod)
        if not dbg then return nil, nil, err end
        local tag = params.device or ":maincpu"
        local dev = manager.machine.devices[tag]
        if not dev then
            return nil, nil, rpc_mod.rpc_error(rpc_mod.DEVICE_NOT_FOUND, "device not found: " .. tag)
        end
        if not dev.debug then
            return nil, nil, rpc_mod.rpc_error(rpc_mod.DEBUGGER_NOT_ENABLED,
                "device has no debug interface: " .. tag)
        end
        return dev, dbg, nil
    end

    -- ── Event emission (called from rpc.tick via check_events) ──────

    -- Register a periodic check for run→stop transitions
    local function check_events()
        local dbg = get_debugger()
        if not dbg then return end

        local state = dbg.execution_state
        if was_running and state == "stop" then
            -- Determine what caused the stop
            local event_sent = false

            -- Check consolelog for watchpoint messages since last cursor
            local log = dbg.consolelog
            local log_len = #log
            for i = consolelog_cursor + 1, log_len do
                local line = log[i]
                -- Watchpoint hit messages look like:
                -- "Stopped at watchpoint N ..."
                local wp_id = line:match("Stopped at watchpoint (%d+)")
                if wp_id then
                    wp_id = tonumber(wp_id)
                    local meta = wp_meta[wp_id]
                    local cpu = manager.machine.devices[meta and meta.device_tag or ":maincpu"]
                    local pc = cpu and cpu.state and cpu.state["PC"] and cpu.state["PC"].value or 0
                    rpc.notify("event.watchpoint", {
                        id = wp_id,
                        pc = pc,
                        device = meta and meta.device_tag or ":maincpu",
                        address = meta and meta.address,
                        type = meta and meta.type,
                    })
                    event_sent = true
                end
            end
            consolelog_cursor = log_len

            -- If no watchpoint, check if a breakpoint was hit by matching PC
            if not event_sent then
                -- Find which CPU stopped — check all known bp devices
                local matched_bp = nil
                local matched_device = nil
                for bp_idx, meta in pairs(bp_meta) do
                    local dev = manager.machine.devices[meta.device_tag]
                    if dev and dev.state and dev.state["PC"] then
                        local pc = dev.state["PC"].value
                        if pc == meta.address then
                            -- Verify this bp is still set and enabled
                            local bp_obj = dev.debug:bplist()[bp_idx]
                            if bp_obj and bp_obj.enabled then
                                matched_bp = bp_idx
                                matched_device = meta.device_tag
                                break
                            end
                        end
                    end
                end

                if matched_bp then
                    local meta = bp_meta[matched_bp]
                    local dev = manager.machine.devices[meta.device_tag]
                    local pc = dev.state["PC"].value
                    rpc.notify("event.breakpoint", {
                        id = matched_bp,
                        pc = pc,
                        device = meta.device_tag,
                    })
                    -- Handle oneshot
                    if meta.oneshot then
                        dev.debug:bpclear(matched_bp)
                        bp_meta[matched_bp] = nil
                    end
                else
                    -- Generic stop (user pause, or unrecognized reason)
                    -- Try to get PC from visible cpu
                    local cpu = dbg.visible_cpu
                    local pc = 0
                    if cpu and cpu.state and cpu.state["PC"] then
                        pc = cpu.state["PC"].value
                    end
                    rpc.notify("event.stopped", {
                        pc = pc,
                        reason = "unknown",
                    })
                end
            end
        end

        if state == "run" then
            was_running = true
            -- Keep consolelog cursor updated while running
            local log = dbg.consolelog
            consolelog_cursor = #log
        elseif state == "stop" then
            was_running = false
        end
    end

    -- Register the event checker as a tick hook
    rpc.register_tick_hook(check_events)

    -- ── Execution state ─────────────────────────────────────────────

    rpc.register_method("exec_state", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        return {state = dbg.execution_state}
    end)

    -- ── Execution control ───────────────────────────────────────────

    rpc.register_method("step", function(params)
        local dev, dbg, err = get_cpu_debug(rpc, params)
        if not dev then return err end
        local count = params.count or 1
        dev.debug:step(count)
        return {pc = dev.state["PC"].value}
    end, {types = {count = "number", device = "string"}})

    rpc.register_method("step_over", function(params)
        local dev, dbg, err = get_cpu_debug(rpc, params)
        if not dev then return err end
        -- "over" debugger command steps over subroutine calls
        local tag = params.device or ":maincpu"
        dbg:command("over " .. tag)
        return {ok = true}
    end, {types = {device = "string"}})

    rpc.register_method("step_out", function(params)
        local dev, dbg, err = get_cpu_debug(rpc, params)
        if not dev then return err end
        local tag = params.device or ":maincpu"
        dbg:command("out " .. tag)
        return {ok = true}
    end, {types = {device = "string"}})

    rpc.register_method("run", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        was_running = true
        consolelog_cursor = #dbg.consolelog
        dbg:command("go")
        return {ok = true}
    end)

    rpc.register_method("pause", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        dbg.execution_state = "stop"
        return {ok = true}
    end)

    -- ── Breakpoints ─────────────────────────────────────────────────

    rpc.register_method("bp_set", function(params)
        local dev, dbg, err = get_cpu_debug(rpc, params)
        if not dev then return err end
        local cond = params.condition or ""
        local action = params.action or ""
        local idx = dev.debug:bpset(params.address, cond, action)
        local tag = params.device or ":maincpu"
        bp_meta[idx] = {
            address = params.address,
            device_tag = tag,
            oneshot = params.oneshot or false,
        }
        return {id = idx}
    end, {required = {"address"}, types = {address = "number", device = "string", condition = "string", action = "string"}})

    rpc.register_method("bp_clear", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        -- Need to find which device owns this bp
        local meta = bp_meta[params.id]
        if meta then
            local dev = manager.machine.devices[meta.device_tag]
            if dev and dev.debug then
                dev.debug:bpclear(params.id)
            end
            bp_meta[params.id] = nil
        else
            -- Try clearing on all CPU devices
            for tag, dev in pairs(manager.machine.devices) do
                if dev.debug then
                    local found = dev.debug:bpclear(params.id)
                    if found then break end
                end
            end
        end
        return {ok = true}
    end, {required = {"id"}, types = {id = "number"}})

    rpc.register_method("bp_enable", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        local meta = bp_meta[params.id]
        local tag = meta and meta.device_tag or params.device or ":maincpu"
        local dev = manager.machine.devices[tag]
        if dev and dev.debug then
            if params.enabled then
                dev.debug:bpenable(params.id)
            else
                dev.debug:bpdisable(params.id)
            end
        end
        return {ok = true}
    end, {required = {"id", "enabled"}, types = {id = "number"}})

    rpc.register_method("bp_list", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        local result = {}
        -- Iterate devices to find breakpoints
        local devices_to_check = {}
        if params.device then
            devices_to_check[params.device] = manager.machine.devices[params.device]
        else
            for tag, dev in pairs(manager.machine.devices) do
                if dev.debug then
                    devices_to_check[tag] = dev
                end
            end
        end
        for tag, dev in pairs(devices_to_check) do
            if dev.debug then
                local bps = dev.debug:bplist()
                for idx, bp in pairs(bps) do
                    result[#result + 1] = {
                        id = bp.index,
                        address = bp.address,
                        condition = bp.condition,
                        action = bp.action,
                        enabled = bp.enabled,
                        device = tag,
                    }
                end
            end
        end
        table.sort(result, function(a, b) return a.id < b.id end)
        return result
    end, {types = {device = "string"}})

    -- ── Watchpoints ─────────────────────────────────────────────────

    rpc.register_method("wp_set", function(params)
        local dev, dbg, err = get_cpu_debug(rpc, params)
        if not dev then return err end
        local space_name = params.space or "program"
        if not dev.spaces or not dev.spaces[space_name] then
            return rpc.rpc_error(rpc.INVALID_ADDR_SPACE,
                "address space not found: " .. space_name)
        end
        local space = dev.spaces[space_name]
        local cond = params.condition or ""
        local action = params.action or ""
        local wp_type = params.type or "rw"
        local idx = dev.debug:wpset(space, wp_type, params.address, params.length, cond, action)
        local tag = params.device or ":maincpu"
        wp_meta[idx] = {
            address = params.address,
            length = params.length,
            device_tag = tag,
            space_name = space_name,
            wp_type = wp_type,
            condition = cond,
            action = action,
            enabled = true,
        }
        return {id = idx}
    end, {required = {"address", "length", "type"}, types = {address = "number", length = "number", device = "string", space = "string", type = "string"}})

    rpc.register_method("wp_clear", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        local meta = wp_meta[params.id]
        if meta then
            local dev = manager.machine.devices[meta.device_tag]
            if dev and dev.debug then
                dev.debug:wpclear(params.id)
            end
            wp_meta[params.id] = nil
        else
            for tag, dev in pairs(manager.machine.devices) do
                if dev.debug then
                    for name, space in pairs(dev.spaces or {}) do
                        dev.debug:wpclear(params.id)
                    end
                end
            end
        end
        return {ok = true}
    end, {required = {"id"}, types = {id = "number"}})

    rpc.register_method("wp_enable", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        local meta = wp_meta[params.id]
        local tag = meta and meta.device_tag or params.device or ":maincpu"
        local dev = manager.machine.devices[tag]
        if dev and dev.debug then
            if params.enabled then
                dev.debug:wpenable(params.id)
            else
                dev.debug:wpdisable(params.id)
            end
            if meta then meta.enabled = params.enabled end
        end
        return {ok = true}
    end, {required = {"id", "enabled"}, types = {id = "number"}})

    rpc.register_method("wp_list", function(params)
        local dbg, err = require_debugger(rpc)
        if not dbg then return err end
        local result = {}
        for id, meta in pairs(wp_meta) do
            if not params.device or meta.device_tag == params.device then
                result[#result + 1] = {
                    id = id,
                    address = meta.address,
                    length = meta.length,
                    type = meta.wp_type,
                    condition = meta.condition or "",
                    action = meta.action or "",
                    enabled = meta.enabled,
                    device = meta.device_tag,
                    space = meta.space_name,
                }
            end
        end
        table.sort(result, function(a, b) return a.id < b.id end)
        return result
    end, {types = {device = "string"}})
end

return M
