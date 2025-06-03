#define NOMINMAX

#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <concurrent_unordered_map.h>
#include <concurrent_priority_queue.h>
#include <atomic>
#include <memory>
#include <chrono>
#include <queue>
#include <algorithm>
#include <sqlext.h>

#include "protocol.h"
#include "include/lua.hpp"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

using namespace std;
using namespace chrono;

constexpr int VIEW_RANGE = 5;
constexpr int SECTOR_SIZE = 16;
constexpr int SECTOR_X = (W_WIDTH + SECTOR_SIZE - 1) / SECTOR_SIZE;
constexpr int SECTOR_Y = (W_HEIGHT + SECTOR_SIZE - 1) / SECTOR_SIZE;
constexpr int TOTAL_SECTORS = SECTOR_X * SECTOR_Y;

constexpr int MAX_HP = 3;
constexpr auto INVINCIBLE_ON_HIT = 5s;
constexpr auto INVINCIBLE_ON_RESPAWN = 10s;

enum EVENT_TYPE { EV_RANDOM_MOVE, EV_MOVE, EV_HEAL, EV_ATTACK };

struct event_type
{
    int obj_id;
    chrono::high_resolution_clock::time_point wakeup_time;
    EVENT_TYPE event_id;
    int target_id;

    bool operator<(const event_type& other) const
    {
        return wakeup_time > other.wakeup_time;
    }
};

concurrency::concurrent_priority_queue<event_type> timer_queue;

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_AI_HELLO };

class OVER_EXP
{
public:
    WSAOVERLAPPED _over;
    WSABUF _wsabuf;
    char _send_buf[BUF_SIZE];
    COMP_TYPE _comp_type;
    int _ai_target_obj;

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
        ZeroMemory(&_over, sizeof(_over));
        _comp_type = OP_SEND;
        memcpy(_send_buf, packet, packet[0]);
    }
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

class SESSION
{
    OVER_EXP _recv_over;

public:
    atomic<S_STATE> _state;
    SOCKET _socket;

    mutex _s_lock;
    mutex _ll;
    mutex _vl;

    unordered_set<int> _view_list;
    atomic_bool _is_active;
    lua_State* _L;

    int _id;
    short x, y;
    char _name[NAME_SIZE];
    int _prev_remain;
    long long last_move_time;

    int hp;
    short spawn_x, spawn_y;
    bool is_invincible;
    high_resolution_clock::time_point invincible_end_time;

    bool _greet_mode = false;
    int  _greet_moves_left = 0;
    int  _greet_target = -1;

public:
    SESSION()
    {
        _id = -1;
        _socket = 0;
        x = 0;
        y = 0;
        _name[0] = 0;
        _state = ST_FREE;
        _prev_remain = 0;
        last_move_time = 0;
        _is_active = false;
        _L = nullptr;

        hp = MAX_HP;
        spawn_x = spawn_y = 0;
        is_invincible = false;
        invincible_end_time = high_resolution_clock::now();
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
        p.hp = hp;

        do_send(&p);
    }

    void send_move_packet(int c_id);

    void send_stat_change_packet()
    {
        SC_STAT_CHANGEL_PACKET p;
        p.size = sizeof(SC_STAT_CHANGEL_PACKET);
        p.type = SC_STAT_CHANGE;
        p.id = _id;
        p.hp = hp;
        p.max_hp = MAX_HP;

        do_send(&p);
    }

    void send_add_player_packet(int c_id);

    void send_chat_packet(int c_id, const char* mess);

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

        SC_REMOVE_OBJECT_PACKET p;
        p.id = c_id;
        p.size = sizeof(p);
        p.type = SC_REMOVE_OBJECT;

        do_send(&p);
    }
};

static vector<unordered_set<SESSION*>> sectors(TOTAL_SECTORS);
static vector<mutex> sector_mutexes(TOTAL_SECTORS);
static vector<SESSION*> session_ptrs;

HANDLE h_iocp;
concurrency::concurrent_unordered_map<int, shared_ptr<SESSION>> clients;
SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

