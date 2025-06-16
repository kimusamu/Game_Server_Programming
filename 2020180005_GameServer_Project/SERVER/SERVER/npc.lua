myid = 99999;

local greeted = false

math.randomseed(os.time())

function init_npc_pos(id)
    local idx   = id - MAX_USER
    local cols  = math.floor(W_WIDTH / 10)
    local baseX = (idx % cols) * 10 + 5
    local baseY = math.floor(idx / cols) * 10 + 5

    local jitter = 8
    local x = (baseX + math.random(-jitter, jitter)) % W_WIDTH
    local y = (baseY + math.random(-jitter, jitter)) % W_HEIGHT

    return x, y
end

function set_uid(x)
    myid = x;
end

function event_player_move(player)
    if greeted then
        return
    end

    local px, py = API_get_x(player), API_get_y(player)
    local mx, my = API_get_x(myid), API_get_y(myid)

    if (math.abs(px - mx) <= 1 and math.abs(py - my) <= 1) then
        API_SendMessage(myid, player, "너를 혼내주마")
        API_StartGreet(myid, player, 3)
        greeted = true
    end
end

function event_greet_end()
    greeted = false
end