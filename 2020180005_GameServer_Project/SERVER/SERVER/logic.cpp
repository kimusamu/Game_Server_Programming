#include "logic.h"

void handle_respawn(shared_ptr<SESSION> s)
{
    int id = s->id;
    const char* type_str = is_pc(id) ? "Player" : "NPC";

    cout << " [" << type_str << " " << id << "] RESPAWN AT (" << s->spawn_x << ", " << s->spawn_y << ").\n";

    {
        std::lock_guard<std::mutex> lk(s->vl);
        s->view_list.clear();
    }

    int spawn_idx = get_sector_index(s->spawn_x, s->spawn_y);

    {
        std::lock_guard<std::mutex> lk(sector_mutexes[spawn_idx]);
        sectors[spawn_idx].insert(session_ptrs[id]);
    }

    s->x = s->spawn_x;
    s->y = s->spawn_y;
    s->hp = BASIC_HP;
    s->exp = 0;
    s->is_invincible = true;
    s->invincible_end_time = high_resolution_clock::now() + INVINCIBLE_ON_RESPAWN;

    s->send_stat_change_packet();

    auto vis = gather_visible(id);

    for (int peer_id : vis)
    {
        clients[peer_id]->send_add_player_packet(id);
        clients[peer_id]->send_move_packet(id);
    }

    for (int peer_id : vis)
    {
        s->send_add_player_packet(peer_id);
        s->send_move_packet(peer_id);
    }

    s->send_add_player_packet(id);
    s->send_move_packet(id);

    if (is_npc(id))
    {
        event_type et{ id, high_resolution_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
        timer_queue.push(et);

        s->is_active.store(true);
    }
}

void handle_damage(shared_ptr<SESSION> a, shared_ptr<SESSION> b)
{
    auto now = high_resolution_clock::now();

    if (a->is_dummy || b->is_dummy)
    {
        return;
    }

    if (a->is_invincible && now < a->invincible_end_time || a->hp <= 0)
    {
        return;
    }

    if (b->is_invincible && now < b->invincible_end_time || b->hp <= 0)
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

    auto schedule_heal = [&](shared_ptr<SESSION> p)
    {
        int max_hp = 3 * p->level;

        if (p->hp > 0 && p->hp < max_hp)
        {
            event_type et{ p->id, chrono::high_resolution_clock::now() + 10s, EV_HEAL, 0 };
            timer_queue.push(et);
        }
    };

    schedule_heal(a);
    schedule_heal(b);

    const char* type_a = is_pc(a->id) ? "Player" : "NPC";
    const char* type_b = is_pc(b->id) ? "Player" : "NPC";

    cout << " [" << type_a << " " << a->id << "] HIT. HP: " << a->hp << "\n";
    cout << " [" << type_b << " " << b->id << "] HIT. HP: " << b->hp << "\n";

    if (a->hp <= 0)
    {
        cout << " [" << type_a << " " << a->name << "] DIED... RESPAWN TO 10 SECONDS ... \n";

        int old_idx = get_sector_index(a->x, a->y);

        {
            std::lock_guard<std::mutex> lk(sector_mutexes[old_idx]);
            sectors[old_idx].erase(session_ptrs[a->id]);
        }

        {
            std::lock_guard<std::mutex> lk(a->vl);
            a->view_list.clear();
        }

        SC_REMOVE_OBJECT_PACKET rem{ };
        rem.type = SC_REMOVE_OBJECT;
        rem.size = sizeof(rem);
        rem.id = a->id;

        for (auto& [pid, peer] : clients)
        {
            if (peer->state.load() == ST_INGAME)
                peer->do_send(&rem);
        }

        event_type et{ a->id, high_resolution_clock::now() + 10s, EV_RESPAWN, 0 };
        timer_queue.push(et);

        if (!is_pc(a->id))
        {
            a->is_active.store(false);
        }
    }

    if (b->hp <= 0)
    {
        cout << " [" << type_b << " " << b->name << "] DIED... RESPAWN TO 10 SECONDS ... \n";

        int old_idx = get_sector_index(b->x, b->y);

        {
            std::lock_guard<std::mutex> lk(sector_mutexes[old_idx]);
            sectors[old_idx].erase(session_ptrs[b->id]);
        }

        {
            std::lock_guard<std::mutex> lk(b->vl);
            b->view_list.clear();
        }

        SC_REMOVE_OBJECT_PACKET rem{ };
        rem.type = SC_REMOVE_OBJECT;
        rem.size = sizeof(rem);
        rem.id = b->id;

        for (auto& [pid, peer] : clients)
        {
            if (peer->state.load() == ST_INGAME)
            {
                peer->do_send(&rem);
            }

        }

        event_type et{ b->id, high_resolution_clock::now() + 10s, EV_RESPAWN, 0 };
        timer_queue.push(et);

        if (!is_pc(b->id))
        {
            b->is_active.store(false);
        }
    }
}

void handle_player_attack(int c_id)
{
    auto& attacker = clients[c_id];
    short px = attacker->x;
    short py = attacker->y;
    int player_sec = get_sector_index(px, py);
    auto neigh_secs = get_neighbor_sectors(player_sec);

    for (int sec_idx : neigh_secs)
    {
        std::lock_guard<std::mutex> lk(sector_mutexes[sec_idx]);

        for (SESSION* sess : sectors[sec_idx])
        {
            int other_id = sess->id;

            if (!is_npc(other_id) || sess->hp <= 0)
            {
                continue;
            }

            int dx = abs(sess->x - px);
            int dy = abs(sess->y - py);

            if (dx <= 1 && dy <= 1 && !(dx == 0 && dy == 0))
            {
                sess->hp -= 1;

                if (sess->hp > 0)
                {
                    std::cout << sess->id << "'S HP : " << sess->hp << '\n';
                    sess->send_stat_change_packet();
                }

                else
                {
                    std::cout << sess->id << " IS DEAD. " << '\n';

                    int old_idx = get_sector_index(sess->x, sess->y);
                    sectors[old_idx].erase(sess);

                    SC_REMOVE_OBJECT_PACKET rem;
                    rem.type = SC_REMOVE_OBJECT;
                    rem.size = sizeof(rem);
                    rem.id = other_id;

                    for (auto& kv : clients)
                    {
                        auto& viewer = kv.second;

                        if (viewer->state.load() == ST_INGAME)
                        {
                            viewer->do_send(&rem);
                        }
                    }

                    event_type et{ other_id, high_resolution_clock::now() + 10s, EV_RESPAWN, 0 };
                    timer_queue.push(et);

                    attacker->exp += 1;

                    {
                        std::uniform_int_distribution<> potion_dist(0, 1);

                        if (potion_dist(gen) == 0)
                        {
                            attacker->potion += 1;
                            cout << " DROPPED : POTION \n";
                        }

                        else
                        {
                            attacker->exp_potion += 1;
                            cout << " DROPPED : EXP POTION \n";
                        }
                    }

                    {
                        std::uniform_int_distribution<> gold_dist(1, 10);
                        int gold_drop = gold_dist(gen);
                        attacker->gold += gold_drop;
                        cout << " DROPPED GOLD : " << gold_drop << "\n";
                    }

                    int need = attacker->level * 10;

                    if (attacker->exp >= need)
                    {
                        cout << attacker->name << " LEVEL UP ! \n";
                        attacker->level += 1;
                        attacker->exp = 0;

                        attacker->hp = min(attacker->hp, 3 * attacker->level);

                        if (attacker->hp < 3 * attacker->level)
                        {
                            event_type et{ attacker->id, high_resolution_clock::now() + 10s, EV_HEAL, 0 };
                            timer_queue.push(et);
                        }
                    }

                    attacker->send_stat_change_packet();
                }
            }
        }
    }
}

void handle_heal(shared_ptr<SESSION> s)
{
    int id = s->id;
    int max_hp = 3 * s->level;

    if (s->hp >= max_hp)
    {
        return;
    }

    s->hp += 1;

    if (s->hp > max_hp)
    {
        s->hp = max_hp;
    }

    s->send_stat_change_packet();

    if (s->hp < max_hp)
    {
        cout << s->name << " HEAL UP \n";

        event_type et{ id, high_resolution_clock::now() + 10s, EV_HEAL, 0 };
        timer_queue.push(et);
    }
}