inline int get_sector_index(int x, int y)
{
    int sx = clamp(x / SECTOR_SIZE, 0, SECTOR_X - 1);
    int sy = clamp(y / SECTOR_SIZE, 0, SECTOR_Y - 1);

    return sy * SECTOR_X + sx;
}

void update_sector(int id, int old_x, int old_y, int new_x, int new_y)
{
    int old_idx = get_sector_index(old_x, old_y);
    int new_idx = get_sector_index(new_x, new_y);

    if (old_idx == new_idx)
    {
        return;
    }

    SESSION* sess = session_ptrs[id];

    {
        lock_guard<mutex> lk(sector_mutexes[old_idx]);
        sectors[old_idx].erase(sess);
    }

    {
        lock_guard<mutex> lk(sector_mutexes[new_idx]);
        sectors[new_idx].insert(sess);
    }
}

bool is_pc(int object_id)
{
    return object_id < MAX_USER;
}

bool is_npc(int object_id)
{
    return !is_pc(object_id);
}

inline bool can_see_inline(SESSION* from, SESSION* to)
{
    if (abs(from->x - to->x) > VIEW_RANGE)
    {
        return false;
    }

    if (abs(from->y - to->y) > VIEW_RANGE)
    {
        return false;
    }

    return true;
}

static vector<int> get_neighbor_sectors(int sec_idx)
{
    int sx = sec_idx % SECTOR_X;
    int sy = sec_idx / SECTOR_X;

    vector<int> neigh;
    neigh.reserve(9);

    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int nx = sx + dx;
            int ny = sy + dy;

            if (nx < 0 || nx >= SECTOR_X || ny < 0 || ny >= SECTOR_Y)
            {
                continue;
            }

            neigh.push_back(ny * SECTOR_X + nx);
        }
    }

    return neigh;
}

unordered_set<int> gather_visible(int viewer_id)
{
    SESSION* viewer = session_ptrs[viewer_id];
    int sec = get_sector_index(viewer->x, viewer->y);
    auto neigh = get_neighbor_sectors(sec);
    unordered_set<int> vis;

    for (int s : neigh)
    {
        lock_guard<mutex> lk(sector_mutexes[s]);

        for (SESSION* peer : sectors[s])
        {
            int other_id = peer->_id;

            if (other_id == viewer_id)
            {
                continue;
            }

            if (peer->_state.load() == ST_INGAME && can_see_inline(viewer, peer))
            {
                vis.insert(other_id);
            }
        }
    }

    return vis;
}

void SESSION::send_move_packet(int c_id)
{
    SC_MOVE_OBJECT_PACKET p;
    p.id = c_id;
    p.size = sizeof(SC_MOVE_OBJECT_PACKET);
    p.type = SC_MOVE_OBJECT;
    p.x = clients[c_id]->x;
    p.y = clients[c_id]->y;
    p.move_time = clients[c_id]->last_move_time;

    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
    SC_ADD_OBJECT_PACKET add_packet;
    add_packet.id = c_id;
    strcpy_s(add_packet.name, clients[c_id]->_name);
    add_packet.size = sizeof(add_packet);
    add_packet.type = SC_ADD_OBJECT;
    add_packet.x = clients[c_id]->x;
    add_packet.y = clients[c_id]->y;

    _vl.lock();
    _view_list.insert(c_id);
    _vl.unlock();

    do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess)
{
    SC_CHAT_PACKET packet;
    packet.id = p_id;
    packet.size = sizeof(SC_CHAT_PACKET);
    packet.type = SC_CHAT;

    strcpy_s(packet.mess, mess);
    do_send(&packet);
}

bool positions_equal(const SESSION* a, const SESSION* b)
{
    return (a->x == b->x && a->y == b->y);
}

void handle_respawn(shared_ptr<SESSION> s)
{
    int id = s->_id;
    const char* typeStr = is_pc(id) ? "Player" : "NPC";

    int old_x = s->x;
    int old_y = s->y;
    cout << "[" << typeStr << " " << id << "] Respawn at (" << s->spawn_x << ", " << s->spawn_y << ").\n";

    update_sector(id, old_x, old_y, s->spawn_x, s->spawn_y);

    s->x = s->spawn_x;
    s->y = s->spawn_y;
    s->hp = MAX_HP;
    s->is_invincible = true;
    s->invincible_end_time = high_resolution_clock::now() + INVINCIBLE_ON_RESPAWN;

    s->send_stat_change_packet();

    auto near_list = gather_visible(id);

    for (int peer_id : near_list)
    {
        clients[peer_id]->send_remove_player_packet(id);
    }

    auto new_vis = gather_visible(id);

    for (int peer_id : new_vis)
    {
        clients[peer_id]->send_add_player_packet(id);
    }

    s->send_move_packet(id);
}

