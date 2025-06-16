#pragma once

#include "stdafx.h"

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_AI_HELLO };

class OVER_EXP
{
public:
    WSAOVERLAPPED over;
    WSABUF wsabuf;
    char send_buf[BUF_SIZE];
    COMP_TYPE comp_type;
    int ai_target_obj;

    OVER_EXP();
    OVER_EXP(char* packet);
};
