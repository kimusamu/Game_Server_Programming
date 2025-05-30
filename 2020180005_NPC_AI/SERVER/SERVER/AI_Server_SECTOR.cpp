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
#include <concurrent_unordered_map.h>
#include <concurrent_priority_queue.h>
#include <atomic>
#include <memory>
#include <chrono>
#include <queue>
#include <algorithm>

#include "protocol.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

using namespace std;
using namespace chrono;

constexpr int VIEW_RANGE = 5;
constexpr int SECTOR_SIZE = 16;
constexpr int SECTOR_X = (W_WIDTH + SECTOR_SIZE - 1) / SECTOR_SIZE;
constexpr int SECTOR_Y = (W_HEIGHT + SECTOR_SIZE - 1) / SECTOR_SIZE;

static vector<unordered_set<int>> sectors(SECTOR_X* SECTOR_Y);
static vector<mutex> sector_mutexes(SECTOR_X* SECTOR_Y);

inline int get_sector_index(int x, int y)
{
	int sx = clamp(x / SECTOR_SIZE, 0, SECTOR_X - 1);
	int sy = clamp(y / SECTOR_SIZE, 0, SECTOR_Y - 1);

	return sy * SECTOR_X + sx;
}

void update_sector(int id, int old_x, int old_y, int new_x, int new_y)
{
	int old_idx = get_sector_index(old_x, old_y);
	int new_idx = get_sector_index(new_x, new_y);

	if (old_idx == new_idx)
	{
		return;
	}

	{
		lock_guard<mutex> lk(sector_mutexes[old_idx]);
		sectors[old_idx].erase(id);
	}

	{
		lock_guard<mutex> lk(sector_mutexes[new_idx]);
		sectors[new_idx].insert(id);
	}
}

enum EVENT_TYPE { EV_RANDOM_MOVE, EV_MOVE, EV_HEAL, EV_ATTACK };

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

concurrency::concurrent_priority_queue<event_type> timer_queue;

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_AI_HELLO };

class OVER_EXP
{
public:
	WSAOVERLAPPED _over;
	WSABUF _wsabuf;
	char _send_buf[BUF_SIZE];
	COMP_TYPE _comp_type;
	int _ai_target_obj;

	OVER_EXP()
	{
		_wsabuf.len = BUF_SIZE;
		_wsabuf.buf = _send_buf;
		_comp_type = OP_RECV;
		ZeroMemory(&_over, sizeof(_over));
	}

	OVER_EXP(char* packet)
	{
		_wsabuf.len = packet[0];
		_wsabuf.buf = _send_buf;
		ZeroMemory(&_over, sizeof(_over));
		_comp_type = OP_SEND;
		memcpy(_send_buf, packet, packet[0]);
	}
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

class SESSION
{
	OVER_EXP _recv_over;

public:
	S_STATE _state;
	SOCKET _socket;

	mutex _s_lock;
	mutex _ll;
	mutex _vl;

	unordered_set <int> _view_list;
	atomic_bool _is_active;

	int _id;
	short	x, y;
	char	_name[NAME_SIZE];
	int		_prev_remain;
	long long last_move_time;

public:
	SESSION()
	{
		_id = -1;
		_socket = 0;
		x = 0;
		y = 0;
		_name[0] = 0;
		_state = ST_FREE;
		_prev_remain = 0;
	}

	~SESSION() {}

	void do_recv()
	{
		DWORD recv_flag = 0;
		memset(&_recv_over._over, 0, sizeof(_recv_over._over));
		_recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
		_recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
		WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag, &_recv_over._over, 0);
	}

	void do_send(void* packet)
	{
		OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
		WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
	}

	void send_login_info_packet()
	{
		SC_LOGIN_INFO_PACKET p;
		p.id = _id;
		p.size = sizeof(SC_LOGIN_INFO_PACKET);
		p.type = SC_LOGIN_INFO;
		p.x = x;
		p.y = y;
		do_send(&p);
	}

	void send_move_packet(int c_id);

	void send_add_player_packet(int c_id);

	void send_chat_packet(int c_id, const char* mess);

	void send_remove_player_packet(int c_id)
	{
		_vl.lock();

		if (_view_list.count(c_id))
		{
			_view_list.erase(c_id);
		}

		else
		{
			_vl.unlock();
			return;
		}

		_vl.unlock();

		SC_REMOVE_OBJECT_PACKET p;
		p.id = c_id;
		p.size = sizeof(p);
		p.type = SC_REMOVE_OBJECT;
		do_send(&p);
	}
};

HANDLE h_iocp;
concurrency::concurrent_unordered_map<int, shared_ptr<SESSION>> clients;
SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

bool is_pc(int object_id)
{
	return object_id < MAX_USER;
}