void handle_damage(shared_ptr<SESSION> a, shared_ptr<SESSION> b)
{
    auto now = high_resolution_clock::now();

    if (a->is_invincible && now < a->invincible_end_time)
    {
        return;
    }

    if (b->is_invincible && now < b->invincible_end_time)
    {
        return;
    }

    a->hp -= 1;
    b->hp -= 1;

    a->is_invincible = true;
    a->invincible_end_time = now + INVINCIBLE_ON_HIT;

    b->is_invincible = true;
    b->invincible_end_time = now + INVINCIBLE_ON_HIT;

    a->send_stat_change_packet();
    b->send_stat_change_packet();

    const char* typeA = is_pc(a->_id) ? "Player" : "NPC";
    const char* typeB = is_pc(b->_id) ? "Player" : "NPC";

    cout << "[" << typeA << " " << a->_id << "] was hit. Remaining HP: " << a->hp << "\n";
    cout << "[" << typeB << " " << b->_id << "] was hit. Remaining HP: " << b->hp << "\n";

    if (a->hp <= 0)
    {
        cout << "[" << typeA << " " << a->_id << "] HP dropped to 0: respawning.\n";
        handle_respawn(a);
    }

    if (b->hp <= 0)
    {
        cout << "[" << typeB << " " << b->_id << "] HP dropped to 0: respawning.\n";
        handle_respawn(b);
    }
}

int get_new_client_id()
{
    for (int i = 0; i < MAX_USER; ++i)
    {
        lock_guard<mutex> ll{ clients[i]->_s_lock };

        if (clients[i]->_state.load() == ST_FREE)
        {
            return i;
        }
    }

    return -1;
}

static SQLHENV g_hEnv = SQL_NULL_HENV;
static SQLHDBC g_hDbc = SQL_NULL_HDBC;

void PrintOdbcError(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE retcode) 
{
    SQLSMALLINT  iRec = 0;
    SQLINTEGER   iError;
    WCHAR        wszMessage[1000];
    WCHAR        wszState[SQL_SQLSTATE_SIZE + 1];

    if (retcode == SQL_INVALID_HANDLE) 
    {
        fwprintf(stderr, L"[ODBC] Invalid handle\n");
        return;
    }

    while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage, (SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), NULL) == SQL_SUCCESS) 
    {
        if (wcsncmp(wszState, L"01004", 5)) 
        {
            fwprintf(stderr, L"[ODBC][%5.5s] %s (%d)\n", wszState, wszMessage, iError);
        }
    }
}

bool DB_Initialize(const wchar_t* dsnName) 
{
    RETCODE retcode;

    setlocale(LC_ALL, "korean");

    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &g_hEnv);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        fprintf(stderr, "[DB] SQLAllocHandle ENV 실패\n");
        return false;
    }

    retcode = SQLSetEnvAttr(g_hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        fprintf(stderr, "[DB] SQLSetEnvAttr 실패\n");
        PrintOdbcError(g_hEnv, SQL_HANDLE_ENV, retcode);
        return false;
    }

    retcode = SQLAllocHandle(SQL_HANDLE_DBC, g_hEnv, &g_hDbc);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        fprintf(stderr, "[DB] SQLAllocHandle DBC 실패\n");
        PrintOdbcError(g_hEnv, SQL_HANDLE_ENV, retcode);
        return false;
    }

    retcode = SQLConnectW(g_hDbc, (SQLWCHAR*)dsnName, SQL_NTS, nullptr, 0, nullptr, 0);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        fprintf(stderr, "[DB] SQLConnect 실패 DSN=%ls\n", dsnName);
        PrintOdbcError(g_hDbc, SQL_HANDLE_DBC, retcode);
        return false;
    }

    wprintf(L"[DB] DSN=%ls에 정상 연결됨\n", dsnName);

    return true;
}

