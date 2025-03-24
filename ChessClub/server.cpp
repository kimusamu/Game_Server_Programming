#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <WS2tcpip.h>
#include <cstdio>
#include <string>

#pragma comment (lib, "WS2_32.lib")

constexpr short SERVER_PORT = 3000;
constexpr int rows = 8, cols = 8;

int player_x = 0, player_y = 0;

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

int main()
{
	std::wcout.imbue(std::locale("korean"));

	WSAData WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, 0);

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
	SOCKET c_socket = WSAAccept(
		s_socket,
		reinterpret_cast<sockaddr*>(&addr),
		&addr_size,
		NULL,
		NULL);

	while (true)
	{
		char recv_buffer[1024];
		WSABUF recv_wsabuf[1];
		recv_wsabuf[0].len = sizeof(recv_buffer);
		recv_wsabuf[0].buf = recv_buffer;

		DWORD recv_bytes;
		DWORD recv_flag = 0;

		auto ret = WSARecv(c_socket, recv_wsabuf, 1, &recv_bytes, &recv_flag, NULL, NULL);

		if (SOCKET_ERROR == ret)
		{
			std::cout << "Error at WSARecv";

			auto s_err = WSAGetLastError();

			//std::cout << s_err << std::endl;
			print_error_message(s_err);
			exit(-1);
		}

		recv_buffer[recv_bytes] = 0;
		std::cout << "From Client : " << recv_buffer << std::endl;

		std::string command(recv_buffer);
		update_position(command);
		std::cout << "Updated Position ( x : " << player_x << ", y : " << player_y << " ) " << std::endl;


		char buffer[1024];
		std::sprintf(buffer, "%d %d", player_x, player_y);

		WSABUF wsabuf[1];
		wsabuf[0].buf = buffer;
		wsabuf[0].len = static_cast<ULONG>(std::strlen(buffer));

		DWORD size_sent;

		WSASend(c_socket, wsabuf, 1, &size_sent, 0, NULL, NULL);
	}

	closesocket(c_socket);
	closesocket(s_socket);
	WSACleanup();
	return 0;
}