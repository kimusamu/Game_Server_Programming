#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>
#include <string>
#include <cstdio>
#include <locale>
#include <cstring>

#pragma comment (lib, "WS2_32.lib")

constexpr short SERVER_PORT = 3000;
constexpr int ROWS = 8, COLS = 8;

class SESSION;
class EXP_OVER;
void CALLBACK g_send_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);
void CALLBACK g_recv_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);
void print_error_message(int);

std::unordered_map<long long, SESSION> g_users;
std::string build_all_positions_string();

class EXP_OVER
{
public:
    EXP_OVER(long long id, const char* mess) : _id(id)
    {
        ZeroMemory(&_send_over, sizeof(_send_over));

        auto packet_size = 2 + strlen(mess);

        if (packet_size > 255)
        {
            std::cout << "MESSAGE TOO LONG" << std::endl;
            exit(-1);
        }

        _send_buffer[0] = static_cast<unsigned char>(packet_size);
        _send_buffer[1] = static_cast<unsigned char>(_id);
        strcpy_s(_send_buffer + 2, sizeof(_send_buffer) - 2, mess);

        _send_wsabuf[0].buf = _send_buffer;
        _send_wsabuf[0].len = static_cast<ULONG>(packet_size);
    }

    WSAOVERLAPPED _send_over{};
    long long     _id;
    char          _send_buffer[1024]{};
    WSABUF        _send_wsabuf[1]{};
};

class SESSION
{
private:
    SOCKET       _c_socket;
    long long    _id;

    WSAOVERLAPPED _recv_over;
    char          _recv_buffer[1024];
    WSABUF        _recv_wsabuf[1];

    void do_recv() {
        DWORD recv_flag = 0;

        ZeroMemory(&_recv_over, sizeof(_recv_over));
        _recv_over.hEvent = reinterpret_cast<HANDLE>(_id);

        auto ret = WSARecv(_c_socket, _recv_wsabuf, 1, NULL, &recv_flag, &_recv_over, g_recv_callback);

        if (ret != 0)
        {
            auto err_no = WSAGetLastError();

            if (WSA_IO_PENDING != err_no)
            {
                print_error_message(err_no);
                exit(-1);
            }
        }
    }

public:
    int player_x = 0;
    int player_y = 0;

    long long get_id() const 
    { 
        return _id; 
    }

    SESSION()
    {
        std::cout << "DEFAULT SESSION CONSTRUCTION CALLED" << std::endl;
        exit(-1);
    }

    SESSION(long long session_id, SOCKET s) : _id(session_id), _c_socket(s)
    {
        _recv_wsabuf[0].len = sizeof(_recv_buffer);
        _recv_wsabuf[0].buf = _recv_buffer;
        _recv_over.hEvent = reinterpret_cast<HANDLE>(_id);

        do_recv();
    }

    ~SESSION()
    {
        closesocket(_c_socket);
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
            if (player_y < ROWS - 1)
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
            if (player_x < COLS - 1)
            {
                player_x++;
            }
        }
    }

    void recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
    {
        _recv_buffer[num_bytes] = '\0';
        std::string command(_recv_buffer);
        std::cout << "From Client[" << _id << "]: " << command << std::endl;

        update_position(command);

        std::string all_positions = build_all_positions_string();

        for (auto& u : g_users)
        {
            u.second.do_send(_id, all_positions.c_str());
        }

        do_recv();
    }

    void do_send(long long id, const char* mess)
    {
        EXP_OVER* o = new EXP_OVER(id, mess);
        DWORD size_sent = 0;

        WSASend(_c_socket, o->_send_wsabuf, 1, &size_sent, 0, &o->_send_over, g_send_callback);
    }
};

std::string build_all_positions_string()
{
    std::string result;

    for (auto& u : g_users)
    {
        auto& s = u.second; 
        result += std::to_string(s.get_id()) + " " + std::to_string(s.player_x) + " "  + std::to_string(s.player_y) + ";";
    }

    return result;
}

int main()
{
    std::wcout.imbue(std::locale("korean"));

    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

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
    long long client_id = 0;

    while (true)
    {
        SOCKET c_socket = WSAAccept(s_socket,
            reinterpret_cast<sockaddr*>(&addr),
            &addr_size,
            NULL,
            NULL);

        if (g_users.size() >= 10)
        {
            std::cout << "Only up to 10 can be accessed." << std::endl;
            closesocket(c_socket);
            continue;
        }

        g_users.try_emplace(client_id, client_id, c_socket);
        std::cout << "Client [" << client_id << "] connected." << std::endl;
        client_id++;
    }

    closesocket(s_socket);
    WSACleanup();
}

void CALLBACK g_send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
    EXP_OVER* o = reinterpret_cast<EXP_OVER*>(p_over);
    delete o;
}

void CALLBACK g_recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED p_over, DWORD flag)
{
    auto my_id = reinterpret_cast<long long>(p_over->hEvent);
    g_users[my_id].recv_callback(err, num_bytes, p_over, flag);
}

void print_error_message(int s_err)
{
    WCHAR* lpMsgBuf;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, s_err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);

    std::wcout << L" ¿¡·¯ " << lpMsgBuf << std::endl;

    while (true);
    LocalFree(lpMsgBuf);
}
