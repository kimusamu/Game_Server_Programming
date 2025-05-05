#include <iostream>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <vector>
#include <cstring>
#include "protocol.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

using namespace std;

enum COMP_TYPE { OP_RECV, OP_SEND };
enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

// 세션 컨테이너 & 동기화
static unordered_map<int, struct Session> sessions;
static shared_mutex                       sessions_mutex;

// 작업 큐 & 워커 스레드
struct PacketTask 
{
    int           sid;
    vector<char>  data;
};

static queue<PacketTask>   task_queue;
static mutex               task_mutex;
static condition_variable  task_cv;
static bool                shutting_down = false;
static vector<thread>      workers;

void process_packet(int sid, char* packet);
void disconnect(int sid);

void CALLBACK recv_callback(DWORD err, DWORD bytes, LPWSAOVERLAPPED ov, DWORD);
void CALLBACK send_callback(DWORD err, DWORD, LPWSAOVERLAPPED ov, DWORD);

class OVER_EXP 
{
public:
    WSAOVERLAPPED _over;
    WSABUF        _wsabuf;
    char          _buf[BUF_SIZE];
    COMP_TYPE     _comp_type;

    OVER_EXP(COMP_TYPE type = OP_RECV) : _comp_type(type) 
    {
        ZeroMemory(&_over, sizeof(_over));
        _wsabuf.buf = _buf;
        _wsabuf.len = BUF_SIZE;
    }

    OVER_EXP(char* packet) : OVER_EXP(OP_SEND) 
    {
        _wsabuf.len = static_cast<ULONG>(packet[0]);
        memcpy(_buf, packet, _wsabuf.len);
    }
};

// Session 관리
class Session 
{
public:
    atomic<S_STATE> state{ ST_FREE };
    atomic<int>     id{ -1 };
    SOCKET          sock{ INVALID_SOCKET };
    atomic<short>   x{ 0 }, y{ 0 };
    char            name[NAME_SIZE]{};
    atomic<int>     prev_remain{ 0 };
    atomic<int>     last_move_time{ 0 };
    OVER_EXP        recv_over{ OP_RECV };

public:
    void do_recv() 
    {
        DWORD flags = 0;
        ZeroMemory(&recv_over._over, sizeof(recv_over._over));
        int pr = prev_remain.load();
        recv_over._wsabuf.len = BUF_SIZE - pr;
        recv_over._wsabuf.buf = recv_over._buf + pr;
        recv_over._over.hEvent = (HANDLE)(long long)id.load();
        WSARecv(sock, &recv_over._wsabuf, 1, nullptr, &flags, &recv_over._over, recv_callback);
    }

    void do_send(void* packet) 
    {
        OVER_EXP* ov = new OVER_EXP{ reinterpret_cast<char*>(packet) };
        ov->_over.hEvent = (HANDLE)(long long)id.load();
        WSASend(sock, &ov->_wsabuf, 1, nullptr, 0, &ov->_over, send_callback);
    }

    void send_login_info_packet() 
    {
        SC_LOGIN_INFO_PACKET p;
        p.id = id.load();
        p.size = sizeof(p);
        p.type = SC_LOGIN_INFO;
        p.x = x.load();
        p.y = y.load();

        do_send(&p);
    }

    void send_move_packet(int cid) 
    {
        SC_MOVE_PLAYER_PACKET p;
        p.id = cid;
        p.size = sizeof(p);
        p.type = SC_MOVE_PLAYER;
        p.x = sessions[cid].x.load();
        p.y = sessions[cid].y.load();
        p.move_time = sessions[cid].last_move_time.load();

        do_send(&p);
    }

    void send_add_player_packet(int cid) 
    {
        SC_ADD_PLAYER_PACKET p;
        p.id = cid;
        strcpy_s(p.name, sessions[cid].name);
        p.size = sizeof(p);
        p.type = SC_ADD_PLAYER;
        p.x = sessions[cid].x.load();
        p.y = sessions[cid].y.load();

        do_send(&p);
    }

    void send_remove_player_packet(int cid) 
    {
        SC_REMOVE_PLAYER_PACKET p;
        p.id = cid;
        p.size = sizeof(p);
        p.type = SC_REMOVE_PLAYER;

        do_send(&p);
    }
};

// 작업 큐 & 워커 스레드
void worker_loop() 
{
    while (true) 
    {
        PacketTask task;

        {
            unique_lock lk(task_mutex);
            task_cv.wait(lk, [] { return !task_queue.empty() || shutting_down; });

            if (shutting_down && task_queue.empty())
            {
                break;
            }
                
            task = move(task_queue.front());
            task_queue.pop();
        }

        process_packet(task.sid, task.data.data());
    }
}

void enqueue_task(int sid, const char* buf, int len) 
{
    PacketTask t;
    t.sid = sid;
    t.data.assign(buf, buf + len);
    {
        lock_guard lk(task_mutex);
        task_queue.push(move(t));
    }

    task_cv.notify_one();
}

