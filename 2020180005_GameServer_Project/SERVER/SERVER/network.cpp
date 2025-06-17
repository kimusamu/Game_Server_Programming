#include "network.h"
#include "over_exp.h"

void disconnect(int c_id)
{
    wchar_t w_userid[11] = { 0 };
    int conv_len = MultiByteToWideChar(CP_UTF8, 0, clients[c_id]->name, -1, w_userid, _countof(w_userid));

    short last_x = clients[c_id]->x;
    short last_y = clients[c_id]->y;
    int last_hp = clients[c_id]->hp;
    int last_exp = clients[c_id]->exp;
    int last_level = clients[c_id]->level;
    int last_potion = clients[c_id]->potion;
    int last_exppotion = clients[c_id]->exp_potion;
    int last_gold = clients[c_id]->gold;

    if (!clients[c_id]->is_dummy)
    {
        DB_SavePlayerPosition(w_userid, last_x, last_y, last_hp, last_exp, last_level, last_potion, last_exppotion, last_gold);
    }

    clients[c_id]->vl.lock();
    unordered_set<int> vl = clients[c_id]->view_list;
    clients[c_id]->vl.unlock();

    for (auto& p_id : vl)
    {
        if (is_npc(p_id))
        {
            continue;
        }

        auto& pl = clients[p_id];

        {
            lock_guard<mutex> ll(pl->s_lock);

            if (pl->state.load() != ST_INGAME)
            {
                continue;
            }
        }

        if (pl->id == c_id)
        {
            continue;
        }

        pl->send_remove_player_packet(c_id);
    }

    closesocket(clients[c_id]->socket);
    lock_guard<mutex> ll(clients[c_id]->s_lock);
    clients[c_id]->state.store(ST_FREE);
}

