#include "timer.h"
#include "network.h"

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
                oe->comp_type = OP_NPC_MOVE;
                PostQueuedCompletionStatus(h_iocp, 1, et.obj_id, &oe->over);

                break;
            }

            case EV_RESPAWN:
            {
                auto sess = clients[et.obj_id];

                if (sess->state.load() != ST_INGAME)
                {
                    break;
                }

                handle_respawn(sess);

                break;
            }

            case EV_HEAL:
            {
                auto sess = clients[et.obj_id];

                if (is_pc(et.obj_id) && sess->state.load() == ST_INGAME)
                {
                    handle_heal(sess);
                }

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

void periodic_save()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(30));

        for (auto& kv : clients)
        {
            int id = kv.first;
            auto& sess = kv.second;

            if (is_pc(id) && sess->state.load() == ST_INGAME && !sess->is_dummy)
            {
                wchar_t w_userid[11] = { 0 };
                int len = MultiByteToWideChar(CP_UTF8, 0, sess->name, -1, w_userid, _countof(w_userid));

                if (len > 0)
                {
                    cout << " DB SAVE ... \n";
                    DB_SavePlayerPosition(w_userid, sess->x, sess->y, sess->hp, sess->exp, sess->level, sess->potion, sess->exp_potion, sess->gold);
                }
            }
        }
    }
}