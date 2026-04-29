-- license:BSD-3-Clause
-- handlers/state.lua — save/load state, screenshots, frame info

local M = {}

function M.register(rpc)

    rpc.register_method("save_state", function(params)
        local name = params.name or "mcp_save"
        manager.machine:save(name)
        return {name = name}
    end, {types = {name = "string"}})

    rpc.register_method("load_state", function(params)
        local name = params.name or "mcp_save"
        manager.machine:load(name)
        return {ok = true}
    end, {required = {"name"}, types = {name = "string"}})

    rpc.register_method("screenshot", function(params)
        -- Find the screen
        local screen_tag = params.screen
        local screen
        if screen_tag then
            screen = manager.machine.screens[screen_tag]
        else
            -- Get first screen
            for tag, s in pairs(manager.machine.screens) do
                screen = s
                break
            end
        end
        if not screen then
            return rpc.rpc_error(rpc.DEVICE_NOT_FOUND, "no screen found")
        end

        -- Use snapshot to save to a temp file, then read it back
        local tmpname = os.tmpname() .. ".png"
        local ferr = screen:snapshot(tmpname)
        if ferr then
            return rpc.rpc_error(rpc.INTERNAL_ERROR, "screenshot failed: " .. tostring(ferr))
        end

        -- Read the PNG file and base64 encode it
        local f = io.open(tmpname, "rb")
        if not f then
            return rpc.rpc_error(rpc.INTERNAL_ERROR, "failed to read screenshot file")
        end
        local data = f:read("*a")
        f:close()
        os.remove(tmpname)

        -- Base64 encode
        local b64 = {}
        local chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
        local i = 1
        while i <= #data do
            local a = data:byte(i) or 0
            local b = (i + 1 <= #data) and data:byte(i + 1) or 0
            local c = (i + 2 <= #data) and data:byte(i + 2) or 0
            local n = a * 65536 + b * 256 + c
            local remaining = #data - i + 1
            b64[#b64 + 1] = chars:sub(math.floor(n / 262144) % 64 + 1, math.floor(n / 262144) % 64 + 1)
            b64[#b64 + 1] = chars:sub(math.floor(n / 4096) % 64 + 1, math.floor(n / 4096) % 64 + 1)
            b64[#b64 + 1] = remaining >= 2 and chars:sub(math.floor(n / 64) % 64 + 1, math.floor(n / 64) % 64 + 1) or "="
            b64[#b64 + 1] = remaining >= 3 and chars:sub(n % 64 + 1, n % 64 + 1) or "="
            i = i + 3
        end

        local w = type(screen.width) == "function" and screen:width() or screen.width
        local h = type(screen.height) == "function" and screen:height() or screen.height
        return {format = "png", data = table.concat(b64), width = w, height = h}
    end, {types = {screen = "string"}})

    rpc.register_method("frame_number", function(params)
        local screen
        for tag, s in pairs(manager.machine.screens) do
            screen = s
            break
        end
        local frame = 0
        if screen then
            local fn = screen.frame_number
            if type(fn) == "function" then
                frame = fn(screen)
            else
                frame = fn or 0
            end
        end
        return {frame = frame}
    end)
end

return M
