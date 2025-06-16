#pragma once

#include "stdafx.h"
#include "protocol.h"

extern SQLHENV g_hEnv;
extern SQLHDBC g_hDbc;
extern mutex db_mutex;

void print_odbc_Error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE retcode);

bool DB_Initialize(const wchar_t* dsn_name);

void DB_Cleanup();

bool DB_LoadPlayerPosition(const wchar_t* user_id, int& out_x, int& out_y, int& out_hp, int& out_exp, int& out_level, int& out_potion, int& out_exppotion, int& out_gold);

bool DB_InsertPlayerPosition(const wchar_t* user_id, short x, short y, int hp = BASIC_HP, int exp = 0, int level = 1, int potion = 0, int exp_potion = 0, int gold = 0);

bool DB_SavePlayerPosition(const wchar_t* user_id, int x, int y, int hp, int exp, int level, int potion, int exp_potion, int gold);