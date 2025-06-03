#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
using namespace std;

#include "..\\..\\SERVER\\SERVER\\protocol.h"  // SC_LOGIN_FAIL, SC_LOGIN_INFO, 등

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 16;
constexpr auto SCREEN_HEIGHT = 16;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = SCREEN_WIDTH * TILE_WIDTH;   // size of window
constexpr auto WINDOW_HEIGHT = SCREEN_HEIGHT * TILE_WIDTH;

int g_left_x;
int g_top_y;
int g_myid;

sf::RenderWindow* g_window;
sf::Font g_font;

//------------------------------------------------------------------------------
// 객체(플레이어, NPC 등)를 그릴 때 사용하는 클래스
class OBJECT {
private:
    bool m_showing;
    sf::Sprite m_sprite;

    sf::Text m_name;
    sf::Text m_chat;
    chrono::system_clock::time_point m_mess_end_time;
public:
    int id;
    int m_x, m_y;
    int m_hp;
    char name[NAME_SIZE];

    OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
        m_showing = false;
        m_sprite.setTexture(t);
        m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
        set_name("NONAME");
        m_mess_end_time = chrono::system_clock::now();
        m_hp = 0;
    }

    OBJECT() {
        m_showing = false;
        m_hp = 0;
    }

    void show() { m_showing = true; }
    void hide() { m_showing = false; }

    void a_move(int x, int y) {
        m_sprite.setPosition((float)x, (float)y);
    }
    void a_draw() {
        g_window->draw(m_sprite);
    }

    void move(int x, int y) {
        m_x = x;
        m_y = y;
    }
    void draw() {
        if (!m_showing) return;
        float rx = (m_x - g_left_x) * 65.0f + 1;
        float ry = (m_y - g_top_y) * 65.0f + 1;
        m_sprite.setPosition(rx, ry);
        g_window->draw(m_sprite);
        auto size = m_name.getGlobalBounds();
        if (m_mess_end_time < chrono::system_clock::now()) {
            m_name.setPosition(rx + 32 - size.width / 2, ry - 10);
            g_window->draw(m_name);
        }
        else {
            m_chat.setPosition(rx + 32 - size.width / 2, ry - 10);
            g_window->draw(m_chat);
        }
    }
    void set_name(const char str[]) {
        m_name.setFont(g_font);
        m_name.setString(str);
        if (id < MAX_USER)      m_name.setFillColor(sf::Color(255, 255, 255));
        else                    m_name.setFillColor(sf::Color(255, 255, 0));
        m_name.setStyle(sf::Text::Bold);
    }

    void set_chat(const char str[]) {
        m_chat.setFont(g_font);
        m_chat.setString(str);
        m_chat.setFillColor(sf::Color(255, 255, 255));
        m_chat.setStyle(sf::Text::Bold);
        m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
    }
};

OBJECT avatar;
unordered_map<int, OBJECT> players;

OBJECT white_tile;
OBJECT black_tile;

sf::Texture* board;
sf::Texture* pieces;

//------------------------------------------------------------------------------
// 초기화 함수 (텍스처, 폰트 로드 등)
void client_initialize()
{
    board = new sf::Texture;
    pieces = new sf::Texture;
    board->loadFromFile("chessmap.bmp");
    pieces->loadFromFile("chess2.png");
    if (!g_font.loadFromFile("cour.ttf")) {
        cout << "Font Loading Error!\n";
        exit(-1);
    }
    white_tile = OBJECT{ *board,  5,  5, TILE_WIDTH, TILE_WIDTH };
    black_tile = OBJECT{ *board, 69,  5, TILE_WIDTH, TILE_WIDTH };
    avatar = OBJECT{ *pieces, 128, 0, 64, 64 };
    avatar.move(4, 4);
}

//------------------------------------------------------------------------------
// 종료 시 자원 해제
void client_finish()
{
    players.clear();
    delete board;
    delete pieces;
}

