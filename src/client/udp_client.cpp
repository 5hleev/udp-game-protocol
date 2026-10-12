// udp_client.cpp - игровой UDP-клиент
//
// Запуск: udp_client [--host 127.0.0.1] [--port 8080] [--login player1]
//                    [--password qwerty] [--count 10] [--interval 1000] [--test-errors]
//
// Авторизуется (LOGIN), затем раз в interval мс отправляет команды,
// чередуя MOVEMENT и SHOOT, и выводит ответ сервера.
#include "../common/net.h"
#include "../common/protocol.h"

#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace proto;

struct Options {
    std::string host     = "127.0.0.1";
    uint16_t    port     = kDefaultPort;
    std::string login    = "player1";
    std::string password = "qwerty";
    int         count    = 10;
    int         interval = 1000;
    bool        testErrors = false;
};

class GameClient {
public:
    explicit GameClient(const Options& opt) : opt_(opt) {}

    bool connect() {
        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ == NET_INVALID_SOCKET) {
            std::cerr << "socket() error: " << net::lastError() << std::endl;
            return false;
        }
        server_.sin_family = AF_INET;
        server_.sin_port   = htons(opt_.port);
        if (inet_pton(AF_INET, opt_.host.c_str(), &server_.sin_addr) != 1) {
            std::cerr << "Некорректный IP-адрес: " << opt_.host << std::endl;
            return false;
        }
        net::setRecvTimeout(sock_, kTimeoutMs);
        std::cout << "Клиент: сервер " << net::addrToString(server_) << std::endl;
        return true;
    }

    // LOGIN с повторами: UDP может потерять пакет
    bool login(const std::string& user, const std::string& pass) {
        for (int attempt = 1; attempt <= 3; ++attempt) {
            uint16_t seq = sendCommand(PacketType::Login, encode(LoginPayload{user, pass}),
                                       "LOGIN login=\"" + user + "\"");
            Packet resp;
            if (!waitResponse(seq, resp)) continue;
            AckPayload ack;
            return resp.header.type == PacketType::Ack && decode(resp.payload, ack)
                && ack.status == Status::Ok;
        }
        return false;
    }

    void sendMovement(const MovementPayload& m) {
        uint16_t seq = sendCommand(PacketType::Movement, encode(m), "MOVEMENT " + fmt(m));
        Packet resp;
        waitResponse(seq, resp);
    }

    void sendShoot(uint8_t weaponId) {
        uint16_t seq = sendCommand(PacketType::Shoot, encode(ShootPayload{weaponId}),
                                   "SHOOT weaponId=" + std::to_string(weaponId));
        Packet resp;
        waitResponse(seq, resp);
    }

    // Отправка заведомо испорченного пакета - сервер должен отбросить его по CRC32
    void sendCorrupted() {
        auto bytes = encodePacket(PacketType::Movement, ++seq_, encode(MovementPayload{1, 2, 3}));
        bytes.back() ^= 0xFF;   // портим последний байт payload
        std::cout << "-> [seq=" << seq_ << "] MOVEMENT с испорченным байтом (ожидается отброс по CRC)"
                  << std::endl;
        sendRaw(bytes);
        Packet resp;
        waitResponse(seq_, resp);
    }

    ~GameClient() {
        if (sock_ != NET_INVALID_SOCKET) net::closeSocket(sock_);
    }

