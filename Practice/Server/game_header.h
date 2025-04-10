#pragma once

constexpr short GAME_PORT = 3000;
constexpr int BUF_SIZE = 1024;

constexpr char S2C_P_AVATAR_INFO = 1;
constexpr char S2C_P_MOVE = 2;
constexpr char S2C_P_ENTER = 3;
constexpr char S2C_P_LEAVE = 4;
constexpr char C2S_P_LOGIN = 5;
constexpr char C2S_P_MOVE = 6;

constexpr char MAX_ID_LEN = 20;

constexpr char MOVE_UP = 1;
constexpr char MOVE_DOWN = 2;
constexpr char MOVE_LEFT = 3;
constexpr char MOVE_RIGHT = 4;

constexpr unsigned short MAP_HEIGHT = 8;
constexpr unsigned short MAP_WIDTH = 8;

#pragma pack (push, 1)
// 메모리에 변수들을 저장할 때 중간 중간 빈칸을 만들지 말고 채워라
// 채우고 있던 방식을 저장해라
// 1byte 단위로 압축해라
// int가 4의 배수가 아니면 안돌아가는 프로그램도 있으므로, byte 단위는 함부로 설정 X

struct sc_packet_avatar_info
{
	unsigned char	size;
	char			type;
	long long		id;
	short			x, y;
	short			hp;
	short			level;
	int				exp;
};

struct sc_packet_move 
{
	unsigned char	size;
	char			type;
	long long		id;
	short			x, y;
};

struct sc_packet_enter 
{
	unsigned char	size;
	char			type;
	long long		id;
	char			name[MAX_ID_LEN];
	char			o_type;
	short			x, y;
};

struct sc_packet_leave 
{
	unsigned char	size;
	char			type;
	long long		id;
};

struct cs_packet_login 
{
	unsigned char	size;
	char			type;
	char			name[MAX_ID_LEN];
};

struct cs_packet_move 
{
	unsigned char	size;
	char			type;
	char			direction;
};

#pragma pack(pop)
// 원상 복귀