bool is_npc(int object_id)
{
	return !is_pc(object_id);
}

bool can_see(int from, int to)
{
	if (abs(clients[from]->x - clients[to]->x) > VIEW_RANGE)
	{
		return false;
	}

	return abs(clients[from]->y - clients[to]->y) <= VIEW_RANGE;
}

static vector<int> get_neighbor_sectors(int sec_idx)
{
	int sx = sec_idx % SECTOR_X;
	int sy = sec_idx / SECTOR_X;

	vector<int> neigh;
	neigh.reserve(9);

	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			int nx = sx + dx;
			int ny = sy + dy;

			if (nx < 0 || nx >= SECTOR_X || ny < 0 || ny >= SECTOR_Y)
			{
				continue;
			}

			neigh.push_back(ny * SECTOR_X + nx);
		}
	}

	return neigh;
}

unordered_set<int> gather_visible(int viewer_id)
{
	int sec = get_sector_index(clients[viewer_id]->x, clients[viewer_id]->y);
	auto neigh = get_neighbor_sectors(sec);
	unordered_set<int> vis;

	for (int s : neigh)
	{
		lock_guard<mutex> lk(sector_mutexes[s]);

		for (int other : sectors[s])
		{
			if (other == viewer_id)
			{
				continue;
			}

			auto& peer = clients[other];

			if (peer->_state == ST_INGAME && can_see(viewer_id, other))
			{
				vis.insert(other);
			}
		}
	}

	return vis;
}