void DB_Cleanup() 
{
    if (g_hDbc != SQL_NULL_HDBC) {
        SQLDisconnect(g_hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, g_hDbc);
        g_hDbc = SQL_NULL_HDBC;
    }

    if (g_hEnv != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, g_hEnv);
        g_hEnv = SQL_NULL_HENV;
    }
}

bool DB_LoadPlayerPosition(int user_id, int& out_x, int& out_y) 
{
    RETCODE retcode;
    SQLHSTMT   hStmt = SQL_NULL_HSTMT;
    SQLINTEGER sql_user_x = 0, sql_user_y = 0;
    SQLLEN     cb_user_x = 0, cb_user_y = 0, cb_dummy = 0;

    setlocale(LC_ALL, "korean");

    retcode = SQLAllocHandle(SQL_HANDLE_STMT, g_hDbc, &hStmt);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        PrintOdbcError(g_hDbc, SQL_HANDLE_DBC, retcode);
        return false;
    }

    retcode = SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, 0, NULL);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        PrintOdbcError(hStmt, SQL_HANDLE_STMT, retcode);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    retcode = SQLExecDirectW(hStmt, (SQLWCHAR*)L"SELECT user_x, user_y FROM dbo.user_table WHERE user_id = ?", SQL_NTS);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        PrintOdbcError(hStmt, SQL_HANDLE_STMT, retcode);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    retcode = SQLBindCol(hStmt, 1, SQL_C_LONG, &sql_user_x, sizeof(sql_user_x), &cb_user_x);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        PrintOdbcError(hStmt, SQL_HANDLE_STMT, retcode);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    retcode = SQLBindCol(hStmt, 2, SQL_C_LONG, &sql_user_y, sizeof(sql_user_y), &cb_user_y);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        PrintOdbcError(hStmt, SQL_HANDLE_STMT, retcode);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    retcode = SQLFetch(hStmt);

    if (retcode == SQL_NO_DATA) 
    {
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    else if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        PrintOdbcError(hStmt, SQL_HANDLE_STMT, retcode);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    out_x = (int)sql_user_x;
    out_y = (int)sql_user_y;

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return true;
}

void DB_InsertPlayerPosition(int user_id, short x, short y) {
    wprintf(L"[DEBUG] DB_InsertPlayerPosition 호출: user_id=%d, x=%d, y=%d\n", user_id, x, y);

    RETCODE retcode;
    SQLHSTMT hStmt = SQL_NULL_HSTMT;
    retcode = SQLAllocHandle(SQL_HANDLE_STMT, g_hDbc, &hStmt);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        PrintOdbcError(g_hDbc, SQL_HANDLE_DBC, retcode);
        return;
    }

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, 0, NULL);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_SMALLINT, 0, 0, &x, 0, NULL);
    SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_SMALLINT, 0, 0, &y, 0, NULL);

    retcode = SQLExecDirectW(hStmt,
        (SQLWCHAR*)L"INSERT INTO dbo.user_table(user_id, user_x, user_y) "
        L"     VALUES (?, ?, ?)",
        SQL_NTS);

    wprintf(L"[DEBUG] SQLExecDirect (INSERT) -> retcode=%d\n", retcode);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        wprintf(L"[ERROR] INSERT 쿼리 실패 -> retcode=%d\n", retcode);
        PrintOdbcError(hStmt, SQL_HANDLE_STMT, retcode);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
}

