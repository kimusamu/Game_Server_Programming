#pragma once

#include "stdafx.h"
#include "session.h"
#include "logic.h"
#include "db.h"
#include "protocol.h"

enum EVENT_TYPE { EV_RANDOM_MOVE, EV_MOVE, EV_HEAL, EV_RESPAWN };

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

extern concurrency::concurrent_priority_queue<event_type> timer_queue;

void do_timer();

void periodic_save();