private:
    static constexpr int kTimeoutMs = 2000;

    uint16_t sendCommand(PacketType type, const std::vector<uint8_t>& payload, const std::string& what) {
        ++seq_;
        auto bytes = encodePacket(type, seq_, payload);
        std::cout << "-> [seq=" << seq_ << "] " << what << "  (" << bytes.size() << " байт)" << std::endl;
        sendRaw(bytes);
        return seq_;
    }

    void sendRaw(const std::vector<uint8_t>& bytes) {
        if (sendto(sock_, reinterpret_cast<const char*>(bytes.data()), int(bytes.size()), 0,
                   reinterpret_cast<const sockaddr*>(&server_), sizeof(server_)) == NET_SOCKET_ERROR)
            std::cerr << "   sendto() error: " << net::lastError() << std::endl;
    }

    // Ждёт ответ с нужным seq; ответы на старые запросы пропускает
    bool waitResponse(uint16_t seq, Packet& out) {
        uint8_t buf[kMaxPacketSize + 1];
        for (;;) {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            int n = recvfrom(sock_, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n == NET_SOCKET_ERROR) {
                std::cout << "<- нет ответа за " << kTimeoutMs / 1000 << " с" << std::endl;
                return false;
            }
            DecodeError err = decodePacket(buf, size_t(n), out);
            if (err != DecodeError::None) {
                std::cout << "<- отброшен ответ: " << toString(err) << std::endl;
                continue;
            }
            if (out.header.seqNum != seq) continue;   // запоздавший ответ на старый запрос
            print(out);
            return true;
        }
    }

    static void print(const Packet& p) {
        std::cout << "<- [seq=" << p.header.seqNum << "] " << toString(p.header.type);
        if (p.header.type == PacketType::State) {
            MovementPayload pos;
            if (decode(p.payload, pos)) std::cout << " позиция на сервере " << fmt(pos);
        } else if (p.header.type == PacketType::Ack) {
            AckPayload ack;
            if (decode(p.payload, ack))
                std::cout << " " << toString(ack.ackedType) << " -> " << toString(ack.status);
        }
        std::cout << std::endl;
    }

    static std::string fmt(const MovementPayload& m) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << "(" << m.x << ", " << m.y << ", " << m.z << ")";
        return ss.str();
    }

    Options     opt_;
    socket_t    sock_ = NET_INVALID_SOCKET;
    sockaddr_in server_{};
    uint16_t    seq_ = 0;
};

static Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--host")        o.host = next();
        else if (a == "--port")        o.port = uint16_t(std::stoi(next()));
        else if (a == "--login")       o.login = next();
        else if (a == "--password")    o.password = next();
        else if (a == "--count")       o.count = std::stoi(next());
        else if (a == "--interval")    o.interval = std::stoi(next());
        else if (a == "--test-errors") o.testErrors = true;
        else std::cerr << "Неизвестный параметр: " << a << std::endl;
    }
    return o;
}

static float randomCoord() { return float(std::rand() % 2001 - 1000) / 10.0f; }  // [-100; 100]

int main(int argc, char** argv) {
    net::setupConsole();
    Options opt = parseArgs(argc, argv);
    std::srand(unsigned(std::time(nullptr)));

    if (!net::init()) {
        std::cerr << "Не удалось инициализировать сокеты" << std::endl;
        return 1;
    }
    int rc = 0;
    {
        GameClient client(opt);
        if (!client.connect()) { net::shutdown(); return 1; }

        if (opt.testErrors) {
            std::cout << "\n=== Проверка ошибок ===" << std::endl;
            client.sendShoot(1);                       // до авторизации -> NOT_AUTHORIZED
            client.login(opt.login, "wrong-password"); // -> BAD_CREDENTIALS
            std::cout << std::endl;
        }

        if (!client.login(opt.login, opt.password)) {
            std::cerr << "Авторизация не удалась" << std::endl;
            rc = 1;
        } else {
            std::cout << "Авторизация успешна\n" << std::endl;
            if (opt.testErrors) {
                client.sendCorrupted();                // -> отброшен по CRC, таймаут
                client.sendShoot(42);                  // -> UNKNOWN_WEAPON
                std::cout << std::endl;
            }
            for (int i = 1; i <= opt.count; ++i) {
                if (i % 2 == 1)
                    client.sendMovement({randomCoord(), randomCoord(), randomCoord()});
                else
                    client.sendShoot(uint8_t(std::rand() % 5 + 1));
                if (i < opt.count) net::sleepMs(opt.interval);
            }
        }
    }
    std::cout << "Клиент завершил работу" << std::endl;
    net::shutdown();
    return rc;
}