void DB_SavePlayerPosition(int user_id, int x, int y) {
    wprintf(L"[DEBUG] DB_SaveOrInsertPlayerPosition 호출: user_id=%d, x=%d, y=%d\n", user_id, x, y);

    RETCODE retcode;
    SQLHSTMT hStmt = SQL_NULL_HSTMT;
    retcode = SQLAllocHandle(SQL_HANDLE_STMT, g_hDbc, &hStmt);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) 
    {
        PrintOdbcError(g_hDbc, SQL_HANDLE_DBC, retcode);
        return;
    }

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_SMALLINT, 0, 0, &x, 0, NULL);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_SMALLINT, 0, 0, &y, 0, NULL);
    SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &user_id, 0, NULL);

    retcode = SQLExecDirectW(hStmt,
        (SQLWCHAR*)L"UPDATE dbo.user_table "
        L"   SET user_x = ?, user_y = ? "
        L" WHERE user_id = ?",
        SQL_NTS);    
    
    wprintf(L"[DEBUG] SQLExecDirect (UPDATE) -> retcode=%d\n", retcode);

    if (retcode == SQL_ERROR || retcode == SQL_INVALID_HANDLE) 
    {
        wprintf(L"[ERROR] UPDATE 쿼리 자체 실패 -> retcode=%d\n", retcode);
        PrintOdbcError(hStmt, SQL_HANDLE_STMT, retcode);
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

        return;
    }

    SQLLEN rowcount = 0;
    SQLRowCount(hStmt, &rowcount);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

    if (rowcount == 0) 
    {
        wprintf(L"[DEBUG] UPDATE로 갱신된 행 없음, INSERT 시도: user_id=%d\n", user_id);
        DB_InsertPlayerPosition(user_id, x, y);
    }

    else 
    {
        wprintf(L"[DEBUG] UPDATE 성공: user_id=%d, 변경된 행수=%lld\n", user_id, rowcount);
    }
}

void wake_up_npc(int npc_id, int waker)
{
    OVER_EXP* exp_over = new OVER_EXP;
    exp_over->_comp_type = OP_AI_HELLO;
    exp_over->_ai_target_obj = waker;
    PostQueuedCompletionStatus(h_iocp, 1, npc_id, &exp_over->_over);

    if (clients[npc_id]->_is_active.load())
    {
        return;
    }

    bool old_state = false;

    if (!atomic_compare_exchange_strong(&clients[npc_id]->_is_active, &old_state, true))
    {
        return;
    }

    event_type et{ npc_id, chrono::high_resolution_clock::now(), EV_RANDOM_MOVE, 0 };
    timer_queue.push(et);
}

