-- license:BSD-3-Clause
-- init.lua — MCP bridge plugin entry point

local exports = {
    name = "mcp",
    version = "1.0.0",
    description = "MCP bridge — JSON-RPC 2.0 debug/introspection server",
    license = "BSD-3-Clause",
    author = {name = "Carl Lom"}
}

function exports.startplugin()
    local rpc = require("mcp/rpc")
    rpc.init({
        host = "127.0.0.1",
        port = tonumber(os.getenv("MAME_MCP_PORT")) or 8080,
    })

    -- Register all handler modules
    for _, mod in ipairs({"machine", "memory", "cpu", "debugger", "state", "raw"}) do
        require("mcp/handlers/" .. mod).register(rpc)
    end

    -- Cache debugger reference on machine reset (safe point)
    local reset_sub = emu.add_machine_reset_notifier(function()
        local dbg = manager.machine.debugger  -- safe to access here
        rpc.set_debugger(dbg)
    end)

    local stop_sub = emu.add_machine_stop_notifier(function()
        rpc.set_debugger(nil)
    end)

    emu.register_periodic(rpc.tick)
end

return exports
