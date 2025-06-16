#pragma once

#include "stdafx.h"
#include "world.h"
#include "script.h"
#include "protocol.h"

struct Node
{
    short x, y;
    int g, f;
    Node* parent;
};

class NodePool
{
    vector<Node> pool;
    size_t idx;

public:
    NodePool(size_t max_nodes) : pool(max_nodes), idx(0) {}

    void reset();

    Node* alloc(short x, short y, int g, int f, Node* parent);
};

bool astar(int sx, int sy, int tx, int ty, vector<pair<short, short>>& out_path);

void wake_up_npc(int npc_id, int waker);

void do_npc_random_move(int npc_id);

void do_npc_seek_move(int npc_id);

void initialize_npc();