#define NOMINMAX

#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <concurrent_unordered_map.h>
#include <memory>
#include <algorithm>
#include "protocol.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "Mswsock.lib")

using namespace std;

constexpr int VIEW_RANGE = 5;
constexpr int SECTOR_SIZE = 16;
constexpr int SECTOR_X = (W_WIDTH + SECTOR_SIZE - 1) / SECTOR_SIZE;
constexpr int SECTOR_Y = (W_HEIGHT + SECTOR_SIZE - 1) / SECTOR_SIZE;

std::vector<std::unordered_set<int>> sectors(SECTOR_X * SECTOR_Y);
std::vector<std::mutex> sector_mutexes(SECTOR_X * SECTOR_Y);

inline int get_sector_index(int x, int y) 
{
    int sx = std::clamp(x / SECTOR_SIZE, 0, SECTOR_X - 1);
    int sy = std::clamp(y / SECTOR_SIZE, 0, SECTOR_Y - 1);

    return sy * SECTOR_X + sx;
}

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND };

class OVER_EXP
{
public:
    WSAOVERLAPPED _over;
    WSABUF _wsabuf;
    char _send_buf[BUF_SIZE];
    COMP_TYPE _comp_type;

    OVER_EXP()
    {
        _wsabuf.len = BUF_SIZE;
        _wsabuf.buf = _send_buf;
        _comp_type = OP_RECV;
        ZeroMemory(&_over, sizeof(_over));
    }

    OVER_EXP(char* packet)
    {
        _wsabuf.len = packet[0];
        _wsabuf.buf = _send_buf;
        _comp_type = OP_SEND;
        ZeroMemory(&_over, sizeof(_over));
        memcpy(_send_buf, packet, packet[0]);
    }
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

class SESSION 
{
    OVER_EXP _recv_over;

public:
    mutex _s_lock;
    S_STATE _state;
    int _id;
    SOCKET _socket;
    short x, y;
    char _name[NAME_SIZE];
    int _prev_remain;
    unordered_set<int> _view_list;
    mutex _vl;
    int last_move_time;

    SESSION()
    {
        _id = -1;
        _socket = 0;
        x = y = 0;
        _name[0] = 0;
        _state = ST_FREE;
        _prev_remain = 0;
        last_move_time = 0;
    }

    ~SESSION() {}

    void do_recv()
    {
        DWORD recv_flag = 0;
        memset(&_recv_over._over, 0, sizeof(_recv_over._over));
        _recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
        _recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
        WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag, &_recv_over._over, 0);
    }

    void do_send(void* packet)
    {
        OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
        WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
    }

    void send_login_info_packet()
    {
        SC_LOGIN_INFO_PACKET p;
        p.id = _id;
        p.size = sizeof(SC_LOGIN_INFO_PACKET);
        p.type = SC_LOGIN_INFO;
        p.x = x;
        p.y = y;
        do_send(&p);
    }

    void send_move_packet(int c_id);

    void send_add_player_packet(int c_id);

    void send_remove_player_packet(int c_id)
    {
        _vl.lock();

        if (_view_list.count(c_id))
        {
            _view_list.erase(c_id);
        }

        else
        {
            _vl.unlock();
            return;
        }

        _vl.unlock();

        SC_REMOVE_PLAYER_PACKET p;
        p.id = c_id;
        p.size = sizeof(p);
        p.type = SC_REMOVE_PLAYER;
        do_send(&p);
    }
};

concurrency::concurrent_unordered_map<int, shared_ptr<SESSION>> clients;
SOCKET g_s_socket = INVALID_SOCKET, g_c_socket = INVALID_SOCKET;
OVER_EXP g_a_over;

bool can_see(int from, int to)
{
    if (abs(clients[from]->x - clients[to]->x) > VIEW_RANGE)
    {
        return false;
    }

    return abs(clients[from]->y - clients[to]->y) <= VIEW_RANGE;
}

void SESSION::send_move_packet(int c_id)
{
    SC_MOVE_PLAYER_PACKET p;
    p.id = c_id;
    p.size = sizeof(SC_MOVE_PLAYER_PACKET);
    p.type = SC_MOVE_PLAYER;
    p.x = clients[c_id]->x;
    p.y = clients[c_id]->y;
    p.move_time = clients[c_id]->last_move_time;

    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
    SC_ADD_PLAYER_PACKET add_packet;
    add_packet.id = c_id;
    strcpy_s(add_packet.name, clients[c_id]->_name);
    add_packet.size = sizeof(add_packet);
    add_packet.type = SC_ADD_PLAYER;
    add_packet.x = clients[c_id]->x;
    add_packet.y = clients[c_id]->y;

    _vl.lock();
    _view_list.insert(c_id);
    _vl.unlock();

    do_send(&add_packet);
}

