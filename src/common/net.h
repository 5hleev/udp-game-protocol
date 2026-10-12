// net.h - тонкая обёртка над Berkeley Sockets (Winsock на Windows, POSIX на Linux)
#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t  = SOCKET;
    using socklen_t = int;
    #define NET_INVALID_SOCKET INVALID_SOCKET
    #define NET_SOCKET_ERROR   SOCKET_ERROR
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
    #include <cerrno>
    using socket_t = int;
    #define NET_INVALID_SOCKET (-1)
    #define NET_SOCKET_ERROR   (-1)
#endif

#include <string>

namespace net {

// Инициализация сетевой подсистемы (нужна только на Windows)
inline bool init() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

inline void shutdown() {
#ifdef _WIN32
    WSACleanup();
#endif
}

inline void closeSocket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

inline int lastError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

// Таймаут на recvfrom(); 0 - блокирующий режим
inline bool setRecvTimeout(socket_t s, int timeoutMs) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeoutMs);
#else
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
#endif
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
}

inline void sleepMs(int ms) {
#ifdef _WIN32
    Sleep(static_cast<DWORD>(ms));
#else
    usleep(static_cast<useconds_t>(ms) * 1000);
#endif
}

// Включает UTF-8 в консоли Windows, чтобы русский текст выводился корректно
inline void setupConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

inline std::string addrToString(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

} // namespace net
