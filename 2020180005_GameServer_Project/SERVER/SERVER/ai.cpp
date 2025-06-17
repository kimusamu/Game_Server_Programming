#include "ai.h"
#include "timer.h"
#include "network.h"

void NodePool::reset()
{
	idx = 0;
}

Node* NodePool::alloc(short x, short y, int g, int f, Node* parent)
{
    Node* n = &pool[idx++];

    n->x = x;
    n->y = y;
    n->g = g;
    n->f = f;
    n->parent = parent;

    return n;
}

bool astar(int sx, int sy, int tx, int ty, vector<pair<short, short>>& out_path)
{
    out_path.clear();
    static thread_local NodePool node_pool(W_WIDTH * W_HEIGHT);
    node_pool.reset();

    static thread_local std::vector<uint32_t> visit_ver;
    static thread_local uint32_t epoch = 1;
    const size_t grid_size = size_t(W_WIDTH) * W_HEIGHT;

    if (visit_ver.size() != grid_size)
    {
        visit_ver.assign(grid_size, 0);
    }

    if (++epoch == 0)
    {
        std::fill(visit_ver.begin(), visit_ver.end(), 0);
        epoch = 1;
    }

    auto cmp = [](Node* a, Node* b)
    {
        return a->f > b->f;
    };

    std::priority_queue<Node*, vector<Node*>, decltype(cmp)> open(cmp);

    auto h = [&](int x, int y)
    {
        return abs(x - tx) + abs(y - ty);
    };

    Node* start = node_pool.alloc(sx, sy, 0, h(sx, sy), nullptr);
    open.push(start);
    visit_ver[sy * W_WIDTH + sx] = epoch;

    static constexpr array<pair<int, int>, 4> dirs{ {{1,0},{-1,0},{0,1},{0,-1}} };

    while (!open.empty())
    {
        Node* cur = open.top(); open.pop();

        if (cur->x == tx && cur->y == ty)
        {
            for (Node* p = cur; p; p = p->parent)
            {
                out_path.emplace_back(p->x, p->y);
            }

            std::reverse(out_path.begin(), out_path.end());

            return true;
        }

        for (auto [dx, dy] : dirs)
        {
            int nx = cur->x + dx, ny = cur->y + dy;

            if (nx < 0 || nx >= W_WIDTH || ny < 0 || ny >= W_HEIGHT)
            {
                continue;
            }

            size_t idx = ny * W_WIDTH + nx;

            if (visit_ver[idx] == epoch || !is_walkable(nx, ny))
            {
                continue;
            }

            visit_ver[idx] = epoch;

            int ng = cur->g + 1;
            Node* nxt = node_pool.alloc(nx, ny, ng, ng + h(nx, ny), cur);
            open.push(nxt);
        }
    }

    return false;
}

void wake_up_npc(int npc_id, int waker)
{
    auto& npc = clients[npc_id];

    if (npc->greet_mode.load(std::memory_order_relaxed))
    {
        return;
    }

    OVER_EXP* exp_over = new OVER_EXP;
    exp_over->comp_type = OP_AI_HELLO;
    exp_over->ai_target_obj = waker;
    PostQueuedCompletionStatus(h_iocp, 1, npc_id, &exp_over->over);

    if (clients[npc_id]->is_active.load())
    {
        return;
    }

    bool old_state = false;

    if (!atomic_compare_exchange_strong(&clients[npc_id]->is_active, &old_state, true))
    {
        return;
    }

    event_type et{ npc_id, chrono::high_resolution_clock::now(), EV_RANDOM_MOVE, 0 };
    timer_queue.push(et);
}

