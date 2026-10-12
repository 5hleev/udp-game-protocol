// echo_server.cpp - UDP эхо-сервер (этап 2: socket -> bind -> recvfrom/sendto)
//
// Запуск: echo_server [port]
#include "../common/net.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    net::setupConsole();
    uint16_t port = argc > 1 ? uint16_t(std::stoi(argv[1])) : 9000;

    if (!net::init()) return 1;

    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == NET_INVALID_SOCKET) {
        std::cerr << "socket() error: " << net::lastError() << std::endl;
        return 1;
    }

    // sockaddr_in: семейство, порт и адрес - порт и адрес в сетевом порядке байт
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);         // host -> network short
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // host -> network long, все интерфейсы

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == NET_SOCKET_ERROR) {
        std::cerr << "bind() error: " << net::lastError() << std::endl;
        net::closeSocket(sock);
        net::shutdown();
        return 1;
    }
    std::cout << "Эхо-сервер слушает порт " << port << std::endl;

    char buf[1024];
    for (;;) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n == NET_SOCKET_ERROR) continue;

        std::cout << net::addrToString(from) << " -> \"" << std::string(buf, n) << "\"" << std::endl;
        sendto(sock, buf, n, 0, reinterpret_cast<sockaddr*>(&from), fromLen);
    }
}
