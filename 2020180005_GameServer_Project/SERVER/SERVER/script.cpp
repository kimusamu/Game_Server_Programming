#include "script.h"

int API_get_x(lua_State* L)
{
    int user_id = (int)lua_tointeger(L, -1);

    lua_pop(L, 2);

    int x = clients[user_id]->x;

    lua_pushnumber(L, x);

    return 1;
}

int API_get_y(lua_State* L)
{
    int user_id = (int)lua_tointeger(L, -1);

    lua_pop(L, 2);

    int y = clients[user_id]->y;

    lua_pushnumber(L, y);

    return 1;
}

int API_SendMessage(lua_State* L)
{
    int my_id = (int)lua_tointeger(L, -3);
    int user_id = (int)lua_tointeger(L, -2);
    char* mess = (char*)lua_tostring(L, -1);
    lua_pop(L, 4);

    std::string utf8 = ansi_to_utf8(mess);
    clients[user_id]->send_chat_packet(my_id, utf8.c_str());

    return 0;
}

int API_StartGreet(lua_State* L)
{
    int npc_id = (int)lua_tointeger(L, -3);
    int player_id = (int)lua_tointeger(L, -2);
    int move_count = (int)lua_tointeger(L, -1);

    lua_pop(L, 4);

    auto& npc = clients[npc_id];

    {
        lock_guard<mutex> lk(npc->s_lock);
        npc->greet_mode = true;
        npc->greet_moves_left = move_count;
        npc->greet_target = player_id;
    }

    return 0;
}