void SESSION::send_move_packet(int c_id)
{
	SC_MOVE_OBJECT_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.x = clients[c_id]->x;
	p.y = clients[c_id]->y;
	p.move_time = clients[c_id]->last_move_time;

	do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
	SC_ADD_OBJECT_PACKET add_packet;
	add_packet.id = c_id;
	strcpy_s(add_packet.name, clients[c_id]->_name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_OBJECT;
	add_packet.x = clients[c_id]->x;
	add_packet.y = clients[c_id]->y;

	_vl.lock();
	_view_list.insert(c_id);
	_vl.unlock();

	do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess)
{
	SC_CHAT_PACKET packet;
	packet.id = p_id;
	packet.size = sizeof(SC_CHAT_PACKET);
	packet.type = SC_CHAT;

	strcpy_s(packet.mess, mess);
	do_send(&packet);
}

int get_new_client_id()
{
	for (int i = 0; i < MAX_USER; ++i)
	{
		lock_guard <mutex> ll{ clients[i]->_s_lock };

		if (clients[i]->_state == ST_FREE)
		{
			return i;
		}
	}

	return -1;
}

void wake_up_npc(int npc_id, int waker)
{
	OVER_EXP* exp_over = new OVER_EXP;
	exp_over->_comp_type = OP_AI_HELLO;
	exp_over->_ai_target_obj = waker;

	PostQueuedCompletionStatus(h_iocp, 1, npc_id, &exp_over->_over);

	if (clients[npc_id]->_is_active)
	{
		return;
	}

	bool old_state = false;

	if (false == atomic_compare_exchange_strong(&clients[npc_id]->_is_active, &old_state, true))
	{
		return;
	}

	event_type et{ npc_id,  chrono::high_resolution_clock::now(), EV_RANDOM_MOVE, 0 };
	timer_queue.push(et);
}

void process_packet(int c_id, char* packet)
{
	switch (packet[1])
	{
	case CS_LOGIN:
	{
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		strcpy_s(clients[c_id]->_name, p->name);

		{
			lock_guard<mutex> ll{ clients[c_id]->_s_lock };
			clients[c_id]->x = rand() % W_WIDTH;
			clients[c_id]->y = rand() % W_HEIGHT;
			clients[c_id]->_state = ST_INGAME;
		}

		update_sector(c_id, 0, 0, clients[c_id]->x, clients[c_id]->y);

		clients[c_id]->send_login_info_packet();

		for (auto& pl : clients)
		{
			{
				lock_guard<mutex> ll(pl.second->_s_lock);

				if (ST_INGAME != pl.second->_state)
				{
					continue;
				}
			}

			if (pl.second->_id == c_id)
			{
				continue;
			}

			if (false == can_see(c_id, pl.second->_id))
			{
				continue;
			}

			if (is_pc(pl.second->_id))
			{
				pl.second->send_add_player_packet(c_id);
			}

			else
			{
				wake_up_npc(pl.second->_id, c_id);
			}

			clients[c_id]->send_add_player_packet(pl.second->_id);
		}

		break;
	}

	case CS_MOVE:
	{
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		clients[c_id]->last_move_time = p->move_time;

		short old_x = clients[c_id]->x;
		short old_y = clients[c_id]->y;
		short x = old_x;
		short y = old_y;

		switch (p->direction)
		{
		case 0:
			if (y > 0)
			{
				y--;
			}

			break;

		case 1:
			if (y < W_HEIGHT - 1)
			{
				y++;
			}

			break;

		case 2:
			if (x > 0)
			{
				x--;
			}

			break;

		case 3:
			if (x < W_WIDTH - 1)
			{
				x++;
			}

			break;
		}

		clients[c_id]->x = x;
		clients[c_id]->y = y;

		update_sector(c_id, old_x, old_y, x, y);

		unordered_set<int> near_list = gather_visible(c_id);

		for (int oid : near_list)
		{
			if (is_npc(oid))
			{
				wake_up_npc(oid, c_id);
			}
		}

		clients[c_id]->_vl.lock();
		unordered_set<int> old_vlist = clients[c_id]->_view_list;
		clients[c_id]->_vl.unlock();

		clients[c_id]->send_move_packet(c_id);

		for (int oid : near_list) {
			auto& peer = clients[oid];
			peer->_vl.lock();
			bool already = peer->_view_list.count(c_id);
			peer->_vl.unlock();

			if (already)
			{
				peer->send_move_packet(c_id);
			}

			else
			{
				peer->send_add_player_packet(c_id);
			}

			if (!old_vlist.count(oid))
			{
				clients[c_id]->send_add_player_packet(oid);
			}
		}

		for (int oid : old_vlist)
		{
			if (!near_list.count(oid))
			{
				clients[c_id]->send_remove_player_packet(oid);

				if (is_pc(oid))
				{
					clients[oid]->send_remove_player_packet(c_id);
				}
			}
		}

		break;
	}

	default:
		break;
	}
}

void disconnect(int c_id)
{
	clients[c_id]->_vl.lock();
	unordered_set <int> vl = clients[c_id]->_view_list;
	clients[c_id]->_vl.unlock();

	for (auto& p_id : vl)
	{
		if (is_npc(p_id))
		{
			continue;
		}

		auto& pl = clients[p_id];

		{
			lock_guard<mutex> ll(pl->_s_lock);
			if (ST_INGAME != pl->_state)
			{
				continue;
			}
		}

		if (pl->_id == c_id)
		{
			continue;
		}

		pl->send_remove_player_packet(c_id);
	}

	closesocket(clients[c_id]->_socket);

	lock_guard<mutex> ll(clients[c_id]->_s_lock);
	clients[c_id]->_state = ST_FREE;
}

void do_npc_random_move(int npc_id)
{
	SESSION& npc = *clients[npc_id];
	int sec_idx = get_sector_index(npc.x, npc.y);
	int sx = (sec_idx % SECTOR_X) * SECTOR_SIZE;
	int sy = (sec_idx / SECTOR_X) * SECTOR_SIZE;
	int ex = min(sx + SECTOR_SIZE - 1, W_WIDTH - 1);
	int ey = min(sy + SECTOR_SIZE - 1, W_HEIGHT - 1);
	int old_x = npc.x;
	int old_y = npc.y;
	int x = old_x;
	int y = old_y;

	switch (rand() % 4) {
	case 0:
	{
		if (x < ex)
		{
			++x;
		}

		break;
	}

	case 1:
	{
		if (x > sx)
		{
			--x;
		}

		break;
	}

	case 2:
	{
		if (y < ey)
		{
			++y;
		}

		break;
	}

	case 3:
	{
		if (y > sy)
		{
			--y;
		}

		break;
	}
	}

	npc.x = x;
	npc.y = y;

	update_sector(npc_id, old_x, old_y, x, y);

	unordered_set<int> old_vl;
	{
		lock_guard<mutex> lk(npc._vl);
		old_vl = npc._view_list;
	}


	auto new_vl = gather_visible(npc_id);

	{
		lock_guard<mutex> lk(npc._vl);
		npc._view_list = new_vl;
	}

	for (int pid : new_vl)
	{
		if (!old_vl.count(pid))
		{
			clients[pid]->send_add_player_packet(npc_id);
		}

		else
		{
			clients[pid]->send_move_packet(npc_id);
		}
	}

	for (int pid : old_vl)
	{
		if (!new_vl.count(pid))
		{
			clients[pid]->send_remove_player_packet(npc_id);
		}
	}

	long long current_time = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

	clients[npc_id]->last_move_time = current_time;
}

void worker_thread(HANDLE h_iocp)
{
	while (true)
	{
		DWORD num_bytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over = nullptr;
		BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
		OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);

		if (FALSE == ret)
		{
			if (ex_over->_comp_type == OP_ACCEPT)
			{
				cout << "Accept Error";
			}

			else
			{
				cout << "GQCS Error on client[" << key << "]\n";
				disconnect(static_cast<int>(key));

				if (ex_over->_comp_type == OP_SEND)
				{
					delete ex_over;
				}

				continue;
			}
		}

		if ((0 == num_bytes) && ((ex_over->_comp_type == OP_RECV) || (ex_over->_comp_type == OP_SEND)))
		{
			disconnect(static_cast<int>(key));

			if (ex_over->_comp_type == OP_SEND)
			{
				delete ex_over;
			}

			continue;
		}

		switch (ex_over->_comp_type)
		{
		case OP_ACCEPT:
		{
			int client_id = get_new_client_id();

			if (client_id != -1)
			{
				{
					lock_guard<mutex> ll(clients[client_id]->_s_lock);
					clients[client_id]->_state = ST_ALLOC;
				}

				clients[client_id]->x = 0;
				clients[client_id]->y = 0;
				clients[client_id]->_id = client_id;
				clients[client_id]->_name[0] = 0;
				clients[client_id]->_prev_remain = 0;
				clients[client_id]->_socket = g_c_socket;
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket), h_iocp, client_id, 0);
				clients[client_id]->do_recv();
				g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			}

			else
			{
				cout << "Max user exceeded.\n";
			}

			ZeroMemory(&g_a_over._over, sizeof(g_a_over._over));
			int addr_size = sizeof(SOCKADDR_IN);
			AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, nullptr, &g_a_over._over);
			break;
		}

		case OP_RECV:
		{
			int remain_data = num_bytes + clients[key]->_prev_remain;
			char* p = ex_over->_send_buf;

			while (remain_data > 0)
			{
				int packet_size = p[0];

				if (packet_size <= remain_data)
				{
					process_packet(static_cast<int>(key), p);
					p = p + packet_size;
					remain_data = remain_data - packet_size;
				}

				else
				{
					break;
				}
			}

			clients[key]->_prev_remain = remain_data;

			if (remain_data > 0)
			{
				memcpy(ex_over->_send_buf, p, remain_data);
			}

			clients[key]->do_recv();
			break;
		}

		case OP_SEND:
		{
			delete ex_over;
			break;
		}

		case OP_NPC_MOVE:
		{
			int npc_id = static_cast<int>(key);
			auto& npc = clients[npc_id];

			std::lock_guard<std::mutex> lk(npc->_s_lock);

			auto visible_players = gather_visible(npc_id);

			if (!visible_players.empty())
			{
				do_npc_random_move(npc_id);

				event_type et{ npc_id, std::chrono::high_resolution_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
				timer_queue.push(et);

				npc->_is_active = true;
			}

			else
			{
				npc->_is_active = false;
			}

			delete ex_over;
			break;
		}

		}
	}
}

