#pragma once

#include "stdafx.h"
#include "session.h"
#include "world.h"
#include "protocol.h"
#include "include/lua.hpp"

int API_get_x(lua_State* L);

int API_get_y(lua_State* L);

int API_SendMessage(lua_State* L);

int API_StartGreet(lua_State* L);