int get_new_client_id()
{
    for (int i = 0; i < MAX_USER; ++i) 
    {
        lock_guard <mutex> ll{ clients[i]->_s_lock };

        if (clients[i]->_state == ST_FREE)
        {
            return i;
        }
    }

    return -1;
}


void process_packet(int c_id, char* packet) {
    switch (packet[1]) 
    {
    case CS_LOGIN: 
    {
        CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);

        strcpy_s(clients[c_id]->_name, p->name);

        {
            lock_guard<mutex> ll{ clients[c_id]->_s_lock };
            clients[c_id]->x = rand() % W_WIDTH;
            clients[c_id]->y = rand() % W_HEIGHT;
            clients[c_id]->_state = ST_INGAME;
        }

        {
            int idx = get_sector_index(clients[c_id]->x, clients[c_id]->y);
            lock_guard<mutex> skl(sector_mutexes[idx]);
            sectors[idx].insert(c_id);
        }

        clients[c_id]->send_login_info_packet();

        for (auto& pl : clients) 
        {

            {
                lock_guard<mutex> ll(pl.second->_s_lock);

                if (ST_INGAME != pl.second->_state)
                {
                    continue;
                }
            }

            if (pl.second->_id == c_id)
            {
                continue;
            }

            if (false == can_see(c_id, pl.second->_id))
            {
                continue;
            }

            pl.second->send_add_player_packet(c_id);
            clients[c_id]->send_add_player_packet(pl.second->_id);
        }
        break;
    }

    case CS_MOVE: 
    {
        CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
        clients[c_id]->last_move_time = p->move_time;

        short old_x, old_y, new_x, new_y;

        {
            old_x = clients[c_id]->x;
            old_y = clients[c_id]->y;


            switch (p->direction) 
            {
            case 0: 
                if (old_y > 0)
                {
                    old_y--;
                }

                break;

            case 1: 
                if (old_y < W_HEIGHT - 1)
                {
                    old_y++;
                }

                break;

            case 2: 
                if (old_x > 0)
                {
                    old_x--;
                }

                break;

            case 3: 
                if (old_x < W_WIDTH - 1)
                {
                    old_x++;
                }

                break;
            }

            clients[c_id]->x = new_x = old_x;
            clients[c_id]->y = new_y = old_y;
        }

        {
            int old_idx = get_sector_index(old_x, old_y);
            int new_idx = get_sector_index(new_x, new_y);

            if (old_idx != new_idx) 
            {
                int a = std::min(old_idx, new_idx), b = std::max(old_idx, new_idx);
                std::scoped_lock lock(sector_mutexes[a], sector_mutexes[b]);
                sectors[old_idx].erase(c_id);
                sectors[new_idx].insert(c_id);
            }
        }

        clients[c_id]->send_move_packet(c_id);

        std::unordered_set<int> near_list;

        int min_sx = std::max(0, (new_x - VIEW_RANGE) / SECTOR_SIZE);
        int max_sx = std::min(SECTOR_X - 1, (new_x + VIEW_RANGE) / SECTOR_SIZE);
        int min_sy = std::max(0, (new_y - VIEW_RANGE) / SECTOR_SIZE);
        int max_sy = std::min(SECTOR_Y - 1, (new_y + VIEW_RANGE) / SECTOR_SIZE);

        for (int sy = min_sy; sy <= max_sy; ++sy) 
        {
            for (int sx = min_sx; sx <= max_sx; ++sx) 
            {
                int idx = sy * SECTOR_X + sx;
                lock_guard<mutex> skl(sector_mutexes[idx]);

                for (int pid : sectors[idx]) 
                {
                    if (pid == c_id)
                    {
                        continue;
                    }

                    if (can_see(c_id, pid))
                    {
                        near_list.insert(pid);
                    }
                }
            }
        }

        {
            for (int pid : near_list) 
            {
                if (clients[c_id]->_view_list.insert(pid).second) 
                {
                    clients[c_id]->send_add_player_packet(pid);
                }
            }

            for (int pid : near_list) 
            {
                auto other = clients[pid];

                if (!other)
                {
                    continue;
                }

                other->send_move_packet(c_id);
            }

            std::vector<int> to_remove;

            for (int pid : clients[c_id]->_view_list) 
            {
                if (!near_list.count(pid))
                {
                    to_remove.push_back(pid);
                }
            }

            for (int pid : to_remove) 
            {
                clients[c_id]->send_remove_player_packet(pid);
                clients[c_id]->_view_list.erase(pid);
            }
        }

        break;
    }
    }
}
 
