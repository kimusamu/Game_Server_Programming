// My IP : 127.0.0.1

#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <locale>

#include <GL/glew.h>
#include <GL/freeglut.h>
#include <WS2tcpip.h>

#pragma comment (lib, "WS2_32.lib")

constexpr short SERVER_PORT = 3000;
constexpr int rows = 8, cols = 8;

float tile_size = 2.0f / rows;
float player_size = tile_size * 0.6f;
int player_x = 0, player_y = 0;

SOCKET c_socket;
WSABUF recv_wsabuf[1];

WSAOVERLAPPED recv_over;
WSAOVERLAPPED g_send_over;

char recv_buffer[1024];
bool recv_ok = false;

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

	while (true);
	LocalFree(lpMsgBuf);
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	recv_buffer[num_bytes] = 0;
	std::cout << "From Server : " << recv_buffer << std::endl;

	int new_x, new_y;

	if (std::sscanf(recv_buffer, "%d %d", &new_x, &new_y) == 2)
	{
		player_x = new_x;
		player_y = new_y;
		glutPostRedisplay();
	}

	recv_ok = true;
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
	recv_wsabuf[0].len = sizeof(recv_buffer);
	recv_wsabuf[0].buf = recv_buffer;

	DWORD recv_flag = 0;

	ZeroMemory(&recv_over, sizeof(recv_over));

	WSARecv(c_socket, recv_wsabuf, 1, NULL, &recv_flag, &recv_over, recv_callback);
}

void send_command(const char* command)
{
	WSABUF wsabuf[1];
	wsabuf[0].buf = const_cast<char*>(command);
	wsabuf[0].len = static_cast<ULONG>(std::strlen(command));

	ZeroMemory(&g_send_over, sizeof(g_send_over));

	DWORD size_sent;
	int ret = WSASend(c_socket, wsabuf, 1, &size_sent, 0, &g_send_over, send_callback);

	if (ret == SOCKET_ERROR)
	{
		int err_no = WSAGetLastError();
		if (err_no != WSA_IO_PENDING)
		{
			error_display("WSASend failed: ", err_no);
		}
	}
}

void draw_scene()
{
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	for (int row = 0; row < rows; ++row)
	{
		for (int col = 0; col < cols; ++col)
		{
			if ((row + col) % 2 == 0)
			{
				glColor3f(1.0f, 1.0f, 1.0f);
			}
				
			else
			{
				glColor3f(0.0f, 0.0f, 0.0f);
			}

			float x = -1.0f + col * tile_size;
			float y = 1.0f - row * tile_size;

			glBegin(GL_QUADS);
			glVertex2f(x, y);
			glVertex2f(x + tile_size, y);
			glVertex2f(x + tile_size, y - tile_size);
			glVertex2f(x, y - tile_size);
			glEnd();
		}
	}

	glColor3f(0.5f, 0.5f, 0.5f);
	float px = -1.0f + player_x * tile_size + (tile_size - player_size) / 2;
	float py = 1.0f - player_y * tile_size - (tile_size - player_size) / 2;

	glBegin(GL_QUADS);
	glVertex2f(px, py);
	glVertex2f(px + player_size, py);
	glVertex2f(px + player_size, py - player_size);
	glVertex2f(px, py - player_size);
	glEnd();

	glutSwapBuffers();
}

void reshape(int w, int h)
{
	glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(-1.0, 1.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void special_keys(int key, int x, int y)
{
	const char* command = nullptr;

	switch (key)
	{
	case GLUT_KEY_UP:
		command = "UP";
		break;

	case GLUT_KEY_DOWN:
		command = "DOWN";
		break;

	case GLUT_KEY_LEFT:
		command = "LEFT";
		break;

	case GLUT_KEY_RIGHT:
		command = "RIGHT";
		break;

	default:
		return;
	}

	send_command(command);
}

void idle()
{
	SleepEx(0, TRUE);
}

int main(int argc, char** argv)
{
	std::wcout.imbue(std::locale("korean"));

	std::string server_ip;
	std::cout << "Enter Server IP: ";
	std::cin >> server_ip;

	WSAData WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);

	inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr);
	WSAConnect(c_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN), NULL, NULL, NULL, NULL);

	std::cout << "Success to connected server at " << server_ip << std::endl;

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowPosition(100, 100);
	glutInitWindowSize(800, 800);
	glutCreateWindow("Chess Board");

	glewExperimental = GL_TRUE;

	if (glewInit() != GLEW_OK)
	{
		std::cerr << "GLEW Initialization failed." << std::endl;
		closesocket(c_socket);
		WSACleanup();
		return -1;
	}

	glutDisplayFunc(draw_scene);
	glutReshapeFunc(reshape);
	glutSpecialFunc(special_keys);
	glutIdleFunc(idle);

	glutMainLoop();

	closesocket(c_socket);
	WSACleanup();
	return 0;
}