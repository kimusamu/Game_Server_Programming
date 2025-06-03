myid = 99999;

function set_uid(x)
    myid = x;
end

function event_player_move(player)
    local px, py = API_get_x(player), API_get_y(player)
    local mx, my = API_get_x(myid), API_get_y(myid)
    if (math.abs(px - mx) <= 1 and math.abs(py - my) <= 1) then
        API_SendMessage(myid, player, "HELLO")
        API_StartGreet(myid, player, 3)
    end
end