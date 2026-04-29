-- license:BSD-3-Clause
-- handlers/machine.lua — machine-level introspection (works without -debug)

local M = {}

function M.register(rpc)

    rpc.register_method("ping", function(params)
        return {
            pong = true,
            mame_version = emu.app_name() .. " " .. emu.app_version(),
            api_version = 1,
        }
    end)

    rpc.register_method("list_devices", function(params)
        local result = {}
        for tag, dev in pairs(manager.machine.devices) do
            result[#result + 1] = {
                tag = tag,
                name = dev.name,
                shortname = dev.shortname,
            }
        end
        table.sort(result, function(a, b) return a.tag < b.tag end)
        return result
    end)

    rpc.register_method("list_spaces", function(params)
        local tag = params.device or ":maincpu"
        local dev = manager.machine.devices[tag]
        if not dev then
            return rpc.rpc_error(rpc.DEVICE_NOT_FOUND, "device not found: " .. tag)
        end
        if not dev.spaces then
            return rpc.rpc_error(rpc.INVALID_ADDR_SPACE, "device has no address spaces: " .. tag)
        end
        local result = {}
        for name, space in pairs(dev.spaces) do
            result[#result + 1] = {
                name = name,
                address_width = space.address_width,
                data_width = space.data_width,
                shift = space.shift,
            }
        end
        table.sort(result, function(a, b) return a.name < b.name end)
        return result
    end, {required = {}, types = {device = "string"}})

    rpc.register_method("driver_info", function(params)
        local driver = manager.machine.system
        return {
            shortname = driver.name,
            description = driver.description,
            manufacturer = driver.manufacturer,
            year = driver.year,
            parent = driver.parent,
            is_bios = driver.is_bios_root,
        }
    end)

    rpc.register_method("list_handlers", function(params)
        return rpc.list_methods()
    end)
end

return M