//------------------------------------------------------------------------------
// 서버에서 오는 패킷을 처리하는 함수
void ProcessPacket(char* ptr)
{
    switch (ptr[1])
    {
    case SC_LOGIN_FAIL:
    {
        // 서버가 로그인 실패를 알려줄 때
        cout << "[SERVER] 로그인 실패 (해당 ID가 없거나 중복 로그인) \n";
        exit(0);
    }
    case SC_LOGIN_INFO:
    {
        // 로그인 성공: SC_LOGIN_INFO_PACKET을 받아서 좌표, HP 등을 초기화
        SC_LOGIN_INFO_PACKET* packet = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(ptr);
        g_myid = packet->id;
        avatar.id = g_myid;
        avatar.move(packet->x, packet->y);
        g_left_x = packet->x - SCREEN_WIDTH / 2;
        g_top_y = packet->y - SCREEN_HEIGHT / 2;
        avatar.show();
        avatar.m_hp = packet->hp;
        break;
    }
    case SC_ADD_OBJECT:
    {
        SC_ADD_OBJECT_PACKET* my_packet = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(ptr);
        int id = my_packet->id;

        if (id == g_myid) {
            // 나 자신이 새롭게 보이거나, 리스폰된 경우
            avatar.move(my_packet->x, my_packet->y);
            g_left_x = my_packet->x - SCREEN_WIDTH / 2;
            g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
            avatar.show();
        }
        else if (id < MAX_USER) {
            // 다른 일반 플레이어
            players[id] = OBJECT{ *pieces, 0, 0, 64, 64 };
            players[id].id = id;
            players[id].move(my_packet->x, my_packet->y);
            players[id].set_name(my_packet->name);
            players[id].show();
        }
        else {
            // NPC 등
            players[id] = OBJECT{ *pieces, 256, 0, 64, 64 };
            players[id].id = id;
            players[id].move(my_packet->x, my_packet->y);
            players[id].set_name(my_packet->name);
            players[id].show();
        }
        break;
    }
    case SC_MOVE_OBJECT:
    {
        SC_MOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(ptr);
        int other_id = my_packet->id;
        if (other_id == g_myid) {
            // 나 자신이 이동
            avatar.move(my_packet->x, my_packet->y);
            g_left_x = my_packet->x - SCREEN_WIDTH / 2;
            g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
        }
        else {
            // 다른 플레이어/ NPC가 이동
            players[other_id].move(my_packet->x, my_packet->y);
        }
        break;
    }
    case SC_REMOVE_OBJECT:
    {
        SC_REMOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(ptr);
        int other_id = my_packet->id;
        if (other_id == g_myid) {
            // 나 자신이 제거(로그아웃 혹은 서버 리스폰처리 등)
            avatar.hide();
        }
        else {
            players.erase(other_id);
        }
        break;
    }
    case SC_CHAT:
    {
        SC_CHAT_PACKET* my_packet = reinterpret_cast<SC_CHAT_PACKET*>(ptr);
        int other_id = my_packet->id;
        if (other_id == g_myid) {
            avatar.set_chat(my_packet->mess);
        }
        else {
            players[other_id].set_chat(my_packet->mess);
        }
        break;
    }
    case SC_STAT_CHANGE:
    {
        SC_STAT_CHANGEL_PACKET* statPkt = reinterpret_cast<SC_STAT_CHANGEL_PACKET*>(ptr);
        int targetId = statPkt->id;
        int newHp = statPkt->hp;
        int newMaxHp = statPkt->max_hp;  // 필요하다면 사용

        if (targetId == g_myid) {
            avatar.m_hp = newHp;
        }
        else {
            auto it = players.find(targetId);
            if (it != players.end()) {
                it->second.m_hp = newHp;
            }
        }
        break;
    }
    default:
        printf("Unknown PACKET type [%d]\n", ptr[1]);
    }
}

//------------------------------------------------------------------------------
// 네트워크 버퍼를 받아서 패킷 단위로 분리 처리
void process_data(char* net_buf, size_t io_byte)
{
    char* ptr = net_buf;
    static size_t in_packet_size = 0;
    static size_t saved_packet_size = 0;
    static char packet_buffer[BUF_SIZE];

    while (io_byte > 0) {
        if (in_packet_size == 0) {
            in_packet_size = ptr[0];
        }
        if (io_byte + saved_packet_size >= in_packet_size) {
            // 온전한 한 패킷이 도착한 경우
            memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
            ProcessPacket(packet_buffer);
            ptr += in_packet_size - saved_packet_size;
            io_byte -= in_packet_size - saved_packet_size;
            in_packet_size = 0;
            saved_packet_size = 0;
        }
        else {
            // 부분적인 데이터만 수신된 경우
            memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
            saved_packet_size += io_byte;
            io_byte = 0;
        }
    }
}