void process_packet(int c_id, char* packet)
{
    switch (packet[1])
    {
    case CS_LOGIN:
    {
        CS_LOGIN_PACKET *p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
        int user_id = atoi(p->name);
        int saved_x = 0, saved_y = 0;
        bool exist = DB_LoadPlayerPosition(user_id, saved_x, saved_y);

        if (!exist) {
            SC_LOGIN_FAIL_PACKET fail_pkt;
            fail_pkt.size = sizeof(fail_pkt);
            fail_pkt.type = SC_LOGIN_FAIL;

            clients[c_id]->do_send(&fail_pkt);

            closesocket(clients[c_id]->_socket);
            lock_guard<mutex> ll{ clients[c_id]->_s_lock };
            clients[c_id]->_state.store(ST_FREE);

            break;
        }

        strcpy_s(clients[c_id]->_name, p->name);

        {
            lock_guard<mutex> ll{ clients[c_id]->_s_lock };

            clients[c_id]->x = saved_x;
            clients[c_id]->y = saved_y;
            clients[c_id]->_state.store(ST_INGAME);
            clients[c_id]->spawn_x = saved_x;
            clients[c_id]->spawn_y = saved_y;
            clients[c_id]->hp = MAX_HP;
            clients[c_id]->is_invincible = true;
            clients[c_id]->invincible_end_time = high_resolution_clock::now() + INVINCIBLE_ON_RESPAWN;
        }

        update_sector(c_id, 0, 0, clients[c_id]->x, clients[c_id]->y);
        clients[c_id]->send_login_info_packet();

        for (auto& pl_pair : clients)
        {
            int other_id = pl_pair.first;
            auto& pl = pl_pair.second;

            {
                lock_guard<mutex> ll{ pl->_s_lock };

                if (pl->_state.load() != ST_INGAME)
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
            if (y > 0)
            {
                y--;
            }

            break;

        case 1:
            if (y < W_HEIGHT - 1)
            {
                y++;
            }

            break;

        case 2:
            if (x > 0)
            {
                x--;
            }

            break;

        case 3:
            if (x < W_WIDTH - 1)
            {
                x++;
            }

            break;
        }

        clients[c_id]->x = x;
        clients[c_id]->y = y;

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

        clients[c_id]->_vl.lock();
        unordered_set<int> old_vlist = clients[c_id]->_view_list;
        clients[c_id]->_vl.unlock();

        clients[c_id]->send_move_packet(c_id);

        for (int oid : near_list)
        {
            auto& peer = clients[oid];
            peer->_vl.lock();
            bool already = peer->_view_list.count(c_id);
            peer->_vl.unlock();

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
            if (!near_list.count(oid))
            {
                clients[c_id]->send_remove_player_packet(oid);

                if (is_pc(oid))
                {
                    clients[oid]->send_remove_player_packet(c_id);
                }
            }
        }

        int user_id = atoi(clients[c_id]->_name);
        DB_SavePlayerPosition(user_id, x, y);

        break;
    }

    default:
        break;
    }
}

void disconnect(int c_id)
{
    int user_id = atoi(clients[c_id]->_name);
    short last_x = clients[c_id]->x;
    short last_y = clients[c_id]->y;
    DB_SavePlayerPosition(user_id, last_x, last_y);

    clients[c_id]->_vl.lock();
    unordered_set<int> vl = clients[c_id]->_view_list;
    clients[c_id]->_vl.unlock();

    for (auto& p_id : vl)
    {
        if (is_npc(p_id))
        {
            continue;
        }

        auto& pl = clients[p_id];

        {
            lock_guard<mutex> ll(pl->_s_lock);

            if (pl->_state.load() != ST_INGAME)
            {
                continue;
            }
        }

        if (pl->_id == c_id)
        {
            continue;
        }

        pl->send_remove_player_packet(c_id);
    }

    closesocket(clients[c_id]->_socket);
    lock_guard<mutex> ll(clients[c_id]->_s_lock);
    clients[c_id]->_state.store(ST_FREE);
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
        if (x < ex)
        {
            x++;
        }
        break;

    case 1:
        if (x > sx)
        {
            x--;
        }
        break;

    case 2:
        if (y < ey)
        {
            y++;
        }
        break;

    case 3:
        if (y > sy)
        {
            y--;
        }
        break;
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
        lock_guard<mutex> lk(npc->_vl);
        old_vl = npc->_view_list;
    }

    auto new_vl = gather_visible(npc_id);

    {
        lock_guard<mutex> lk(npc->_vl);
        npc->_view_list = new_vl;
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

    long long current_time = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    clients[npc_id]->last_move_time = current_time;
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
        case OP_ACCEPT:
        {
            int client_id = get_new_client_id();

            if (client_id != -1)
            {
                {
                    lock_guard<mutex> ll{ clients[client_id]->_s_lock };
                    clients[client_id]->_state.store(ST_ALLOC);
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
                    p += packet_size;
                    remain_data -= packet_size;
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
        {
            delete ex_over;
            break;
        }

        case OP_NPC_MOVE:
        {
            int npc_id = static_cast<int>(key);
            SESSION* npc = session_ptrs[npc_id];

            {
                lock_guard<mutex> lk(npc->_s_lock);

                if (npc->_greet_mode)
                {
                    do_npc_random_move(npc_id);
                    npc->_greet_moves_left--;

                    if (npc->_greet_moves_left > 0)
                    {
                        event_type et{ npc_id, high_resolution_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
                        timer_queue.push(et);
                    }

                    else
                    {
                        lock_guard<mutex> lk2(npc->_ll);
                        lua_State* L = npc->_L;
                        lua_getglobal(L, "API_SendMessage");
                        lua_pushinteger(L, npc_id);
                        lua_pushinteger(L, npc->_greet_target);
                        lua_pushstring(L, "BYE");

                        if (lua_pcall(L, 3, 0, 0) != LUA_OK)
                        {
                            const char* err = lua_tostring(L, -1);
                            cerr << "[Lua Error] API_SendMessage: " << err << "\n";
                            lua_pop(L, 1);
                        }

                        npc->_greet_mode = false;
                        event_type et{ npc_id, high_resolution_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
                        timer_queue.push(et);
                        npc->_is_active.store(true);
                    }

                    delete ex_over;
                    break;
                }
            }

            auto visible_players = gather_visible(npc_id);

            if (!visible_players.empty())
            {
                do_npc_random_move(npc_id);
                event_type et{ npc_id, high_resolution_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
                timer_queue.push(et);
                npc->_is_active.store(true);
            }

            else
            {
                npc->_is_active.store(false);
            }

            delete ex_over;
            break;
        }

        case OP_AI_HELLO:
        {
            int npc_id = static_cast<int>(key);
            SESSION* npc = session_ptrs[npc_id];
            lock_guard<mutex> lk(npc->_ll);
            lua_State* L = npc->_L;

            lua_getglobal(L, "event_player_move");
            lua_pushnumber(L, ex_over->_ai_target_obj);
            lua_pcall(L, 1, 0, 0);

            delete ex_over;
            break;
        }
        }
    }
}

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
    clients[user_id]->send_chat_packet(my_id, mess);

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
        lock_guard<mutex> lk(npc->_s_lock);
        npc->_greet_mode = true;
        npc->_greet_moves_left = move_count;
        npc->_greet_target = player_id;
    }

    return 0;
}

void InitializeNPC()
{
    cout << "NPC initialize begin.\n";

    for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
    {
        clients[i]->x = rand() % W_WIDTH;
        clients[i]->y = rand() % W_HEIGHT;
        clients[i]->_id = i;
        sprintf_s(clients[i]->_name, "NPC%d", i);
        clients[i]->_state.store(ST_INGAME);

        clients[i]->spawn_x = clients[i]->x;
        clients[i]->spawn_y = clients[i]->y;
        clients[i]->hp = MAX_HP;

        clients[i]->is_invincible = true;
        clients[i]->invincible_end_time = high_resolution_clock::now() + INVINCIBLE_ON_RESPAWN;

        int idx = get_sector_index(clients[i]->x, clients[i]->y);

        {
            lock_guard<mutex> lk(sector_mutexes[idx]);
            sectors[idx].insert(session_ptrs[i]);
        }

        lua_State* L = luaL_newstate();
        clients[i]->_L = L;
        luaL_openlibs(L);
        luaL_loadfile(L, "npc.lua");
        lua_pcall(L, 0, 0, 0);

        lua_getglobal(L, "set_uid");
        lua_pushnumber(L, i);
        lua_pcall(L, 1, 0, 0);

        lua_register(L, "API_SendMessage", API_SendMessage);
        lua_register(L, "API_get_x", API_get_x);
        lua_register(L, "API_get_y", API_get_y);
        lua_register(L, "API_StartGreet", API_StartGreet);
    }

    cout << "NPC initialize end.\n";
}

void do_timer()
{
    while (true)
    {
        event_type et;
        auto current_time = chrono::high_resolution_clock::now();

        if (timer_queue.try_pop(et))
        {

            if (et.wakeup_time > current_time)
            {
                timer_queue.push(et);
                this_thread::sleep_for(1ms);
                continue;
            }

            switch (et.event_id)
            {
            case EV_RANDOM_MOVE:
            {
                OVER_EXP* oe = new OVER_EXP;
                oe->_comp_type = OP_NPC_MOVE;
                PostQueuedCompletionStatus(h_iocp, 1, et.obj_id, &oe->_over);

                break;
            }

            default:
                break;
            }

            continue;
        }

        this_thread::sleep_for(1ms);
    }
}

int main()
{
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);

    if (!DB_Initialize(L"2025_GameServer")) {
        fprintf(stderr, "DB 초기화 실패, 서버 종료\n");
        return -1;
    }

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

    InitializeNPC();

    h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
    g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    g_a_over._comp_type = OP_ACCEPT;

    SOCKADDR_IN cl_addr;
    int addr_size = sizeof(cl_addr);
    AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

    vector<thread> worker_threads;
    int num_threads = thread::hardware_concurrency();

    for (int i = 0; i < num_threads; ++i)
    {
        worker_threads.emplace_back(worker_thread, h_iocp);
    }

    thread timer_thread{ do_timer };
    timer_thread.join();

    for (auto& th : worker_threads)
    {
        th.join();
    }

    closesocket(g_s_socket);
    WSACleanup();
    DB_Cleanup();
}
