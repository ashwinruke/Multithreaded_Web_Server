-- A rough approximation of real traffic against this server: mostly page and
-- asset loads, a steady share of API reads, and occasional writes.
--
--   60%  GET  /demo/index.html   static, cached
--   20%  GET  /api/stats         dynamic JSON, built per request
--   15%  GET  /api/kv            read of shared state
--    5%  POST /api/kv            write to shared state (mutex)
local counter = 0

init = function(args)
    -- Each wrk thread has its own Lua state; seed them differently.
    math.randomseed(os.time() + math.floor(os.clock() * 1000000))
end

request = function()
    counter = counter + 1
    local roll = math.random(100)
    if roll <= 60 then
        return wrk.format("GET", "/demo/index.html")
    elseif roll <= 80 then
        return wrk.format("GET", "/api/stats")
    elseif roll <= 95 then
        return wrk.format("GET", "/api/kv")
    else
        local body = '{"key":"mix' .. (counter % 20) .. '","value":"v' .. counter .. '"}'
        return wrk.format("POST", "/api/kv", { ["Content-Type"] = "application/json" }, body)
    end
end
