#pragma once

#include "stdafx.h"
#include "timer.h"
#include "world.h"
#include "protocol.h"
#include "ai.h"

extern HANDLE h_iocp;
extern SOCKET g_s_socket;
extern SOCKET g_c_socket;
extern OVER_EXP g_a_over;

void disconnect(int c_id);

void process_packet(int c_id, char* packet);

void worker_thread(HANDLE h_iocp);