void process_packet(int c_id, char* packet)
{
    if (clients[c_id]->hp <= 0)
    {
        return;
    }

    switch (packet[1])
    {
    case CS_LOGIN:
    {
        CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);

        for (auto& [other_id, other_sess] : clients)
        {
            if (other_id != c_id && other_sess->state.load() == ST_INGAME && strcmp(other_sess->name, p->name) == 0)
            {
                SC_LOGIN_FAIL_PACKET failPkt;
                failPkt.size = sizeof(failPkt);
                failPkt.type = SC_LOGIN_FAIL;
                clients[c_id]->do_send(&failPkt);

                disconnect(c_id);

                return;
            }
        }

        bool all_digits = true;

        for (char c : std::string(p->name))
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
            {
                all_digits = false;
                break;
            }
        }

        clients[c_id]->is_dummy = all_digits;

        wchar_t w_userid[11] = { 0 };
        int conv_len = MultiByteToWideChar(CP_UTF8, 0, p->name, -1, w_userid, _countof(w_userid));

        int saved_x = 0;
        int saved_y = 0;
        int saved_hp = BASIC_HP;
        int saved_exp = 0;
        int saved_level = 1;
        int saved_potion = 0;
        int saved_exppotion = 0;
        int saved_gold = 0;

        if (conv_len == 0)
        {
            SC_LOGIN_FAIL_PACKET fail_pkt;
            fail_pkt.size = sizeof(fail_pkt);
            fail_pkt.type = SC_LOGIN_FAIL;
            clients[c_id]->do_send(&fail_pkt);

            closesocket(clients[c_id]->socket);

            lock_guard<mutex> ll{ clients[c_id]->s_lock };
            clients[c_id]->state.store(ST_FREE);

            break;
        }

        if (!clients[c_id]->is_dummy)
        {
            bool exist = DB_LoadPlayerPosition(w_userid, saved_x, saved_y, saved_hp, saved_exp,
                saved_level, saved_potion, saved_exppotion, saved_gold);

            if (!exist)
            {
                std::uniform_int_distribution<> distX(0, W_WIDTH - 1);
                std::uniform_int_distribution<> distY(0, W_HEIGHT - 1);

                saved_x = static_cast<short>(distX(gen));
                saved_y = static_cast<short>(distY(gen));
                saved_hp = BASIC_HP;
                saved_exp = 0;
                saved_level = 1;
                saved_potion = 0;
                saved_exppotion = 0;
                saved_gold = 0;

                DB_InsertPlayerPosition(w_userid, saved_x, saved_y, saved_hp, saved_exp, saved_level, saved_potion, saved_exppotion, saved_gold);

                exist = true;
            }
        }

        else
        {
            std::uniform_int_distribution<> dist_x(0, W_WIDTH - 1);
            std::uniform_int_distribution<> dist_y(0, W_HEIGHT - 1);

            saved_x = dist_x(gen);
            saved_y = dist_y(gen);
        }


        strcpy_s(clients[c_id]->name, p->name);

        {
            lock_guard<mutex> ll{ clients[c_id]->s_lock };

            clients[c_id]->x = saved_x;
            clients[c_id]->y = saved_y;
            clients[c_id]->state.store(ST_INGAME);
            clients[c_id]->spawn_x = saved_x;
            clients[c_id]->spawn_y = saved_y;
            clients[c_id]->hp = saved_hp;
            clients[c_id]->exp = saved_exp;
            clients[c_id]->level = saved_level;
            clients[c_id]->potion = saved_potion;
            clients[c_id]->exp_potion = saved_exppotion;
            clients[c_id]->gold = saved_gold;
            clients[c_id]->is_invincible = true;
            clients[c_id]->invincible_end_time = high_resolution_clock::now() + INVINCIBLE_ON_RESPAWN;
        }

        update_sector(c_id, 0, 0, clients[c_id]->x, clients[c_id]->y);
        clients[c_id]->send_login_info_packet();

        schedule_heal_event(c_id);

        for (auto& pl_pair : clients)
        {
            int other_id = pl_pair.first;
            auto& pl = pl_pair.second;

            {
                lock_guard<mutex> ll{ pl->s_lock };

                if (pl->state.load() != ST_INGAME)
                {
                    continue;
                }
            }

            if (other_id == c_id)
            {
                continue;
            }

            if (!can_see_inline(clients[c_id].get(), pl.get()))
            {
                continue;
            }

            if (is_pc(other_id))
            {
                pl->send_add_player_packet(c_id);
            }

            else
            {
                wake_up_npc(other_id, c_id);
            }

            clients[c_id]->send_add_player_packet(other_id);
        }

        int player_sec = get_sector_index(clients[c_id]->x, clients[c_id]->y);
        auto visible_secs = get_neighbor_sectors(player_sec);

        for (int sec : visible_secs)
        {
            int sx = (sec % SECTOR_X) * SECTOR_SIZE;
            int sy = (sec / SECTOR_Y) * SECTOR_SIZE;
            int ex = min(sx + SECTOR_SIZE, W_WIDTH);
            int ey = min(sy + SECTOR_SIZE, W_HEIGHT);

            for (int oy = sy; oy < ey; ++oy)
            {
                for (int ox = sx; ox < ex; ++ox)
                {
                    if (obstacle_map[oy * W_WIDTH + ox])
                    {
                        SC_OBSTACLE_PACKET pkt;
                        pkt.size = sizeof(pkt);
                        pkt.type = SC_OBSTACLE;
                        pkt.x = static_cast<short>(ox);
                        pkt.y = static_cast<short>(oy);

                        clients[c_id]->do_send(&pkt);
                    }
                }
            }
        }

        break;
    }

    case CS_MOVE:
    {
        CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
        clients[c_id]->last_move_time = p->move_time;

        short old_x = clients[c_id]->x;
        short old_y = clients[c_id]->y;
        short x = old_x;
        short y = old_y;

        switch (p->direction)
        {

        case 0:
        {
            if (y > 0)
            {
                y--;
            }

            break;
        }

        case 1:
        {
            if (y < W_HEIGHT - 1)
            {
                y++;
            }

            break;
        }

        case 2:
        {
            if (x > 0)
            {
                x--;
            }

            break;
        }

        case 3:
        {
            if (x < W_WIDTH - 1)
            {
                x++;
            }

            break;
        }
        }

        if (!is_walkable(x, y))
        {
            x = old_x;
            y = old_y;
        }

        clients[c_id]->x = x;
        clients[c_id]->y = y;

        int old_sec = get_sector_index(old_x, old_y);
        int new_sec = get_sector_index(x, y);

        if (old_sec != new_sec) {
            auto old_neigh = get_neighbor_sectors(old_sec);
            auto new_neigh = get_neighbor_sectors(new_sec);

            for (int sec : old_neigh)
            {
                if (std::find(new_neigh.begin(), new_neigh.end(), sec) == new_neigh.end())
                {
                    clients[c_id]->send_remove_obstacle_sector(c_id, sec);
                }
            }

            for (int sec : new_neigh)
            {
                if (std::find(old_neigh.begin(), old_neigh.end(), sec) == old_neigh.end())
                {
                    int sx = (sec % SECTOR_X) * SECTOR_SIZE;
                    int sy = (sec / SECTOR_X) * SECTOR_SIZE;
                    int ex = min(sx + SECTOR_SIZE, W_WIDTH);
                    int ey = min(sy + SECTOR_SIZE, W_HEIGHT);

                    for (int oy = sy; oy < ey; ++oy)
                    {
                        for (int ox = sx; ox < ex; ++ox)
                        {
                            if (obstacle_map[oy * W_WIDTH + ox])
                            {
                                SC_OBSTACLE_PACKET pkt;
                                pkt.size = sizeof(pkt);
                                pkt.type = SC_OBSTACLE;
                                pkt.x = static_cast<short>(ox);
                                pkt.y = static_cast<short>(oy);

                                clients[c_id]->do_send(&pkt);
                            }
                        }
                    }
                }
            }
        }

        update_sector(c_id, old_x, old_y, x, y);

        auto near_list = gather_visible(c_id);

        for (int oid : near_list)
        {
            if (is_npc(oid))
            {
                auto& npc_sess = clients[oid];

                if (positions_equal(clients[c_id].get(), npc_sess.get()))
                {
                    handle_damage(clients[c_id], npc_sess);
                }

                else
                {
                    wake_up_npc(oid, c_id);
                }
            }
        }

        clients[c_id]->vl.lock();
        unordered_set<int> old_vlist = clients[c_id]->view_list;
        clients[c_id]->vl.unlock();

        clients[c_id]->send_move_packet(c_id);

        for (int oid : near_list)
        {
            auto& peer = clients[oid];
            peer->vl.lock();
            bool already = peer->view_list.count(c_id);
            peer->vl.unlock();

            if (already)
            {
                peer->send_move_packet(c_id);
            }

            else
            {
                peer->send_add_player_packet(c_id);
            }

            if (!old_vlist.count(oid))
            {
                clients[c_id]->send_add_player_packet(oid);
            }
        }

        for (int oid : old_vlist)
        {
            if (oid == c_id)
            {
                continue;
            }

            if (!near_list.count(oid))
            {
                clients[c_id]->send_remove_player_packet(oid);

                if (is_pc(oid))
                {
                    clients[oid]->send_remove_player_packet(c_id);
                }
            }
        }

        break;
    }

    case CS_CHAT:
    {
        CS_CHAT_PACKET* p = reinterpret_cast<CS_CHAT_PACKET*>(packet);
        SC_CHAT_PACKET snd_pkt;

        snd_pkt.size = sizeof(snd_pkt);
        snd_pkt.type = SC_CHAT;
        snd_pkt.id = c_id;

        memset(snd_pkt.name, 0, NAME_SIZE);
        strncpy_s(snd_pkt.name, NAME_SIZE, clients[c_id]->name, _TRUNCATE);

        for (int i = 0; i < NAME_SIZE; ++i)
        {
            unsigned char b = static_cast<unsigned char>(snd_pkt.name[i]);
        }

        std::cout << " SEND CHAT \n";

        memset(snd_pkt.mess, 0, CHAT_SIZE);
        strncpy_s(snd_pkt.mess, CHAT_SIZE, p->mess, _TRUNCATE);

        for (auto& kv : clients)
        {
            auto& sess = kv.second;

            if (sess->state.load() == ST_INGAME)
            {
                sess->do_send(&snd_pkt);
            }
        }

        break;
    }

    case CS_ATTACK:
    {
        handle_player_attack(c_id);
        break;
    }

    case CS_USE_POTION:
    {
        auto p = reinterpret_cast<CS_USE_POTION_PACKET*>(packet);
        auto& sess = clients[c_id];

        if (p->potion_type == 1 && sess->potion > 0)
        {
            int max_hp = 3 * sess->level;

            if (sess->hp < max_hp)
            {
                sess->potion--;
                sess->hp++;

                cout << sess->name << " USE POTION ! \n";
            }
        }

        else if (p->potion_type == 2 && sess->exp_potion > 0)
        {
            sess->exp_potion--;
            sess->exp++;

            cout << sess->name << " USE EXP POTION ! \n";

            int need = sess->level * 10;

            if (sess->exp >= need)
            {
                cout << sess->name << " LEVEL UP ! \n";
                sess->level++;
                sess->exp = 0;
                sess->hp = std::min(sess->hp, 3 * sess->level);
            }
        }

        sess->send_stat_change_packet();
        break;
    }

    default:
        break;
    }
}