//------------------------------------------------------------------------------
// 매 프레임마다 화면 출력 및 네트워크 데이터 수신
void client_main()
{
    char net_buf[BUF_SIZE];
    size_t received;

    auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
    if (recv_result == sf::Socket::Error) {
        wcout << L"[CLIENT] Recv 에러!\n";
        exit(-1);
    }
    if (recv_result == sf::Socket::Disconnected) {
        wcout << L"[CLIENT] Disconnected\n";
        exit(-1);
    }
    if (recv_result != sf::Socket::NotReady && received > 0) {
        process_data(net_buf, received);
    }

    // 배경 타일 그리기
    for (int i = 0; i < SCREEN_WIDTH; ++i) {
        for (int j = 0; j < SCREEN_HEIGHT; ++j) {
            int tile_x = i + g_left_x;
            int tile_y = j + g_top_y;
            if (tile_x < 0 || tile_y < 0) continue;
            if (((tile_x / 3) + (tile_y / 3)) % 2 == 0) {
                white_tile.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
                white_tile.a_draw();
            }
            else {
                black_tile.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
                black_tile.a_draw();
            }
        }
    }

    avatar.draw();
    for (auto& pl : players) {
        pl.second.draw();
    }

    sf::Text text;
    text.setFont(g_font);

    char buf[100];
    sprintf_s(buf, "(%d, %d)  HP: %d", avatar.m_x, avatar.m_y, avatar.m_hp);
    text.setString(buf);
    g_window->draw(text);
}

//------------------------------------------------------------------------------
// 패킷을 서버로 전송하는 헬퍼
void send_packet(void* packet)
{
    unsigned char* p = reinterpret_cast<unsigned char*>(packet);
    size_t sent = 0;
    s_socket.send(packet, p[0], sent);
}

//------------------------------------------------------------------------------
// main 함수: ID 입력받고 서버에 로그인, 윈도우 루프 수행
int main()
{
    wcout.imbue(locale("korean"));

    // 1) 서버 IP 입력
    std::string server_ip;
    cout << "Enter Server IP (e.g., 127.0.0.1): ";
    std::getline(std::cin, server_ip);

    // 2) ID 입력 (숫자 전용, 숫자가 아닌 문자가 있으면 재입력 요청)
    std::string user_id_str;
    while (true) {
        cout << "Enter your ID (숫자로만 구성): ";
        std::getline(cin, user_id_str);

        if (user_id_str.empty()) {
            cout << "ID가 비어 있습니다. 다시 입력하세요.\n";
            continue;
        }

        bool all_digits = true;
        for (char c : user_id_str) {
            if (!isdigit((unsigned char)c)) {
                all_digits = false;
                break;
            }
        }

        if (!all_digits) {
            cout << "잘못된 형식입니다. 숫자만 입력하세요.\n";
            continue;
        }
        break;  // 숫자로만 이루어진 ID가 들어왔으면 루프 탈출
    }

    // 3) 서버 연결
    sf::Socket::Status status = s_socket.connect(server_ip.c_str(), PORT_NUM);
    s_socket.setBlocking(false);

    if (status != sf::Socket::Done) {
        wcout << L"[CLIENT] 서버와 연결할 수 없습니다.\n";
        return 0;
    }

    client_initialize();

    // 4) 로그인 패킷 전송: CS_LOGIN_PACKET.name에 user_id_str을 그대로 복사
    CS_LOGIN_PACKET loginPkt;
    loginPkt.size = sizeof(loginPkt);
    loginPkt.type = CS_LOGIN;
    memset(loginPkt.name, 0, NAME_SIZE);
    strncpy_s(loginPkt.name, user_id_str.c_str(), NAME_SIZE - 1);
    loginPkt.name[NAME_SIZE - 1] = '\0';

    send_packet(&loginPkt);

    // avatar.name(화면에 표시될 이름)도 ID 문자열로 설정
    avatar.set_name(loginPkt.name);

    // 5) SFML 윈도우 루프
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
    g_window = &window;

    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::KeyPressed) {
                int direction = -1;
                switch (event.key.code) {
                case sf::Keyboard::Left:
                    direction = 2;  // LEFT
                    break;
                case sf::Keyboard::Right:
                    direction = 3;  // RIGHT
                    break;
                case sf::Keyboard::Up:
                    direction = 0;  // UP
                    break;
                case sf::Keyboard::Down:
                    direction = 1;  // DOWN
                    break;
                case sf::Keyboard::Escape:
                    window.close();
                    break;
                }
                if (direction != -1) {
                    // 이동 패킷 전송
                    CS_MOVE_PACKET movePkt;
                    movePkt.size = sizeof(movePkt);
                    movePkt.type = CS_MOVE;
                    movePkt.direction = direction;
                    send_packet(&movePkt);
                }
            }
        }

        window.clear();
        client_main();
        window.display();
    }

    client_finish();
    return 0;
}
