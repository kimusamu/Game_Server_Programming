#include "over_exp.h"

OVER_EXP::OVER_EXP()
{
	wsabuf.len = BUF_SIZE;
	wsabuf.buf = send_buf;
	comp_type = OP_RECV;
	ZeroMemory(&over, sizeof(over));
}

OVER_EXP::OVER_EXP(char* packet)
{
	wsabuf.len = packet[0];
	wsabuf.buf = send_buf;
	ZeroMemory(&over, sizeof(over));
	comp_type = OP_SEND;
	memcpy(send_buf, packet, packet[0]);
}