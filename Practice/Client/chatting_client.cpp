#include <iostream>
#include <WS2tcpip.h>

#pragma comment (lib, "WS2_32.lib")

constexpr short SERVER_PORT = 3000;
constexpr char SERVER_ADDR[] = "127.0.0.1";

SOCKET c_socket;
WSABUF recv_wsabuf[1];

WSAOVERLAPPED recv_over;

char recv_buffer[1024];
bool recv_ok = false;
bool b_logout = false;

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);

	std::cout << msg;
	std::wcout << L" ¿¡·¯ " << lpMsgBuf << std::endl;

	LocalFree(lpMsgBuf);
	exit(-1);
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	if ((0 != err) || (0 == num_bytes))
	{
		b_logout = true;
		return;
	}

	char* p = recv_buffer;

	while (p < recv_buffer + num_bytes)
	{
		unsigned char packet_size = p[0];
		int user_id = p[1];
		char buff[1024];

		memcpy(buff, p + 2, packet_size - 2);
		buff[packet_size - 2] = 0;

		std::cout << "[User " << user_id << "] : " << buff << std::endl;

		p = p + packet_size;
	}

	recv_ok = true;
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	if ((0 != err) || (0 == num_bytes))
	{
		b_logout = true;
		return;
	}

	recv_wsabuf[0].len = sizeof(recv_buffer);
	recv_wsabuf[0].buf = recv_buffer;

	DWORD recv_flag = 0;

	ZeroMemory(&recv_over, sizeof(recv_over));

	WSARecv(c_socket, recv_wsabuf, 1, NULL, &recv_flag, &recv_over, recv_callback);
}

int main()
{
	std::wcout.imbue(std::locale("korean"));

	WSAData WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);

	inet_pton(AF_INET, SERVER_ADDR, &addr.sin_addr);
	WSAConnect(c_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN), NULL, NULL, NULL, NULL);

	while (false == b_logout)
	{
		char buffer[1024];

		std::cout << "INPUT : ";
		std::cin.getline(buffer, sizeof(buffer));

		WSABUF wsabuf[1];
		wsabuf[0].buf = buffer;
		wsabuf[0].len = static_cast<ULONG>(strlen(buffer));

		DWORD size_sent;
		WSAOVERLAPPED send_over;
		ZeroMemory(&send_over, sizeof(send_over));

		int ret = WSASend(c_socket, wsabuf, 1, &size_sent, 0, &send_over, send_callback);

		if (SOCKET_ERROR == ret)
		{
			auto err_no = WSAGetLastError();
			error_display("WSASEND : ", err_no);
		}

		while (false == recv_ok)
		{
			SleepEx(0, TRUE);
		}

		recv_ok = false;
	}

	closesocket(c_socket);
	WSACleanup();
}