void InitializeNPC()
{
	using namespace chrono;

	cout << "NPC intialize begin.\n";

	for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i)
	{
		clients[i]->x = rand() % W_WIDTH;
		clients[i]->y = rand() % W_HEIGHT;
		clients[i]->_id = i;
		sprintf_s(clients[i]->_name, "NPC%d", i);
		clients[i]->_state = ST_INGAME;

		int idx = get_sector_index(clients[i]->x, clients[i]->y);
		lock_guard<mutex> lk(sector_mutexes[idx]);
		sectors[idx].insert(i);
	}

	cout << "NPC initialize end.\n";
}

void do_timer()
{
	while (true)
	{
		event_type et;
		auto current_time = chrono::high_resolution_clock::now();

		if (true == timer_queue.try_pop(et))
		{
			if (et.wakeup_time > current_time)
			{
				timer_queue.push(et);
				this_thread::sleep_for(1ms);
				continue;
			}

			switch (et.event_id)
			{
			case EV_RANDOM_MOVE:
				OVER_EXP* oe = new OVER_EXP;
				oe->_comp_type = OP_NPC_MOVE;

				PostQueuedCompletionStatus(h_iocp, 1, et.obj_id, &oe->_over);
				break;
			}

			continue;
		}

		this_thread::sleep_for(1ms);
	}
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;

	bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_s_socket, SOMAXCONN);

	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);

	for (int i = 0; i < MAX_USER + MAX_NPC; ++i)
	{
		clients.insert({ i, std::make_shared<SESSION>() });
	}

	InitializeNPC();

	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
	g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_a_over._comp_type = OP_ACCEPT;
	AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

	vector <thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();

	for (int i = 0; i < num_threads; ++i)
	{
		worker_threads.emplace_back(worker_thread, h_iocp);
	}

	thread timer_thread{ do_timer };
	timer_thread.join();

	for (auto& th : worker_threads)
	{
		th.join();
	}

	closesocket(g_s_socket);
	WSACleanup();
}