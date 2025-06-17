#include "session.h"

SESSION::SESSION()
{
    id = -1;
    socket = 0;
    x = 0;
    y = 0;
    name[0] = 0;
    state = ST_FREE;
    prev_remain = 0;
    last_move_time = 0;
    is_active = false;
    L = nullptr;

    hp = BASIC_HP;
    exp = 0;
    level = 1;
    spawn_x = 0;
    spawn_y = 0;
    is_invincible = false;
    invincible_end_time = high_resolution_clock::now();
    path.reserve(W_WIDTH + W_HEIGHT);
    behavior = MoveBehavior::RANDOM;

    heal = false;
    potion = 0;
    exp_potion = 0;
    gold = 0;
}

SESSION::~SESSION() {}

void SESSION::do_recv()
{
    DWORD recv_flag = 0;
    memset(&_recv_over.over, 0, sizeof(_recv_over.over));
    _recv_over.wsabuf.len = BUF_SIZE - prev_remain;
    _recv_over.wsabuf.buf = _recv_over.send_buf + prev_remain;
    WSARecv(socket, &_recv_over.wsabuf, 1, 0, &recv_flag, &_recv_over.over, 0);
}

void SESSION::do_send(void* packet)
{
    OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
    WSASend(socket, &sdata->wsabuf, 1, 0, 0, &sdata->over, 0);
}

void SESSION::send_login_info_packet()
{
    SC_LOGIN_INFO_PACKET p;
    p.id = id;
    p.size = sizeof(p);
    p.type = SC_LOGIN_INFO;
    p.x = x;
    p.y = y;
    p.hp = hp;
    p.exp = exp;
    p.level = level;
    p.potion = potion;
    p.exp_potion = exp_potion;
    p.gold = gold;

    do_send(&p);
}

void SESSION::send_move_packet(int c_id)
{
    SC_MOVE_OBJECT_PACKET p;
    p.id = c_id;
    p.size = sizeof(p);
    p.type = SC_MOVE_OBJECT;
    p.x = clients[c_id]->x;
    p.y = clients[c_id]->y;
    p.move_time = clients[c_id]->last_move_time;

    do_send(&p);
}

void SESSION::send_stat_change_packet()
{
    SC_STAT_CHANGEL_PACKET p;
    p.size = sizeof(p);
    p.type = SC_STAT_CHANGE;
    p.id = id;
    p.hp = hp;
    p.max_hp = 3 * level;
    p.exp = exp;
    p.level = level;
    p.potion = potion;
    p.exp_potion = exp_potion;
    p.gold = gold;

    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
    SC_ADD_OBJECT_PACKET add_packet;
    add_packet.id = c_id;
    strcpy_s(add_packet.name, clients[c_id]->name);
    add_packet.size = sizeof(add_packet);
    add_packet.type = SC_ADD_OBJECT;
    add_packet.x = clients[c_id]->x;
    add_packet.y = clients[c_id]->y;

    vl.lock();
    view_list.insert(c_id);
    vl.unlock();

    do_send(&add_packet);
}

void SESSION::send_chat_packet(int c_id, const char* mess)
{
    SC_CHAT_PACKET packet;
    packet.id = c_id;
    packet.size = sizeof(packet);
    packet.type = SC_CHAT;

    memset(packet.name, 0, NAME_SIZE);
    strncpy_s(packet.name, NAME_SIZE, clients[c_id]->name, _TRUNCATE);

    memset(packet.mess, 0, CHAT_SIZE);
    strncpy_s(packet.mess, CHAT_SIZE, mess, _TRUNCATE);

    do_send(&packet);
}

void SESSION::send_remove_player_packet(int c_id)
{
    vl.lock();

    if (view_list.count(c_id))
    {
        view_list.erase(c_id);
    }

    else
    {
        vl.unlock();
        return;
    }

    vl.unlock();

    SC_REMOVE_OBJECT_PACKET p;
    p.id = c_id;
    p.size = sizeof(p);
    p.type = SC_REMOVE_OBJECT;

    do_send(&p);
}

void SESSION::send_remove_obstacle_sector(int c_id, uint16_t sector_idx)
{
    SC_REMOVE_OBSTACLE_SECTOR_PACKET pkt;
    pkt.size = sizeof(pkt);
    pkt.type = SC_REMOVE_OBSTACLE_SECTOR;
    pkt.sector_idx = sector_idx;

    clients[c_id]->do_send(&pkt);
}

int get_new_client_id()
{
    for (int i = 0; i < MAX_USER; ++i)
    {
        lock_guard<mutex> ll{ clients[i]->s_lock };

        if (clients[i]->state.load() == ST_FREE)
        {
            return i;
        }
    }

    return -1;
}

bool is_pc(int object_id)
{
    return object_id < MAX_USER;
}

bool is_npc(int object_id)
{
    return !is_pc(object_id);
}

std::string ansi_to_utf8(const char* ansi)
{
    int wlen = MultiByteToWideChar(CP_ACP, 0, ansi, -1, NULL, 0);
    std::wstring wbuf(wlen, L'\0');

    MultiByteToWideChar(CP_ACP, 0, ansi, -1, &wbuf[0], wlen);

    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), -1,
        NULL, 0, NULL, NULL);
    std::string ubuf(ulen, '\0');

    WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), -1,
        &ubuf[0], ulen, NULL, NULL);

    return ubuf;
}