void do_npc_random_move(int npc_id)
{
    SESSION* npc = session_ptrs[npc_id];
    int sec_idx = get_sector_index(npc->x, npc->y);
    int sx = (sec_idx % SECTOR_X) * SECTOR_SIZE;
    int sy = (sec_idx / SECTOR_X) * SECTOR_SIZE;
    int ex = min(sx + SECTOR_SIZE - 1, W_WIDTH - 1);
    int ey = min(sy + SECTOR_SIZE - 1, W_HEIGHT - 1);
    int old_x = npc->x;
    int old_y = npc->y;
    int x = old_x;
    int y = old_y;

    switch (rand() % 4) {
    case 0:
    {
        if (x < ex)
        {
            x++;
        }

        break;
    }

    case 1:
    {
        if (x > sx)
        {
            x--;
        }

        break;
    }

    case 2:
    {
        if (y < ey)
        {
            y++;
        }

        break;
    }

    case 3:
    {
        if (y > sy)
        {
            y--;
        }

        break;
    }
    }

    if (!is_walkable(x, y))
    {
        x = old_x;
        y = old_y;
    }

    npc->x = x;
    npc->y = y;

    update_sector(npc_id, old_x, old_y, x, y);

    {
        auto new_vl = gather_visible(npc_id);

        for (int pid : new_vl)
        {
            if (is_pc(pid))
            {
                auto& player_sess = clients[pid];

                if (positions_equal(npc, player_sess.get()))
                {
                    handle_damage(clients[npc_id], player_sess);
                }
            }
        }
    }

    unordered_set<int> old_vl;

    {
        lock_guard<mutex> lk(npc->vl);
        old_vl = npc->view_list;
    }

    auto new_vl = gather_visible(npc_id);

    {
        lock_guard<mutex> lk(npc->vl);
        npc->view_list = new_vl;
    }

    for (int pid : new_vl)
    {
        auto& peer = clients[pid];

        if (!old_vl.count(pid))
        {
            peer->send_add_player_packet(npc_id);
        }

        else
        {
            peer->send_move_packet(npc_id);
        }
    }

    for (int pid : old_vl)
    {
        if (!new_vl.count(pid))
        {
            clients[pid]->send_remove_player_packet(npc_id);
        }
    }

    short mx = npc->x;
    short my = npc->y;

    for (int pid : gather_visible_players(npc_id))
    {
        auto& player = clients[pid];

        if (player->is_dummy)
        {
            continue;
        }

        int dx = abs(player->x - mx);
        int dy = abs(player->y - my);

        if ((dx <= 1 && dy <= 1) && !(dx == 0 && dy == 0))
        {
            handle_monster_attack(clients[npc_id], player);
        }
    }

    long long current_time = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    clients[npc_id]->last_move_time = current_time;
}