// 세션 할당, 해제
int allocate_session_id() 
{
    unique_lock lk(sessions_mutex);

    for (int i = 0; i < MAX_USER; ++i) 
    {
        auto it = sessions.find(i);

        if (it == sessions.end() || it->second.state.load() == ST_FREE) 
        {
            Session& s = sessions[i];
            s.state.store(ST_ALLOC);
            s.id.store(i);
            return i;
        }
    }

    return -1;
}

void disconnect(int sid) 
{
    {   
        shared_lock lk(sessions_mutex);

        for (auto& [oid, os] : sessions) {

            if (oid == sid || os.state.load() != ST_INGAME)
            {
                continue;
            }

            os.send_remove_player_packet(sid);
        }
    }

    {   
        unique_lock lk(sessions_mutex);
        sessions[sid].state.store(ST_FREE);
        closesocket(sessions[sid].sock);
    }
}

// 패킷 처리
void process_packet(int sid, char* packet) 
{
    switch (packet[1]) {
    case CS_LOGIN: 
    {
        auto* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
        {
            shared_lock lk(sessions_mutex);
            Session& s = sessions[sid];
            strcpy_s(s.name, p->name);
            s.send_login_info_packet();
        }

        vector<int> others;
        {
            shared_lock lk(sessions_mutex);
            for (auto& [oid, os] : sessions) {
                if (oid == sid || os.state.load() != ST_INGAME) continue;
                others.push_back(oid);
            }
        }

        for (int oid : others) 
        {
            sessions[oid].send_add_player_packet(sid);
            sessions[sid].send_add_player_packet(oid);
        }

        break;
    }

    case CS_MOVE: 
    {
        auto* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
        sessions[sid].last_move_time.store(p->move_time);
        short nx = sessions[sid].x.load();
        short ny = sessions[sid].y.load();

        switch (p->direction)
        {
        case 0:
            if (ny > 0)
            {
                ny--;
            }
            break;

        case 1:
            if (ny < W_HEIGHT - 1)
            {
                ny++;
            }
            break;

        case 2:
            if (nx > 0)
            {
                nx--;
            }
            break;

        case 3:
            if (nx < W_WIDTH - 1)
            {
                nx++;
            }
            break;
        }

        sessions[sid].x.store(nx);
        sessions[sid].y.store(ny);

        shared_lock lk(sessions_mutex);

        for (auto& [oid, os] : sessions) 
        {
            if (os.state.load() != ST_INGAME)
            {
                continue;
            }

            os.send_move_packet(sid);
        }

        break;
    }
    }
}

// I/O 콜백 흐름
void CALLBACK recv_callback(DWORD err, DWORD bytes, LPWSAOVERLAPPED ov, DWORD) 
{
    int sid = static_cast<int>((long long)ov->hEvent);
    Session* ps = nullptr;

    {
        shared_lock lk(sessions_mutex);
        auto it = sessions.find(sid);

        if (it == sessions.end())
        {
            return;
        }

        ps = &it->second;
    }

    Session& s = *ps;

    if (err) 
    {
        disconnect(sid);
        return;
    }

    OVER_EXP* ex = reinterpret_cast<OVER_EXP*>(ov);
    int total = bytes + s.prev_remain.load();
    char* buf = ex->_buf;

    while (total > 0) 
    {
        int sz = buf[0];

        if (sz <= total) 
        {
            enqueue_task(sid, buf, sz);
            buf += sz;
            total -= sz;
        }

        else
        {
            break;
        }
    }

    s.prev_remain.store(total);

    if (total > 0)
    {
        memcpy(ex->_buf, buf, total);
    }

    s.do_recv();
}

void CALLBACK send_callback(DWORD err, DWORD, LPWSAOVERLAPPED ov, DWORD) 
{
    int sid = static_cast<int>((long long)ov->hEvent);

    if (err)
    {
        disconnect(sid);
    }

    delete ov;
}

// 메인 함수 흐름
int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    int nThreads = max(1u, thread::hardware_concurrency());

    for (int i = 0; i < nThreads; ++i)
    {
        workers.emplace_back(worker_loop);
    }
        
    SOCKET listen_sock = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT_NUM);
    srv.sin_addr.S_un.S_addr = INADDR_ANY;
    bind(listen_sock, (sockaddr*)&srv, sizeof(srv));
    listen(listen_sock, SOMAXCONN);

    while (true) 
    {
        SOCKADDR_IN cli{};
        int cli_sz = sizeof(cli);
        SOCKET client = WSAAccept(listen_sock, (sockaddr*)&cli, &cli_sz, nullptr, NULL);

        if (client == INVALID_SOCKET)
        {
            continue;
        }

        int sid = allocate_session_id();

        if (sid < 0) 
        {
            cout << "Max user exceeded\n";
            closesocket(client);
            continue;
        }

        {   
            unique_lock lk(sessions_mutex);
            Session& s = sessions[sid];
            s.sock = client;
            s.x.store(0);     
            s.y.store(0);
            s.prev_remain.store(0);
            s.last_move_time.store(0);
            s.name[0] = '\0';
            s.state.store(ST_INGAME);
        }

        sessions[sid].do_recv();
    }

    shutting_down = true;
    task_cv.notify_all();

    for (auto& t : workers)
    {
        t.join();
    }

    closesocket(listen_sock);
    WSACleanup();
}
