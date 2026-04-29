-- license:BSD-3-Clause
-- rpc.lua — JSON-RPC 2.0 framing, dispatch, TCP listener
-- Newline-delimited JSON-RPC over a single TCP connection.

local json = require("json")

local M = {}

-- ── JSON-RPC error codes ────────────────────────────────────────────
M.PARSE_ERROR        = -32700
M.INVALID_REQUEST    = -32600
M.METHOD_NOT_FOUND   = -32601
M.INVALID_PARAMS     = -32602
M.INTERNAL_ERROR     = -32603

-- Application-defined (-32000 to -32099)
M.DEBUGGER_NOT_ENABLED = -32000
M.DEVICE_NOT_FOUND     = -32001
M.INVALID_ADDR_SPACE   = -32002
M.BP_NOT_FOUND         = -32003
M.SYSTEM_NOT_RUNNING   = -32004
M.INVALID_ADDRESS      = -32005
M.TAP_NOT_FOUND        = -32006

-- ── State ───────────────────────────────────────────────────────────
local handlers = {}
local tick_hooks = {}       -- functions called each tick before I/O
local outbox = {}           -- pending notifications to flush
local sock = nil
local buf = ""
local connected = false
local config = {}
local cached_debugger = nil -- set by init.lua via machine reset notifier

-- ── Response builders ───────────────────────────────────────────────

local function success_response(id, result)
    return json.encode({jsonrpc = "2.0", id = id, result = result})
end

local function error_response(id, code, message, data)
    local err = {code = code, message = message}
    if data ~= nil then err.data = data end
    return json.encode({jsonrpc = "2.0", id = id, error = err})
end

local function notification_message(method, params)
    return json.encode({jsonrpc = "2.0", method = method, params = params})
end

-- ── Schema validation (minimal Phase 1) ─────────────────────────────

local function validate(params, schema)
    if not schema then return true end
    if schema.required then
        for _, field in ipairs(schema.required) do
            if params[field] == nil then
                return false, "missing required field: " .. field
            end
        end
    end
    if schema.types then
        for field, expected_type in pairs(schema.types) do
            if params[field] ~= nil then
                local actual = type(params[field])
                if actual ~= expected_type then
                    return false, "field '" .. field .. "': expected " .. expected_type .. ", got " .. actual
                end
            end
        end
    end
    return true
end

-- ── Dispatch ────────────────────────────────────────────────────────

local function dispatch(req)
    -- Validate JSON-RPC structure
    if type(req) ~= "table" or req.jsonrpc ~= "2.0" then
        return error_response(req and req.id, M.INVALID_REQUEST, "invalid JSON-RPC 2.0 request")
    end
    if type(req.method) ~= "string" then
        return error_response(req.id, M.INVALID_REQUEST, "method must be a string")
    end

    local h = handlers[req.method]
    if not h then
        return error_response(req.id, M.METHOD_NOT_FOUND, "method not found: " .. req.method)
    end

    local params = req.params or {}
    if type(params) ~= "table" then
        return error_response(req.id, M.INVALID_PARAMS, "params must be object or array")
    end

    -- Schema validation
    local ok, validation_err = validate(params, h.schema)
    if not ok then
        return error_response(req.id, M.INVALID_PARAMS, validation_err)
    end

    -- Call handler
    local ok2, result_or_err = pcall(h.fn, params)
    if not ok2 then
        return error_response(req.id, M.INTERNAL_ERROR, tostring(result_or_err))
    end

    -- Handler may return an explicit error table
    if type(result_or_err) == "table" and result_or_err._rpc_error then
        return error_response(req.id, result_or_err.code, result_or_err.message, result_or_err.data)
    end

    return success_response(req.id, result_or_err)
end

-- ── Public API ──────────────────────────────────────────────────────

function M.register_method(name, fn, schema)
    handlers[name] = {fn = fn, schema = schema}
end

function M.notify(method, params)
    outbox[#outbox + 1] = notification_message(method, params)
end

function M.rpc_error(code, message, data)
    return {_rpc_error = true, code = code, message = message, data = data}
end

function M.register_tick_hook(fn)
    tick_hooks[#tick_hooks + 1] = fn
end

function M.set_debugger(dbg)
    cached_debugger = dbg
end

function M.get_debugger()
    return cached_debugger
end

function M.list_methods()
    local names = {}
    for name, _ in pairs(handlers) do
        names[#names + 1] = name
    end
    table.sort(names)
    return names
end

function M.init(cfg)
    config = cfg or {}
    local host = config.host or "127.0.0.1"
    local port = config.port or 8080

    sock = emu.file("", 7)  -- OPEN_FLAG_CREATE = listen mode
    sock:open("socket." .. host .. ":" .. port)
    print("[mcp] listening on " .. host .. ":" .. port)
    connected = false
    buf = ""
    outbox = {}
end

function M.tick()
    if not sock then return end

    -- Run tick hooks (event detection etc.)
    for _, hook in ipairs(tick_hooks) do
        local ok, err = pcall(hook)
        if not ok then
            print("[mcp] tick hook error: " .. tostring(err))
        end
    end

    -- Read incoming data (non-blocking)
    repeat
        local data = sock:read(4096)
        if #data > 0 then
            connected = true
            buf = buf .. data
        end
    until #data == 0

    -- Process complete newline-delimited messages
    while true do
        local nl = buf:find("\n")
        if not nl then break end
        local line = buf:sub(1, nl - 1)
        buf = buf:sub(nl + 1)
        line = line:gsub("^%s+", ""):gsub("%s+$", "")
        if #line > 0 then
            local ok_parse, req = pcall(json.decode, line)
            local response
            if not ok_parse or type(req) ~= "table" then
                response = error_response(nil, M.PARSE_ERROR, "parse error")
            else
                response = dispatch(req)
            end
            sock:write(response .. "\n")
        end
    end

    -- Flush outbox notifications
    if connected then
        for _, msg in ipairs(outbox) do
            sock:write(msg .. "\n")
        end
        outbox = {}
    end
end

function M.shutdown()
    if sock then
        print("[mcp] shutting down")
        sock:close()
        sock = nil
    end
end

return M