void disconnect(int c_id)
{
    clients[c_id]->_vl.lock();
    unordered_set<int> vl = clients[c_id]->_view_list;
    clients[c_id]->_vl.unlock();

    for (auto& p_id : vl)
    {
        auto& pl = clients[p_id];

        {
            lock_guard<mutex> ll(pl->_s_lock);

            if (ST_INGAME != pl->_state)
            {
                continue;
            }

            if (pl->_id == c_id)
            {
                continue;
            }

            pl->send_remove_player_packet(c_id);
        }

        closesocket(clients[c_id]->_socket);

        lock_guard<mutex> ll(clients[c_id]->_s_lock);
        clients[c_id]->_state = ST_FREE;
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
            if (ex_over->_comp_type == OP_ACCEPT)
            {
                cout << "Accept Error";
            }

            else
            {
                cout << "GQCS Error on client[" << key << "]\n";
                disconnect(static_cast<int>(key));

                if (ex_over->_comp_type == OP_SEND)
                {
                    delete ex_over;
                }

                continue;
            }
        }

        if ((0 == num_bytes) && ((ex_over->_comp_type == OP_RECV) || (ex_over->_comp_type == OP_SEND)))
        {
            disconnect(static_cast<int>(key));

            if (ex_over->_comp_type == OP_SEND)
            {
                delete ex_over;
            }

            continue;
        }

        switch (ex_over->_comp_type)
        {
        case OP_ACCEPT: {
            int client_id = get_new_client_id();

            if (client_id != -1)
            {
                {
                    lock_guard<mutex> ll(clients[client_id]->_s_lock);
                    clients[client_id]->_state = ST_ALLOC;
                }

                clients[client_id]->x = 0;
                clients[client_id]->y = 0;
                clients[client_id]->_id = client_id;
                clients[client_id]->_name[0] = 0;
                clients[client_id]->_prev_remain = 0;
                clients[client_id]->_socket = g_c_socket;
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket), h_iocp, client_id, 0);
                clients[client_id]->do_recv();
                g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
            }

            else
            {
                cout << "Max user exceeded.\n";
            }

            ZeroMemory(&g_a_over._over, sizeof(g_a_over._over));
            int addr_size = sizeof(SOCKADDR_IN);
            AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, nullptr, &g_a_over._over);
            break;
        }

        case OP_RECV:
        {
            int remain_data = num_bytes + clients[key]->_prev_remain;
            char* p = ex_over->_send_buf;

            while (remain_data > 0)
            {
                int packet_size = p[0];

                if (packet_size <= remain_data)
                {
                    process_packet(static_cast<int>(key), p);
                    p = p + packet_size;
                    remain_data = remain_data - packet_size;
                }

                else
                {
                    break;
                }
            }

            clients[key]->_prev_remain = remain_data;

            if (remain_data > 0)
            {
                memcpy(ex_over->_send_buf, p, remain_data);
            }

            clients[key]->do_recv();
            break;
        }

        case OP_SEND:
            delete ex_over;
            break;
        }
    }
}

int main() {
    HANDLE h_iocp;

    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);

    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

    SOCKADDR_IN server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUM);
    server_addr.sin_addr.S_un.S_addr = INADDR_ANY;

    bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    listen(g_s_socket, SOMAXCONN);

    SOCKADDR_IN cl_addr;
    int addr_size = sizeof(cl_addr);

    h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);

    for (int i = 0; i < MAX_USER; ++i)
    {
        clients.insert({ i, std::make_shared<SESSION>() });
    }

    g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    g_a_over._comp_type = OP_ACCEPT;
    AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

    vector <thread> worker_threads;
    int num_threads = std::thread::hardware_concurrency();

    for (int i = 0; i < num_threads; ++i)
    {
        worker_threads.emplace_back(worker_thread, h_iocp);
    }

    for (auto& th : worker_threads)
    {
        th.join();
    }

    closesocket(g_s_socket);
    WSACleanup();
}
