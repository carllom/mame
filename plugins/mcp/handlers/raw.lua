-- license:BSD-3-Clause
-- handlers/raw.lua — escape hatch for arbitrary debugger console commands
-- Use named handlers when possible; this exists for completeness.

local M = {}

function M.register(rpc)

    rpc.register_method("exec_command", function(params)
        local debugger = rpc.get_debugger()
        if not debugger then
            return rpc.rpc_error(rpc.DEBUGGER_NOT_ENABLED, "debugger not enabled")
        end
        local log = debugger.consolelog
        local before = #log
        debugger:command(params.command)
        local after = #log
        local lines = {}
        for i = before + 1, after do
            lines[#lines + 1] = log[i]
        end
        return {output = table.concat(lines, "\n")}
    end, {required = {"command"}, types = {command = "string"}})
end

return M
