#pragma once

#include "stdafx.h"
#include "over_exp.h"
#include "protocol.h"
#include "include/lua.hpp"

enum class MoveBehavior { RANDOM, SEEK };
enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

class SESSION
{
    OVER_EXP _recv_over;

public:
    SOCKET socket;
    MoveBehavior behavior;
    lua_State* L;

    atomic<S_STATE> state;
    atomic<bool> greet_mode{ false };
    atomic<bool> heal;
    atomic<int> greet_moves_left{ 0 };
    atomic_bool is_active;

    mutex s_lock;
    mutex ll;
    mutex vl;

    unordered_set<int> view_list;
    vector<pair<short, short>> path;

    size_t path_idx = 0;

    high_resolution_clock::time_point invincible_end_time;

    short x;
    short y;
    short spawn_x;
    short spawn_y;

    int id;
    int prev_remain;
    int hp;
    int exp;
    int level;
    int potion;
    int exp_potion;
    int gold;
    int greet_target = -1;
    int ref_event_player_move = LUA_NOREF;

    char name[NAME_SIZE];

    bool is_invincible;
    bool is_dummy = false;

    long long last_move_time;

public:
    SESSION();

    ~SESSION();

    void do_recv();

    void do_send(void* packet);

    void send_login_info_packet();

    void send_move_packet(int c_id);

    void send_stat_change_packet();

    void send_add_player_packet(int c_id);

    void send_chat_packet(int c_id, const char* mess);

    void send_remove_player_packet(int c_id);

    void send_remove_obstacle_sector(int c_id, uint16_t sector_idx);
};

extern vector<SESSION*> session_ptrs;
extern concurrency::concurrent_unordered_map<int, shared_ptr<SESSION>> clients;

int get_new_client_id();

bool is_pc(int object_id);

bool is_npc(int object_id);

string ansi_to_utf8(const char* ansi);