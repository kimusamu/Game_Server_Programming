#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <WS2tcpip.h>
#include <cstdio>
#include <string>

#pragma comment (lib, "WS2_32.lib")

constexpr short SERVER_PORT = 3000;
constexpr int rows = 8, cols = 8;

int player_x = 0, player_y = 0;

char recv_buffer[1024];
WSABUF recv_wsabuf[1];
WSAOVERLAPPED recv_over;

char send_buffer[1024];
WSABUF send_wsabuf[1];
WSAOVERLAPPED send_over;

SOCKET c_socket;

void print_error_message(int s_err)
{
	WCHAR* lpMsgBuf;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, s_err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);

	std::wcout << L" 에러 " << lpMsgBuf << std::endl;

	while (true);
	// 디버깅 용
	LocalFree(lpMsgBuf);
}

void update_position(const std::string& command)
{
	if (command == "UP")
	{
		if (player_y > 0)
		{
			player_y--;
		}
	}
	else if (command == "DOWN")
	{
		if (player_y < rows - 1)
		{
			player_y++;
		}
	}
	else if (command == "LEFT")
	{
		if (player_x > 0)
		{
			player_x--;
		}
	}
	else if (command == "RIGHT")
	{
		if (player_x < cols - 1)
		{
			player_x++;
		}
	}
}

void CALLBACK recv_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);
void CALLBACK send_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);

int main()
{
	std::wcout.imbue(std::locale("korean"));

	WSAData WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

	if (s_socket <= 0)
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

	bind(s_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN));
	listen(s_socket, SOMAXCONN);

	INT addr_size = sizeof(SOCKADDR_IN);

	c_socket = WSAAccept(
		s_socket,
		reinterpret_cast<sockaddr*>(&addr),
		&addr_size,
		NULL,
		NULL);

	send_callback(0, 0, 0, 0);

	while (true)
	{
		SleepEx(0, TRUE);
	}

	closesocket(c_socket);
	closesocket(s_socket);
	WSACleanup();
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	recv_wsabuf[0].len = sizeof(recv_buffer);
	recv_wsabuf[0].buf = recv_buffer;

	DWORD recv_flag = 0;

	ZeroMemory(&recv_over, sizeof(recv_over));

	auto ret = WSARecv(c_socket, recv_wsabuf, 1, NULL, &recv_flag, &recv_over, recv_callback);

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

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	recv_buffer[num_bytes] = 0;
	std::cout << "From Client : " << recv_buffer << std::endl;

	std::string command(recv_buffer);
	update_position(command);

	std::sprintf(send_buffer, "%d %d", player_x, player_y);

	send_wsabuf[0].buf = send_buffer;
	send_wsabuf[0].len = static_cast<ULONG>(std::strlen(send_buffer));

	ZeroMemory(&send_over, sizeof(send_over));
	DWORD size_sent = 0;

	WSASend(c_socket, send_wsabuf, 1, &size_sent, 0, &send_over, send_callback);
}