void worker_thread(HANDLE h_iocp)
{
    while (true)
    {
        DWORD num_bytes;
        ULONG_PTR key;
        WSAOVERLAPPED* over = nullptr;
        BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
        OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);

        if (FALSE == ret)
        {
            if (ex_over->comp_type == OP_ACCEPT)
            {
                cout << " ACCEPT ERROR \n";
            }

            else
            {
                cout << " GQCS ERROR ON CLIENT [" << key << "] \n";
                disconnect(static_cast<int>(key));

                if (ex_over->comp_type == OP_SEND)
                {
                    delete ex_over;
                }

                continue;
            }
        }

        if ((0 == num_bytes) && ((ex_over->comp_type == OP_RECV) || (ex_over->comp_type == OP_SEND)))
        {
            disconnect(static_cast<int>(key));

            if (ex_over->comp_type == OP_SEND)
            {
                delete ex_over;
            }

            continue;
        }

        switch (ex_over->comp_type)
        {
        case OP_ACCEPT:
        {
            int client_id = get_new_client_id();

            if (client_id != -1)
            {
                {
                    lock_guard<mutex> ll{ clients[client_id]->s_lock };
                    clients[client_id]->state.store(ST_ALLOC);
                }

                clients[client_id]->x = 0;
                clients[client_id]->y = 0;
                clients[client_id]->id = client_id;
                clients[client_id]->name[0] = 0;
                clients[client_id]->prev_remain = 0;
                clients[client_id]->socket = g_c_socket;
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket), h_iocp, client_id, 0);
                clients[client_id]->do_recv();
                g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
            }

            else
            {
                cout << "MAX USER EXCEEDED.\n";
            }

            ZeroMemory(&g_a_over.over, sizeof(g_a_over.over));
            int addr_size = sizeof(SOCKADDR_IN);
            AcceptEx(g_s_socket, g_c_socket, g_a_over.send_buf,
                0, addr_size + 16, addr_size + 16, nullptr, &g_a_over.over);

            break;
        }

        case OP_RECV:
        {
            int remain_data = num_bytes + clients[key]->prev_remain;
            char* p = ex_over->send_buf;

            while (remain_data > 0)
            {
                int packet_size = p[0];

                if (packet_size <= remain_data)
                {
                    process_packet(static_cast<int>(key), p);
                    p += packet_size;
                    remain_data -= packet_size;
                }

                else
                {
                    break;
                }
            }

            clients[key]->prev_remain = remain_data;

            if (remain_data > 0)
            {
                memcpy(ex_over->send_buf, p, remain_data);
            }

            clients[key]->do_recv();
            break;
        }

        case OP_SEND:
        {
            delete ex_over;
            break;
        }

        case OP_NPC_MOVE:
        {
            int npc_id = static_cast<int>(key);
            SESSION* npc = session_ptrs[npc_id];

            if (!npc->is_active.load(std::memory_order_acquire) || npc->hp <= 0) 
            {
                delete ex_over;
                break;
            }

            if (npc->greet_mode.load(std::memory_order_relaxed))
            {
                int remain = npc->greet_moves_left.fetch_sub(1, std::memory_order_acq_rel);

                if (remain > 1)
                {
                    if (npc->behavior == MoveBehavior::RANDOM)
                    {
                        do_npc_random_move(npc_id);
                    }

                    else
                    {
                        do_npc_seek_move(npc_id);
                    }

                    event_type et{ npc_id, high_resolution_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
                    timer_queue.push(et);
                    delete ex_over;

                    break;
                }

                else if (remain == 1)
                {
                    lua_State* L = npc->L;

                    lua_getglobal(L, "API_SendMessage");
                    lua_pushinteger(L, npc_id);
                    lua_pushinteger(L, npc->greet_target);
                    lua_pushstring(L, "두고 보자");

                    if (lua_pcall(L, 3, 0, 0) != LUA_OK)
                    {
                        std::cerr << " LUA ERROR : " << lua_tostring(L, -1) << "\n ";
                        lua_pop(L, 1);
                    }

                    lua_getglobal(L, "event_greet_end");
                    lua_pcall(L, 0, 0, 0);
                }

                else
                {
                    npc->greet_mode.store(false, std::memory_order_relaxed);
                }
            }

            if (npc->behavior == MoveBehavior::RANDOM)
            {
                do_npc_random_move(npc_id);
            }

            else
            {
                auto vis = gather_visible_players(npc_id);

                if (!vis.empty())
                {
                    do_npc_seek_move(npc_id);
                }

                else
                {
                    do_npc_random_move(npc_id);
                }
            }

            event_type et{ npc_id, high_resolution_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
            timer_queue.push(et);

            npc->is_active.store(true);
            delete ex_over;

            break;
        }

        case OP_AI_HELLO:
        {
            int npc_id = static_cast<int>(key);
            SESSION* npc = session_ptrs[npc_id];
            int ref = npc->ref_event_player_move;

            lock_guard<mutex> lk(npc->ll);
            lua_State* L = npc->L;

            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            lua_pushinteger(L, ex_over->ai_target_obj);

            if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            {
                cerr << " LUA ERROR : " << lua_tostring(L, -1) << "\n ";
                lua_pop(L, 1);
            }

            delete ex_over;
            break;
        }
        }
    }
}