#include "stdafx.h"
#include "ai.h"
#include "db.h"
#include "logic.h"
#include "network.h"
#include "script.h"
#include "session.h"
#include "timer.h"
#include "world.h"
#include "protocol.h"

#include "include/lua.hpp"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

HANDLE h_iocp;
SOCKET g_s_socket;
SOCKET g_c_socket;
OVER_EXP g_a_over;

concurrency::concurrent_unordered_map<int, std::shared_ptr<SESSION>> clients;
concurrency::concurrent_priority_queue<event_type> timer_queue;

SQLHENV g_hEnv = SQL_NULL_HENV;
SQLHDBC g_hDbc = SQL_NULL_HDBC;

mutex db_mutex;

int main()
{
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);

    if (!DB_Initialize(L"2025_GameServer"))
    {
        fprintf(stderr, "DB RESET FAILED, SERVER END ... \n");
        return -1;
    }

    std::uniform_int_distribution<> dist_x(0, W_WIDTH - 1);
    std::uniform_int_distribution<> dist_y(0, W_HEIGHT - 1);

    while ((int)obstacles.size() < OBSTACLE_COUNT)
    {
        short x = dist_x(gen);
        short y = dist_y(gen);
        int idx = y * W_WIDTH + x;

        if (!obstacle_map[idx])
        {
            obstacle_map[idx] = true;
            obstacles.emplace_back(x, y);
        }
    }

    cout << "OBSTACLES SPAWN COMPLETE ! \n";

    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

    SOCKADDR_IN server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUM);
    server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
    bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    listen(g_s_socket, SOMAXCONN);

    for (int i = 0; i < MAX_USER + MAX_NPC; ++i)
    {
        clients.insert({ i, make_shared<SESSION>() });
    }

    session_ptrs.resize(MAX_USER + MAX_NPC);

    for (int i = 0; i < MAX_USER + MAX_NPC; ++i)
    {
        session_ptrs[i] = clients[i].get();
    }

    initialize_npc();

    h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
    g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    g_a_over.comp_type = OP_ACCEPT;

    SOCKADDR_IN cl_addr;
    int addr_size = sizeof(cl_addr);
    AcceptEx(g_s_socket, g_c_socket, g_a_over.send_buf,
        0, addr_size + 16, addr_size + 16, 0, &g_a_over.over);

    vector<thread> worker_threads;
    int num_threads = thread::hardware_concurrency();

    for (int i = 0; i < num_threads; ++i)
    {
        worker_threads.emplace_back(worker_thread, h_iocp);
    }

    thread timer_thread{ do_timer };
    thread save_thread{ periodic_save };

    save_thread.detach();
    timer_thread.join();

    for (auto& th : worker_threads)
    {
        th.join();
    }

    closesocket(g_s_socket);
    WSACleanup();
    DB_Cleanup();
}