void do_npc_seek_move(int npc_id)
{
    SESSION* monster = session_ptrs[npc_id];
    auto vis = gather_visible_players(npc_id);

    if (vis.empty())
    {
        do_npc_random_move(npc_id);

        return;
    }

    int target_id = vis[0];

    auto& path = monster->path;
    size_t path_len = path.size();
    bool need_repath = (path_len == 0) || (monster->path_idx + 1 >= path_len) ||
        (clients[target_id]->x != path[path_len - 1].first) || (clients[target_id]->y != path[path_len - 1].second);

    if (need_repath)
    {
        monster->path_idx = 0;
        path.clear();
        astar(monster->x, monster->y, clients[target_id]->x, clients[target_id]->y, monster->path);
        path_len = path.size();
    }

    if (monster->path_idx + 1 < path_len)
    {
        auto [nx, ny] = path[++monster->path_idx];
        int old_x = monster->x, old_y = monster->y;
        monster->x = nx;
        monster->y = ny;

        update_sector(npc_id, old_x, old_y, nx, ny);
    }

    unordered_set<int> old_vl;

    {
        lock_guard<mutex> lk(monster->vl);
        old_vl = monster->view_list;
    }

    auto new_vl = gather_visible(npc_id);

    {
        lock_guard<mutex> lk(monster->vl);
        monster->view_list = new_vl;
    }

    for (int pid : new_vl)
    {
        auto& peer = clients[pid];

        if (!old_vl.count(pid))
        {
            peer->send_add_player_packet(npc_id);
        }

        else
        {
            peer->send_move_packet(npc_id);
        }
    }

    for (int pid : old_vl)
    {
        if (!new_vl.count(pid))
        {
            clients[pid]->send_remove_player_packet(npc_id);
        }
    }

    short mx = monster->x;
    short my = monster->y;

    for (int pid : gather_visible_players(npc_id))
    {
        auto& player = clients[pid];

        if (player->is_dummy)
        {
            continue;
        }

        int dx = abs(player->x - mx);
        int dy = abs(player->y - my);

        if ((dx <= 1 && dy <= 1) && !(dx == 0 && dy == 0))
        {
            handle_monster_attack(clients[npc_id], player);
        }
    }

    clients[npc_id]->last_move_time = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void initialize_npc()
{
    cout << " MONSTER SPAWN START ! \n";

    for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
    {
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);

        lua_register(L, "API_SendMessage", API_SendMessage);
        lua_register(L, "API_get_x", API_get_x);
        lua_register(L, "API_get_y", API_get_y);
        lua_register(L, "API_StartGreet", API_StartGreet);

        lua_pushinteger(L, MAX_USER);
        lua_setglobal(L, "MAX_USER");

        lua_pushinteger(L, MAX_NPC);
        lua_setglobal(L, "MAX_NPC");

        lua_pushinteger(L, W_WIDTH);
        lua_setglobal(L, "W_WIDTH");

        lua_pushinteger(L, W_HEIGHT);
        lua_setglobal(L, "W_HEIGHT");

        if (luaL_dofile(L, "npc.lua") != LUA_OK)
        {
            std::cerr << " NPC LUA LOAD FAILED : " << lua_tostring(L, -1) << "\n";
            lua_pop(L, 1);
        }

        lua_getglobal(L, "event_player_move");
        clients[i]->ref_event_player_move = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_rawgeti(L, LUA_REGISTRYINDEX, clients[i]->ref_event_player_move);
        lua_getglobal(L, "set_uid");
        lua_pushinteger(L, i);

        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            std::cerr << " LUA ERROR : SET_UID : " << lua_tostring(L, -1) << "\n";
            lua_pop(L, 1);
        }

        short spawn_x, spawn_y;
        lua_getglobal(L, "init_npc_pos");
        lua_pushinteger(L, i);

        if (lua_pcall(L, 1, 2, 0) == LUA_OK)
        {
            spawn_x = (short)lua_tointeger(L, -2);
            spawn_y = (short)lua_tointeger(L, -1);
            lua_pop(L, 2);
        }

        else
        {
            cout << " SPAWN SCRIPT ERROR : SPAWN TO RANDOM \n";
            lua_pop(L, 1);
            spawn_x = rand() % W_WIDTH;
            spawn_y = rand() % W_HEIGHT;
        }

        static std::uniform_int_distribution<> jitter_dist(-8, +8);

        spawn_x = static_cast<short>((spawn_x + jitter_dist(gen) + W_WIDTH) % W_WIDTH);
        spawn_y = static_cast<short>((spawn_y + jitter_dist(gen) + W_HEIGHT) % W_HEIGHT);

        clients[i]->L = L;
        clients[i]->id = i;
        clients[i]->x = spawn_x;
        clients[i]->y = spawn_y;

        if (i % 10000 == 0)
        {
            int ret = sprintf_s(clients[i]->name, NAME_SIZE, "MONSTER%d", i);

            for (int k = 0; k < NAME_SIZE; ++k)
            {
                unsigned char b = static_cast<unsigned char>(clients[i]->name[k]);
            }

            std::cout << " MONSTER SPAWN : ID = " << i << ", string=\"" << clients[i]->name << "\"" << " (ret=" << ret << ") \n";
        }

        char buf[NAME_SIZE];

        if (i % 2 == 0)
        {
            sprintf_s(buf, NAME_SIZE, "FOLLOW_%d", i);
        }

        else
        {
            sprintf_s(buf, NAME_SIZE, "RANDOM_%d", i);
        }

        strcpy_s(clients[i]->name, buf);

        std::string nm(buf);

        if (nm.rfind("FOLLOW_", 0) == 0)
        {
            clients[i]->behavior = MoveBehavior::SEEK;
        }

        else
        {
            clients[i]->behavior = MoveBehavior::RANDOM;
        }

        clients[i]->state.store(ST_INGAME);
        clients[i]->spawn_x = clients[i]->x;
        clients[i]->spawn_y = clients[i]->y;
        clients[i]->hp = BASIC_HP;
        clients[i]->is_invincible = true;
        clients[i]->invincible_end_time = high_resolution_clock::now() + INVINCIBLE_ON_RESPAWN;

        int idx = get_sector_index(clients[i]->x, clients[i]->y);

        {
            lock_guard<mutex> lk(sector_mutexes[idx]);
            sectors[idx].insert(session_ptrs[i]);
        }
    }

    cout << " MONSTER SPAWN END ! \n";
}