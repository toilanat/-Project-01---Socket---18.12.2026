#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#ifdef _WIN32
  #include <conio.h>
  #include <windows.h>
#else
  #include <termios.h>
  #include <unistd.h>
#endif

#pragma once
#ifdef _WIN32
  #define _WIN32_WINNT 0x0601
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  #define CLOSE_SOCKET(s) closesocket(s)
  #define IS_VALID_SOCKET(s) ((s) != INVALID_SOCKET)
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
  constexpr socket_t INVALID_SOCKET = -1;
  #define CLOSE_SOCKET(s) close(s)
  #define IS_VALID_SOCKET(s) ((s) >= 0)
#endif

inline void init_network() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

inline void cleanup_network() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// src/protocol.cpp snippet
bool send_all(socket_t sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool recv_exact(socket_t sock, char* buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        int n = recv(sock, buf + received, static_cast<int>(len - received), 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool recv_header(socket_t sock, std::string& out_line) {
    out_line.clear();
    char c;
    while (true) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') break;
        out_line.push_back(c);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Terminal helpers: read a single key without waiting for Enter
// ---------------------------------------------------------------------------
#ifdef _WIN32
static void setup_terminal() {
    // Enable ANSI escape codes (\033[K) in the Windows console
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(out, &mode);
    SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
static void restore_terminal() {}

// Returns the key, or -1 for keys we ignore (arrows, F1..F12)
static int get_key() {
    int ch = _getch();
    if (ch == 0 || ch == 0xE0) {   // special keys send 2 codes
        _getch();                  // read and ignore the second code
        return -1;
    }
    return ch;
}
#else
static termios g_old_settings;

static void setup_terminal() {
    tcgetattr(STDIN_FILENO, &g_old_settings);
    termios raw = g_old_settings;
    raw.c_lflag &= ~(ICANON | ECHO);   // keys arrive one by one, no echo
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}
static void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_old_settings);
}
static int get_key() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return 3;   // treat EOF as Ctrl+C
    if (c == 27) {                                  // arrow keys: ESC [ A
        char seq[2];
        read(STDIN_FILENO, seq, 2);
        return -1;
    }
    return static_cast<unsigned char>(c);
}
#endif

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------
static const std::string PROMPT     = "> ";
static const std::string CLEAR_LINE = "\r\033[K";   // start of line + erase

static std::string       buffer;                    // text typed so far
static std::mutex        screen_mutex;
static std::atomic<bool> running{true};

// Print a line ABOVE the input line, then redraw prompt + typed text.
// Safe to call from any thread (receiver, file sender, UDP listener...).
static void show_message(const std::string& line) {
    std::lock_guard<std::mutex> lock(screen_mutex);
    std::cout << CLEAR_LINE            // 1. erase input line
              << line << "\n"          // 2. print the message
              << PROMPT << buffer      // 3. redraw prompt + typed text
              << std::flush;
}

std::string read_command_line(const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(screen_mutex);
        std::cout << PROMPT << buffer << std::flush;
    }

    while (true) {
        int ch = get_key(); // read a single key
        if (ch == -1) continue; // ignore special keys

        std::lock_guard<std::mutex> lock(screen_mutex);
        if (ch == '\r' || ch == '\n') { // enter
            std::string text = buffer;
            buffer.clear();
            std::cout << CLEAR_LINE;
            return text;
        } else if (ch == 8 || ch == 127) { // backspace
            if (!buffer.empty()) {
                buffer.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else if (ch == 3) { // Ctrl+C
            std::cout << "\n" << std::flush;
            return "/quit";
        } else if (ch >= 32 && ch < 127) { // printable ASCII
            buffer += static_cast<char>(ch);
            std::cout << static_cast<char>(ch) << std::flush;
        }
    }
}

int main() {
    return 0;
}