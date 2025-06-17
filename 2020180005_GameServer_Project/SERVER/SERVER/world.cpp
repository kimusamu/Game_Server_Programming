#include "world.h"

vector<char> obstacle_map(W_WIDTH* W_HEIGHT, false);
vector<pair<short, short>> obstacles;

vector<unordered_set<SESSION*>> sectors(TOTAL_SECTORS);
vector<mutex> sector_mutexes(TOTAL_SECTORS);
vector<SESSION*> session_ptrs;

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

vector<int> get_neighbor_sectors(int sec_idx)
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

    vector<SESSION*> candidates;
    candidates.reserve(64);

    for (int s : neigh)
    {
        lock_guard<mutex> lk(sector_mutexes[s]);

        for (auto sess : sectors[s])
        {
            candidates.push_back(sess);
        }
    }

    unordered_set<int> vis;
    vis.reserve(candidates.size());

    for (auto peer : candidates)
    {
        int other_id = peer->id;

        if (other_id == viewer_id)
        {
            continue;
        }

        if (peer->state.load() != ST_INGAME)
        {
            continue;
        }

        if (peer->hp <= 0)
        {
            continue;
        }

        if (!can_see_inline(viewer, peer))
        {
            continue;
        }

        vis.insert(other_id);
    }

    return vis;
}

vector<int> gather_visible_players(int npc_id)
{
    auto all = gather_visible(npc_id);

    vector<int> players;
    players.reserve(all.size());

    for (int id : all)
    {
        if (is_pc(id)) players.push_back(id);
    }

    return players;
}

bool positions_equal(const SESSION* a, const SESSION* b)
{
    return (a->x == b->x && a->y == b->y);
}

void send_obstacles_to_client(int c_id)
{
    auto& sess = clients[c_id];
    int sec = get_sector_index(sess->x, sess->y);
    auto neigh_secs = get_neighbor_sectors(sec);

    for (int s : neigh_secs)
    {
        int sx = (s % SECTOR_X) * SECTOR_SIZE;
        int sy = (s / SECTOR_X) * SECTOR_SIZE;
        int ex = min(sx + SECTOR_SIZE, W_WIDTH);
        int ey = min(sy + SECTOR_SIZE, W_HEIGHT);

        for (int y = sy; y < ey; ++y)
        {
            for (int x = sx; x < ex; ++x)
            {
                if (obstacle_map[y * W_WIDTH + x]) 
                {
                    SC_OBSTACLE_PACKET pkt;
                    pkt.size = sizeof(pkt);
                    pkt.type = SC_OBSTACLE;
                    pkt.x = static_cast<short>(x);
                    pkt.y = static_cast<short>(y);

                    sess->do_send(&pkt);
                }
            }
        }
    }
}