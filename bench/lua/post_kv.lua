-- POST /api/kv with a JSON body. Keys rotate through a fixed set of 20 so the
-- store stays under its 50-entry cap: every request after warm-up is a replace
-- (200), which exercises body parsing and the store's mutex without ever
-- tripping the 507 "store full" path.
local counter = 0

request = function()
    counter = counter + 1
    local key = "bench" .. (counter % 20)
    local body = '{"key":"' .. key .. '","value":"v' .. counter .. '"}'
    return wrk.format("POST", "/api/kv", { ["Content-Type"] = "application/json" }, body)
end
