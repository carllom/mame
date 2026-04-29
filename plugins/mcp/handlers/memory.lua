-- license:BSD-3-Clause
-- handlers/memory.lua — memory read/write/search/tap (works without -debug)

local M = {}

-- Tap state: ring buffers keyed by tap_id
local taps = {}
local next_tap_id = 1
local DEFAULT_TAP_CAPACITY = 10000

local function get_space(rpc, params)
    local tag = params.device or ":maincpu"
    local dev = manager.machine.devices[tag]
    if not dev then
        return nil, rpc.rpc_error(rpc.DEVICE_NOT_FOUND, "device not found: " .. tag)
    end
    if not dev.spaces then
        return nil, rpc.rpc_error(rpc.INVALID_ADDR_SPACE, "device has no address spaces: " .. tag)
    end
    local space_name = params.space or "program"
    local space = dev.spaces[space_name]
    if not space then
        return nil, rpc.rpc_error(rpc.INVALID_ADDR_SPACE,
            "address space not found: " .. space_name .. " on " .. tag)
    end
    return space, nil
end

function M.register(rpc)

    rpc.register_method("read_mem", function(params)
        local space, err = get_space(rpc, params)
        if not space then return err end
        local addr = params.address
        local len = params.length or 1
        local unit = params.unit_size or 1
        local bytes = {}
        if unit == 1 then
            for i = 0, len - 1 do
                bytes[#bytes + 1] = space:read_u8(addr + i)
            end
        elseif unit == 2 then
            for i = 0, len - 1 do
                bytes[#bytes + 1] = space:read_u16(addr + i * 2)
            end
        elseif unit == 4 then
            for i = 0, len - 1 do
                bytes[#bytes + 1] = space:read_u32(addr + i * 4)
            end
        elseif unit == 8 then
            for i = 0, len - 1 do
                bytes[#bytes + 1] = space:read_u64(addr + i * 8)
            end
        else
            return rpc.rpc_error(rpc.INVALID_PARAMS, "unit_size must be 1, 2, 4, or 8")
        end
        return {bytes = bytes}
    end, {required = {"address"}, types = {address = "number", length = "number", device = "string", space = "string"}})

    rpc.register_method("write_mem", function(params)
        local space, err = get_space(rpc, params)
        if not space then return err end
        local addr = params.address
        local bytes = params.bytes
        for i, b in ipairs(bytes) do
            space:write_u8(addr + i - 1, b)
        end
        return {written = #bytes}
    end, {required = {"address", "bytes"}, types = {address = "number", device = "string", space = "string"}})

    rpc.register_method("search_mem", function(params)
        local space, err = get_space(rpc, params)
        if not space then return err end
        local addr = params.address
        local len = params.length
        local pattern = params.pattern  -- array of byte values
        local matches = {}
        local plen = #pattern
        for i = 0, len - plen do
            local found = true
            for j = 1, plen do
                if space:read_u8(addr + i + j - 1) ~= pattern[j] then
                    found = false
                    break
                end
            end
            if found then
                matches[#matches + 1] = addr + i
            end
        end
        return {matches = matches}
    end, {required = {"address", "length", "pattern"}, types = {address = "number", length = "number"}})

    rpc.register_method("install_read_tap", function(params)
        local space, err = get_space(rpc, params)
        if not space then return err end
        local tap_addr = params.address
        local tap_len = params.length
        local capacity = params.capacity or DEFAULT_TAP_CAPACITY
        local tid = next_tap_id
        next_tap_id = next_tap_id + 1

        local tap_state = {
            id = tid,
            entries = {},
            capacity = capacity,
            drops = 0,
            kind = "read",
            handle = nil,
        }

        local name = "mcp_rtap_" .. tid
        local handle = space:install_read_tap(tap_addr, tap_addr + tap_len - 1, name,
            function(offset, value, mask)
                local entries = tap_state.entries
                if #entries >= tap_state.capacity then
                    table.remove(entries, 1)
                    tap_state.drops = tap_state.drops + 1
                end
                entries[#entries + 1] = {
                    addr = tap_addr + offset,
                    value = value,
                    mask = mask,
                }
            end)
        tap_state.handle = handle
        taps[tid] = tap_state
        return {tap_id = tid}
    end, {required = {"address", "length"}, types = {address = "number", length = "number", device = "string", space = "string"}})

    rpc.register_method("install_write_tap", function(params)
        local space, err = get_space(rpc, params)
        if not space then return err end
        local tap_addr = params.address
        local tap_len = params.length
        local capacity = params.capacity or DEFAULT_TAP_CAPACITY
        local tid = next_tap_id
        next_tap_id = next_tap_id + 1

        local tap_state = {
            id = tid,
            entries = {},
            capacity = capacity,
            drops = 0,
            kind = "write",
            handle = nil,
        }

        local name = "mcp_wtap_" .. tid
        local handle = space:install_write_tap(tap_addr, tap_addr + tap_len - 1, name,
            function(offset, value, mask)
                local entries = tap_state.entries
                if #entries >= tap_state.capacity then
                    table.remove(entries, 1)
                    tap_state.drops = tap_state.drops + 1
                end
                entries[#entries + 1] = {
                    addr = tap_addr + offset,
                    value = value,
                    mask = mask,
                }
            end)
        tap_state.handle = handle
        taps[tid] = tap_state
        return {tap_id = tid}
    end, {required = {"address", "length"}, types = {address = "number", length = "number", device = "string", space = "string"}})

    rpc.register_method("read_tap_buffer", function(params)
        local tid = params.tap_id
        local ts = taps[tid]
        if not ts then
            return rpc.rpc_error(rpc.TAP_NOT_FOUND, "tap not found: " .. tostring(tid))
        end
        local max_entries = params.max_entries or #ts.entries
        local drain = params.drain
        if drain == nil then drain = true end

        -- Return up to max_entries from the buffer
        local result = {}
        local count = math.min(max_entries, #ts.entries)
        for i = 1, count do
            result[#result + 1] = ts.entries[i]
        end

        if drain then
            -- Remove returned entries
            local remaining = {}
            for i = count + 1, #ts.entries do
                remaining[#remaining + 1] = ts.entries[i]
            end
            ts.entries = remaining
        end

        return {entries = result, drops = ts.drops, remaining = #ts.entries}
    end, {required = {"tap_id"}, types = {tap_id = "number"}})

    rpc.register_method("remove_tap", function(params)
        local tid = params.tap_id
        local ts = taps[tid]
        if not ts then
            return rpc.rpc_error(rpc.TAP_NOT_FOUND, "tap not found: " .. tostring(tid))
        end
        if ts.handle then
            ts.handle:remove()
        end
        taps[tid] = nil
        return {ok = true}
    end, {required = {"tap_id"}, types = {tap_id = "number"}})
end

return M
