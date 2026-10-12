// echo_client.cpp - UDP эхо-клиент (этап 3): отправляет строку и ждёт её обратно
//
// Запуск: echo_client [message] [host] [port]
#include "../common/net.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    net::setupConsole();
    std::string msg  = argc > 1 ? argv[1] : "Hello, UDP!";
    std::string host = argc > 2 ? argv[2] : "127.0.0.1";
    uint16_t    port = argc > 3 ? uint16_t(std::stoi(argv[3])) : 9000;

    if (!net::init()) return 1;

    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == NET_INVALID_SOCKET) {
        std::cerr << "socket() error: " << net::lastError() << std::endl;
        return 1;
    }
    net::setRecvTimeout(sock, 2000);

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port   = htons(port);
    inet_pton(AF_INET, host.c_str(), &server.sin_addr);

    sendto(sock, msg.data(), int(msg.size()), 0, reinterpret_cast<sockaddr*>(&server), sizeof(server));
    std::cout << "Отправлено: \"" << msg << "\"" << std::endl;

    char buf[1024];
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
    int rc = 0;
    if (n == NET_SOCKET_ERROR) {
        std::cout << "Ответ не получен (таймаут)" << std::endl;
        rc = 1;
    } else {
        std::cout << "Получено от " << net::addrToString(from) << ": \"" << std::string(buf, n) << "\""
                  << std::endl;
    }
    net::closeSocket(sock);
    net::shutdown();
    return rc;
}
