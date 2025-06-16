#pragma once

#define NOMINMAX

#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <unordered_map>
#include <concurrent_unordered_map.h>
#include <concurrent_priority_queue.h>
#include <atomic>
#include <memory>
#include <chrono>
#include <queue>
#include <algorithm>
#include <random>
#include <cstring>
#include <sqlext.h>

#include "protocol.h"

using namespace std;
using namespace chrono;

constexpr int VIEW_RANGE = 5;
constexpr int BASIC_HP = 3;
constexpr int OBSTACLE_COUNT = 100000;
constexpr auto INVINCIBLE_ON_HIT = 3s;
constexpr auto INVINCIBLE_ON_RESPAWN = 10s;
constexpr int TOTAL_SECTORS = SECTOR_X * SECTOR_Y;

inline random_device rd;
inline mt19937 gen(rd());