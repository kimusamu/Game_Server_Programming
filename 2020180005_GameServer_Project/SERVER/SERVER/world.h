#pragma once

#include "stdafx.h"
#include "session.h"
#include "protocol.h"

class SESSION;

extern vector<char> obstacle_map;
extern vector<pair<short, short>> obstacles;

extern vector<unordered_set<SESSION*>> sectors;
extern vector<mutex> sector_mutexes;
extern vector<SESSION*> session_ptrs;

inline int get_sector_index(int x, int y);

void update_sector(int id, int old_x, int old_y, int new_x, int new_y);

inline bool is_walkable(int x, int y)
{
    if (x < 0 || x >= W_WIDTH || y < 0 || y >= W_HEIGHT)
    {
        return false;
    }

    return !obstacle_map[y * W_WIDTH + x];
}

inline bool can_see_inline(SESSION* from, SESSION* to);

vector<int> get_neighbor_sectors(int sec_idx);

unordered_set<int> gather_visible(int viewer_id);

vector<int> gather_visible_players(int npc_id);

bool positions_equal(const SESSION* a, const SESSION* b);

void send_obstacles_to_client(int c_id);