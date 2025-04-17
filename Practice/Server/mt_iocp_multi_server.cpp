#include <iostream>
#include <unordered_map>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <atomic>

#include "game_header.h"

#pragma comment (lib, "WS2_32.lib")
#pragma comment (lib, "MSWSock.lib")

constexpr short SERVER_PORT = 3000;

class EXP_OVER;
class SESSION;
extern std::unordered_map<long long, SESSION> g_users;

void print_error_message(int s_err)
{
	WCHAR* lpMsgBuf;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, s_err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);

	std::wcout << L" ERROR " << lpMsgBuf << std::endl;

	LocalFree(lpMsgBuf);
}

enum IO_OPERATION
{
	IO_RECV, IO_SEND, IO_ACCEPT
};

std::unordered_map<long long, SESSION> g_users;
HANDLE g_hIOCP;
SOCKET g_s_socket;
std::atomic<int> g_new_id = 0;

class EXP_OVER
{
public:
	EXP_OVER(IO_OPERATION op) : _io_op(op)
	{
		ZeroMemory(&_over, sizeof(_over));

		_wsabuf[0].buf = reinterpret_cast<CHAR*>(_buffer);
		_wsabuf[0].len = sizeof(_buffer);
	}

	WSAOVERLAPPED	_over;
	IO_OPERATION	_io_op;
	SOCKET			_accept_socket;
	unsigned char	_buffer[1024];
	WSABUF			_wsabuf[1];
};

EXP_OVER g_accept_over{ IO_ACCEPT };

class SESSION
{
public:
	SOCKET			_c_socket;
	long long		_id;

	EXP_OVER		_recv_over{ IO_RECV };
	unsigned char	_remained;

	short			_x, _y;
	std::string		_name;

	SESSION()
	{
		std::cout << "DEFAULT SESSION CONSTRUCTION CALLED" << std::endl;
		exit(-1);
	}

	SESSION(long long session_id, SOCKET s) : _id(session_id), _c_socket(s)
	{
		_remained = 0;
		do_recv();
	}

	~SESSION()
	{
		sc_packet_leave lp;
		lp.size = sizeof(lp);
		lp.type = S2C_P_LEAVE;
		lp.id = _id;

		for (auto& u : g_users)
		{
			if (_id != u.first)
			{
				u.second.do_send(&lp);
			}
		}

		closesocket(_c_socket);
	}

	void do_recv()
	{
		DWORD recv_flag = 0;

		ZeroMemory(&_recv_over._over, sizeof(_recv_over._over));
		_recv_over._wsabuf[0].buf = reinterpret_cast<CHAR*>(_recv_over._buffer + _remained);
		_recv_over._wsabuf[0].len = sizeof(_recv_over._buffer) - _remained;

		auto ret = WSARecv(_c_socket, _recv_over._wsabuf, 1, NULL, &recv_flag, &_recv_over._over, NULL);

		if (0 != ret)
		{
			auto err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
			{
				print_error_message(err_no);
				exit(-1);
			}
		}
	}

	void do_send(void* buff)
	{
		EXP_OVER* o = new EXP_OVER(IO_SEND);
		const unsigned char packet_size = reinterpret_cast<unsigned char*>(buff)[0];

		memcpy(o->_buffer, buff, packet_size);
		o->_wsabuf[0].len = packet_size;

		DWORD size_sent;
		WSASend(_c_socket, o->_wsabuf, 1, &size_sent, 0, &(o->_over), NULL);
	}

	void send_player_info_packet()
	{
		sc_packet_avatar_info p;
		p.size = sizeof(p);
		p.type = S2C_P_AVATAR_INFO;
		p.id = _id;
		p.x = _x;
		p.y = _y;
		p.level = 1;
		p.hp = 100;
		p.exp = 200;

		do_send(&p);
	}

	void send_player_position()
	{
		sc_packet_move p;
		p.size = sizeof(p);
		p.type = S2C_P_MOVE;
		p.id = _id;
		p.x = _x;
		p.y = _y;

		do_send(&p);
	}

