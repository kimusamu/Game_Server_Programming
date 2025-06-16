#pragma once

constexpr int PORT_NUM = 4000;

constexpr int BUF_SIZE = 200;
constexpr int NAME_SIZE = 20;
constexpr int CHAT_SIZE = 100;

constexpr int MAX_USER = 10000;
constexpr int MAX_NPC = 200000;

constexpr int W_WIDTH = 2000;
constexpr int W_HEIGHT = 2000;

constexpr int SECTOR_SIZE = 16;
constexpr int SECTOR_X = (W_WIDTH + SECTOR_SIZE - 1) / SECTOR_SIZE;
constexpr int SECTOR_Y = (W_HEIGHT + SECTOR_SIZE - 1) / SECTOR_SIZE;

constexpr char CS_LOGIN = 0;
constexpr char CS_MOVE = 1;
constexpr char CS_CHAT = 2;
constexpr char CS_ATTACK = 3;
constexpr char CS_TELEPORT = 4;
constexpr char CS_LOGOUT = 5;
constexpr char CS_USE_POTION = 6;

constexpr char SC_LOGIN_INFO = 2;
constexpr char SC_ADD_OBJECT = 3;
constexpr char SC_REMOVE_OBJECT = 4;
constexpr char SC_MOVE_OBJECT = 5;
constexpr char SC_CHAT = 6;
constexpr char SC_LOGIN_OK = 7;
constexpr char SC_LOGIN_FAIL = 8;
constexpr char SC_STAT_CHANGE = 9;
constexpr char SC_OBSTACLE = 10; 
constexpr char SC_REMOVE_OBSTACLE_SECTOR = 11;

#pragma pack (push, 1)

struct CS_LOGIN_PACKET {
	unsigned char size;
	char	type;
	char	name[NAME_SIZE];
};

struct CS_MOVE_PACKET {
	unsigned char size;
	char	type;
	char	direction;
	unsigned	move_time;
};

struct CS_CHAT_PACKET {
	unsigned char size;
	char	type;
	char	mess[CHAT_SIZE];
};

struct CS_ATTACK_PACKET {
	unsigned char size;
	char          type;
	char          direction;
};

struct CS_TELEPORT_PACKET {
	unsigned char size;
	char	type;
};

struct CS_LOGOUT_PACKET {
	unsigned char size;
	char	type;
};

struct CS_USE_POTION_PACKET {
	unsigned char size;
	char type;
	char potion_type;
};

struct SC_LOGIN_INFO_PACKET {
	unsigned char size;
	char	type;
	int		id;
	int		hp;
	int		max_hp;
	int		exp;
	int		level;
	int     potion;
	int     exp_potion;
	int     gold;
	short	x, y;
};

struct SC_ADD_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int		id;
	short	x, y;
	char	name[NAME_SIZE];
};

struct SC_REMOVE_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int		id;
};

struct SC_MOVE_OBJECT_PACKET {
	unsigned char size;
	char	type;
	int		id;
	short	x, y;
	unsigned int move_time;
};

struct SC_CHAT_PACKET {
	unsigned char size;
	char	type;
	int		id;
	char	name[NAME_SIZE];
	char	mess[CHAT_SIZE];
};

struct SC_LOGIN_OK_PACKET {
	unsigned char size;
	char	type;
};

struct SC_LOGIN_FAIL_PACKET {
	unsigned char size;
	char	type;

};

struct SC_STAT_CHANGEL_PACKET {
	unsigned char size;
	char	type;
	int     id;
	int		hp;
	int		max_hp;
	int		exp;
	int		level;
	int     potion;
	int     exp_potion;
	int     gold;
};

struct SC_OBSTACLE_PACKET {
	unsigned char size;
	char type;
	short x, y;
};

struct SC_REMOVE_OBSTACLE_SECTOR_PACKET {
	unsigned char size;
	char          type;
	short         sector_idx;
};

#pragma pack (pop)