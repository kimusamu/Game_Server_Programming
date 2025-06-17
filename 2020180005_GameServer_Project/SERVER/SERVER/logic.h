#pragma once

#include <memory>
#include "stdafx.h"
#include "world.h"
#include "timer.h"
#include "protocol.h"
#include "session.h"

class SESSION;

void handle_respawn(shared_ptr<SESSION> s);

void handle_damage(shared_ptr<SESSION> a, shared_ptr<SESSION> b);

void handle_player_attack(int c_id);

void handle_heal(shared_ptr<SESSION> s);

void handle_monster_attack(shared_ptr<SESSION> monster, shared_ptr <SESSION> player);

void schedule_heal_event(int session_id);