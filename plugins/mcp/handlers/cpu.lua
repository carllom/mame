-- license:BSD-3-Clause
-- handlers/cpu.lua — CPU register access and disassembly

local M = {}

local function get_cpu(rpc, params)
    local tag = params.device or ":maincpu"
    local dev = manager.machine.devices[tag]
    if not dev then
        return nil, rpc.rpc_error(rpc.DEVICE_NOT_FOUND, "device not found: " .. tag)
    end
    if not dev.state then
        return nil, rpc.rpc_error(rpc.DEVICE_NOT_FOUND, "device has no register state: " .. tag)
    end
    return dev, nil
end

function M.register(rpc)

    rpc.register_method("read_reg", function(params)
        local cpu, err = get_cpu(rpc, params)
        if not cpu then return err end
        local entry = cpu.state[params.name]
        if not entry then
            return rpc.rpc_error(rpc.INVALID_PARAMS, "register not found: " .. tostring(params.name))
        end
        return {name = entry.symbol, value = entry.value}
    end, {required = {"name"}, types = {name = "string", device = "string"}})

    rpc.register_method("write_reg", function(params)
        local cpu, err = get_cpu(rpc, params)
        if not cpu then return err end
        local entry = cpu.state[params.name]
        if not entry then
            return rpc.rpc_error(rpc.INVALID_PARAMS, "register not found: " .. tostring(params.name))
        end
        if not entry.writeable then
            return rpc.rpc_error(rpc.INVALID_PARAMS, "register is read-only: " .. params.name)
        end
        entry.value = params.value
        return {ok = true}
    end, {required = {"name", "value"}, types = {name = "string", value = "number", device = "string"}})

    rpc.register_method("list_regs", function(params)
        local cpu, err = get_cpu(rpc, params)
        if not cpu then return err end
        local result = {}
        for symbol, entry in pairs(cpu.state) do
            if type(symbol) == "string" and entry.visible then
                result[#result + 1] = {
                    name = entry.symbol,
                    value = entry.value,
                    size_bits = entry.datasize * 8,
                }
            end
        end
        table.sort(result, function(a, b) return a.name < b.name end)
        return result
    end, {types = {device = "string"}})

    -- Disassemble uses the dasm debugger command to write to a temp file
    rpc.register_method("disassemble", function(params)
        local debugger = rpc.get_debugger()
        if not debugger then
            return rpc.rpc_error(rpc.DEBUGGER_NOT_ENABLED, "debugger not enabled")
        end
        local tag = params.device or ":maincpu"
        local dev = manager.machine.devices[tag]
        if not dev then
            return rpc.rpc_error(rpc.DEVICE_NOT_FOUND, "device not found: " .. tag)
        end
        local addr = params.address
        local count = params.count or 10

        -- dasm <filename>,<address>,<length>[,<opcodes>[,<CPU>]]
        -- length is in address units, not instruction count.
        -- We use a generous length and trim to count instructions.
        local tmpfile = os.tmpname()
        local length = count * 16  -- generous: max 16 bytes per instruction
        local cmd = string.format("dasm %s,%x,%x,1,%s", tmpfile, addr, length, tag)
        debugger:command(cmd)

        -- Read the file back
        local f = io.open(tmpfile, "r")
        if not f then
            return rpc.rpc_error(rpc.INTERNAL_ERROR, "failed to read disassembly file")
        end
        local result = {}
        for line in f:lines() do
            -- Typical format: "  00180012: 203C 0018 0000           move.l  #$180000, D0"
            local a, rest = line:match("^%s*(%x+):%s*(.*)")
            if a and #result < count then
                result[#result + 1] = {
                    address = tonumber(a, 16),
                    text = rest,
                }
            end
        end
        f:close()
        os.remove(tmpfile)

        return result
    end, {required = {"address"}, types = {address = "number", count = "number", device = "string"}})
end

return M