	void process_packet(unsigned char* p)
	{
		const unsigned char packet_type = p[1];

		switch (packet_type)
		{
		case C2S_P_LOGIN:
		{
			cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);
			_name = packet->name;
			_x = 4;
			_y = 4;
			send_player_info_packet();

			sc_packet_enter ep;
			ep.size = sizeof(ep);
			ep.type = S2C_P_ENTER;
			ep.id = _id;
			strcpy_s(ep.name, _name.c_str());
			ep.o_type = 0;
			ep.x = _x;
			ep.y = _y;

			for (auto& u : g_users)
			{
				if (u.first != _id)
				{
					u.second.do_send(&ep);
				}
			}

			for (auto& u : g_users)
			{
				if (u.first != _id)
				{
					sc_packet_enter ep;
					ep.size = sizeof(ep);
					ep.type = S2C_P_ENTER;
					ep.id = u.first;
					strcpy_s(ep.name, u.second._name.c_str());
					ep.o_type = 0;
					ep.x = u.second._x;
					ep.y = u.second._y;

					do_send(&ep);
				}
			}

			break;
		}

		case C2S_P_MOVE:
		{
			cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);

			switch (packet->direction)
			{
			case MOVE_UP:
			{
				if (_y > 0)
				{
					_y = _y - 1;
				}

				break;
			}

			case MOVE_DOWN:
			{
				if (_y < MAP_HEIGHT - 1)
				{
					_y = _y + 1;
				}

				break;
			}

			case MOVE_LEFT:
			{
				if (_x > 0)
				{
					_x = _x - 1;
				}

				break;
			}

			case MOVE_RIGHT:
			{
				if (_x < MAP_WIDTH - 1)
				{
					_x = _x + 1;
				}

				break;
			}

			default:
			{
				break;
			}
			}

			sc_packet_move mp;
			mp.size = sizeof(mp);
			mp.type = S2C_P_MOVE;
			mp.id = _id;
			mp.x = _x;
			mp.y = _y;

			for (auto& u : g_users)
			{
				u.second.do_send(&mp);
			}

			break;
		}

		default:
		{
			std::cout << " ERROR Invalid Packet Type" << std::endl;
			exit(-1);
		}

		break;
		}
	}
};

void do_accept(SOCKET s_socket, EXP_OVER* accept_over)
{
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	accept_over->_accept_socket = c_socket;
	AcceptEx(s_socket, c_socket, accept_over->_buffer, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, NULL, &accept_over->_over);
}

void worker()
{
	while (true)
	{
		DWORD io_size;
		WSAOVERLAPPED* o;
		ULONG_PTR key;
		BOOL ret = GetQueuedCompletionStatus(g_hIOCP, &io_size, &key, &o, INFINITE);
		EXP_OVER* eo = reinterpret_cast<EXP_OVER*>(o);

		if (false == ret)
		{
			auto err_no = WSAGetLastError();
			print_error_message(err_no);

			if (g_users.count(key) != 0)
			{
				g_users.erase(key);
			}

			continue;
		}

		if (((eo->_io_op == IO_RECV) || (eo->_io_op == IO_SEND)) && (0 == io_size))
		{
			if (g_users.count(key) != 0)
			{
				g_users.erase(key);
			}

			continue;
		}

		switch (eo->_io_op)
		{
		case IO_ACCEPT:
		{
			int new_id = g_new_id++;

			CreateIoCompletionPort(reinterpret_cast<HANDLE>(eo->_accept_socket), g_hIOCP, new_id, 0);
			g_users.try_emplace(new_id, new_id, eo->_accept_socket);
			do_accept(g_s_socket, &g_accept_over);
			break;
		}

		case IO_SEND:
		{
			delete eo;
			break;
		}

		case IO_RECV:
		{
			SESSION& user = g_users[key];
			unsigned char* p = eo->_buffer;
			int data_size = io_size + user._remained;

			while (p < eo->_buffer + data_size)
			{
				unsigned char packet_size = *p;

				if (p + packet_size > eo->_buffer + data_size)
				{
					break;
				}

				user.process_packet(p);
				p = p + packet_size;
			}

			if (p < eo->_buffer + data_size)
			{
				user._remained = static_cast<unsigned char>(eo->_buffer + data_size - p);
				memcpy(p, eo->_buffer, user._remained);
			}

			else
			{
				user._remained = 0;
			}

			user.do_recv();
			break;
		}

		default:
		{
			break;
		}
		}
	}
}

int main()
{
	std::wcout.imbue(std::locale("korean"));

	WSAData WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

	if (g_s_socket <= 0)
	{
		std::cout << "ERROR" << std::endl;
	}

	else
	{
		std::cout << "Socket Created" << std::endl;
	}

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	bind(g_s_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN));
	listen(g_s_socket, SOMAXCONN);

	INT addr_size = sizeof(SOCKADDR_IN);
	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), g_hIOCP, -1, 0);

	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_hIOCP, 3, 0);


	do_accept(g_s_socket, &g_accept_over);

	auto num_core = std::thread::hardware_concurrency;
	std::vector<std::thread> workers;

	for (unsigned int i = 0; i < num_core(); ++i)
	{
		workers.emplace_back(worker);
	}

	for (auto& w : workers)
	{
		w.join();
	}

	closesocket(g_s_socket);
	WSACleanup();
}