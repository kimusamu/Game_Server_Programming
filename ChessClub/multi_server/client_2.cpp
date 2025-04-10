#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <locale>
#include <unordered_map>

#include <gl/glew.h>
#include <gl/freeglut.h>
#include <gl/freeglut_ext.h>

#include <WS2tcpip.h>

#pragma comment (lib, "WS2_32.lib")

constexpr short SERVER_PORT = 3000;
constexpr int rows = 8, cols = 8;

float tile_size = 2.0f / rows;
float player_size = tile_size * 0.6f;

std::unordered_map<int, std::pair<int, int>> g_positions;

SOCKET c_socket;
WSABUF recv_wsabuf[1];

WSAOVERLAPPED recv_over;
WSAOVERLAPPED g_send_over;

char recv_buffer[1024];
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

std::vector<std::string> split_by_semicolon(const std::string& s)
{
    std::vector<std::string> tokens;
    size_t start = 0;

    while (true)
    {
        auto pos = s.find(';', start);

        if (pos == std::string::npos)
        {
            if (start < s.size())
            {
                tokens.push_back(s.substr(start));
            }
                
            break;
        }

        tokens.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }

    return tokens;
}

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
    if ((0 != err) || (0 == num_bytes))
    {
        b_logout = true;
        return;
    }

    char* p = recv_buffer;
    char* end = recv_buffer + num_bytes;

    while (p < end)
    {
        if (end - p < 2)
        {
            break;
        }

        unsigned char packet_size = (unsigned char)p[0];
        int user_id = (unsigned char)p[1];

        if (end - p < packet_size)
        {
            break;
        }

        char buff[1024] = { 0 };
        memcpy(buff, p + 2, packet_size - 2);
        buff[packet_size - 2] = '\0';

        std::cout << "[From User " << user_id << "] " << buff << std::endl;

        std::string data(buff);
        auto tokens = split_by_semicolon(data);

        for (auto& t : tokens)
        {
            if (t.empty())
            {
                continue;
            }

            int id, x, y;

            if (std::sscanf(t.c_str(), "%d %d %d", &id, &x, &y) == 3)
            {
                g_positions[id] = { x, y };
            }
        }

        p += packet_size;
    }

    glutPostRedisplay();

    recv_wsabuf[0].len = sizeof(recv_buffer);
    recv_wsabuf[0].buf = recv_buffer;
    DWORD recv_flag = 0;
    ZeroMemory(&recv_over, sizeof(recv_over));

    WSARecv(c_socket, recv_wsabuf, 1, NULL, &recv_flag, &recv_over, recv_callback);
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

void send_command(const char* command)
{
    WSABUF wsabuf[1];
    wsabuf[0].buf = const_cast<char*>(command);
    wsabuf[0].len = static_cast<ULONG>(std::strlen(command));

    ZeroMemory(&g_send_over, sizeof(g_send_over));

    DWORD size_sent = 0;
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

    for (auto& u : g_positions)
    {
        int user_id = u.first;
        int px_idx = u.second.first;
        int py_idx = u.second.second;

        switch (user_id % 10)
        {
        case 0: 
        {
            glColor3f(1.0f, 0.0f, 0.0f);
        }
        break;

        case 1: 
        {
            glColor3f(0.0f, 1.0f, 0.0f);
        }
        break;

        case 2: 
        {
            glColor3f(0.0f, 0.0f, 1.0f);
        }
        break;

        case 3: 
        {
            glColor3f(0.5f, 0.5f, 0.5f);
        }
        break;

        case 4: 
        {
            glColor3f(1.0f, 1.0f, 0.0f);
        }
        break;

        case 5: 
        {
            glColor3f(0.0f, 1.0f, 1.0f);
        }
        break;

        case 6: 
        {
            glColor3f(1.0f, 0.0f, 1.0f);
        }
        break;

        case 7: 
        {
            glColor3f(0.5f, 0.5f, 0.0f);
        }
        break;

        case 8: 
        {
            glColor3f(0.0f, 0.5f, 1.0f);
        }
        break;

        case 9: 
        {
            glColor3f(0.0f, 0.5f, 0.5f);
        }
        break;

        }

        float x = -1.0f + px_idx * tile_size + (tile_size - player_size) / 2.0f;
        float y = 1.0f - py_idx * tile_size - (tile_size - player_size) / 2.0f;

        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + player_size, y);
        glVertex2f(x + player_size, y - player_size);
        glVertex2f(x, y - player_size);
        glEnd();
    }

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
    {
        command = "UP";
    }
    break;

    case GLUT_KEY_DOWN:
    {
        command = "DOWN";
    }
    break;

    case GLUT_KEY_LEFT:
    {
        command = "LEFT";
    }
    break;

    case GLUT_KEY_RIGHT:
    {
        command = "RIGHT";
    }
    break;

    default:
    {
        return;
    }
    }

    send_command(command);
}

void idle()
{
    SleepEx(0, TRUE);

    if (b_logout)
    {
        std::cout << "Disconnected from server." << std::endl;
        glutLeaveMainLoop();
    }
}

int main(int argc, char** argv)
{
    std::wcout.imbue(std::locale("korean"));

    std::string server_ip;
    std::cout << "Enter Server IP: ";
    std::cin >> server_ip;

    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

    SOCKADDR_IN addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);

    inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr);
    WSAConnect(c_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN), NULL, NULL, NULL, NULL);

    std::cout << "Success to connect server at " << server_ip << std::endl;

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
