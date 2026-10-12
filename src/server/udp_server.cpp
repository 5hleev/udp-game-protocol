// udp_server.cpp - авторитетный игровой UDP-сервер (Dedicated Server)
//
// Запуск: udp_server [port]
// Принимает LOGIN / MOVEMENT / SHOOT, отвечает ACK / STATE, пишет лог
// всех полученных команд в консоль и в файл server.log.
#include "../common/net.h"
#include "../common/protocol.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using namespace proto;

// ------------------------------------------------------------------ Logger
class Logger {
public:
    explicit Logger(const std::string& path) : file_(path, std::ios::app) {}

    void info(const std::string& msg) {
        std::string line = "[" + timestamp() + "] " + msg;
        std::cout << line << std::endl;
        if (file_) file_ << line << std::endl;
    }

private:
    static std::string timestamp() {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    }
    std::ofstream file_;
};

// ------------------------------------------------------------- AuthService
// Заранее заданный список учётных записей (доп. задание)
class AuthService {
public:
    AuthService() {
        users_ = {{"player1", "qwerty"}, {"player2", "12345"}, {"admin", "admin"}};
    }
    bool check(const std::string& login, const std::string& password) const {
        auto it = users_.find(login);
        return it != users_.end() && it->second == password;
    }

private:
    std::map<std::string, std::string> users_;
};

// ------------------------------------------------------------ Player/World
struct Player {
    std::string     login;
    bool            authorized = false;
    MovementPayload pos;               // авторитетная позиция
    uint16_t        lastSeq = 0;       // последний принятый seq
    bool            hasSeq  = false;
    uint32_t        shots   = 0;
};

class World {
public:
    static constexpr float kHalfSize = 100.0f;  // мир - куб [-100; 100]^3
    static constexpr float kMaxStep  = 10.0f;   // макс. перемещение за одну команду

    // Сервер - авторитет: желаемая позиция клиента ограничивается
    // скоростью и границами мира, клиенту возвращается итоговая позиция.
    MovementPayload applyMove(const MovementPayload& from, const MovementPayload& wanted) const {
        float dx = wanted.x - from.x, dy = wanted.y - from.y, dz = wanted.z - from.z;
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len > kMaxStep) {
            float k = kMaxStep / len;
            dx *= k; dy *= k; dz *= k;
        }
        return {clamp(from.x + dx), clamp(from.y + dy), clamp(from.z + dz)};
    }

    static bool validWeapon(uint8_t id) { return id >= 1 && id <= 5; }

private:
    static float clamp(float v) { return std::max(-kHalfSize, std::min(kHalfSize, v)); }
};

// -------------------------------------------------------------- GameServer
class GameServer {
public:
    explicit GameServer(uint16_t port) : port_(port), log_("server.log") {}

    bool start() {
        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ == NET_INVALID_SOCKET) {
            log_.info("socket() error: " + std::to_string(net::lastError()));
            return false;
        }
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(port_);
        if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == NET_SOCKET_ERROR) {
            log_.info("bind() error: " + std::to_string(net::lastError()));
            return false;
        }
        log_.info("Сервер запущен на порту " + std::to_string(port_));
        return true;
    }

    void run() {
        uint8_t buf[kMaxPacketSize + 1];
        for (;;) {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            int n = recvfrom(sock_, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n == NET_SOCKET_ERROR) {
                // На Windows ICMP "port unreachable" от закрытого клиента приходит как ошибка recvfrom
                log_.info("recvfrom() error: " + std::to_string(net::lastError()));
                continue;
            }
            handleDatagram(buf, size_t(n), from);
        }
    }

    ~GameServer() {
        if (sock_ != NET_INVALID_SOCKET) net::closeSocket(sock_);
    }

private:
    void handleDatagram(const uint8_t* data, size_t size, const sockaddr_in& from) {
        std::string key = net::addrToString(from);
        Packet pkt;
        DecodeError err = decodePacket(data, size, pkt);
        if (err != DecodeError::None) {
            log_.info("ОТБРОШЕН пакет от " + key + " (" + std::to_string(size) +
                      " байт): " + toString(err));
            return;
        }

        const PacketHeader& h = pkt.header;
        Player& pl = players_[key];
        std::string prefix = key + (pl.login.empty() ? "" : " [" + pl.login + "]") +
                             " seq=" + std::to_string(h.seqNum) + " ";

        // Защита от дубликатов и устаревших пакетов (UDP не гарантирует порядок)
        if (h.type != PacketType::Login && pl.hasSeq && !seqNewer(h.seqNum, pl.lastSeq)) {
            log_.info(prefix + toString(h.type) + " - устаревший/повторный пакет, игнорируется");
            return;
        }

        switch (h.type) {
            case PacketType::Login:    onLogin(pkt, pl, from, prefix);    break;
            case PacketType::Movement: onMovement(pkt, pl, from, prefix); break;
            case PacketType::Shoot:    onShoot(pkt, pl, from, prefix);    break;
            default:
                log_.info(prefix + "неизвестный тип 0x" + hex(uint8_t(h.type)));
                sendAck(from, h.seqNum, h.type, Status::UnknownType);
        }
    }

    void onLogin(const Packet& pkt, Player& pl, const sockaddr_in& from, const std::string& prefix) {
        LoginPayload lp;
        if (!decode(pkt.payload, lp)) {
            log_.info(prefix + "LOGIN - некорректный payload");
            sendAck(from, pkt.header.seqNum, PacketType::Login, Status::BadPayload);
            return;
        }
        bool ok = auth_.check(lp.login, lp.password);
        log_.info(prefix + "LOGIN login=\"" + lp.login + "\" -> " + (ok ? "успех" : "отказ"));
        if (ok) {
            pl = Player{};
            pl.login = lp.login;
            pl.authorized = true;
            pl.lastSeq = pkt.header.seqNum;
            pl.hasSeq = true;
        }
        sendAck(from, pkt.header.seqNum, PacketType::Login,
                ok ? Status::Ok : Status::BadCredentials);
    }

    void onMovement(const Packet& pkt, Player& pl, const sockaddr_in& from, const std::string& prefix) {
        MovementPayload want;
        if (!decode(pkt.payload, want)) {
            log_.info(prefix + "MOVEMENT - некорректный payload (" +
                      std::to_string(pkt.payload.size()) + " байт)");
            sendAck(from, pkt.header.seqNum, PacketType::Movement, Status::BadPayload);
            return;
        }
        if (!requireAuth(pl, from, pkt, prefix)) return;
        pl.lastSeq = pkt.header.seqNum;

        pl.pos = world_.applyMove(pl.pos, want);
        log_.info(prefix + "MOVEMENT запрошено " + fmt(want) + " -> принято " + fmt(pl.pos));
        send(from, encodePacket(PacketType::State, pkt.header.seqNum, encode(pl.pos)));
    }

    void onShoot(const Packet& pkt, Player& pl, const sockaddr_in& from, const std::string& prefix) {
        ShootPayload sp;
        if (!decode(pkt.payload, sp)) {
            log_.info(prefix + "SHOOT - некорректный payload");
            sendAck(from, pkt.header.seqNum, PacketType::Shoot, Status::BadPayload);
            return;
        }
        if (!requireAuth(pl, from, pkt, prefix)) return;
        pl.lastSeq = pkt.header.seqNum;

        bool ok = World::validWeapon(sp.weaponId);
        if (ok) ++pl.shots;
        log_.info(prefix + "SHOOT weaponId=" + std::to_string(sp.weaponId) + " из " + fmt(pl.pos) +
                  (ok ? " (выстрел #" + std::to_string(pl.shots) + ")" : " - неизвестное оружие"));
        sendAck(from, pkt.header.seqNum, PacketType::Shoot, ok ? Status::Ok : Status::UnknownWeapon);
    }

    bool requireAuth(const Player& pl, const sockaddr_in& from, const Packet& pkt,
                     const std::string& prefix) {
        if (pl.authorized) return true;
        log_.info(prefix + toString(pkt.header.type) + " - клиент не авторизован");
        sendAck(from, pkt.header.seqNum, pkt.header.type, Status::NotAuthorized);
        return false;
    }

    void sendAck(const sockaddr_in& to, uint16_t seq, PacketType acked, Status st) {
        send(to, encodePacket(PacketType::Ack, seq, encode(AckPayload{acked, st})));
    }

    void send(const sockaddr_in& to, const std::vector<uint8_t>& bytes) {
        sendto(sock_, reinterpret_cast<const char*>(bytes.data()), int(bytes.size()), 0,
               reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }

    static std::string fmt(const MovementPayload& p) {
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(2);
        ss << "(" << p.x << ", " << p.y << ", " << p.z << ")";
        return ss.str();
    }
    static std::string hex(uint8_t v) {
        const char* d = "0123456789ABCDEF";
        return {d[v >> 4], d[v & 0xF]};
    }

    uint16_t port_;
    socket_t sock_ = NET_INVALID_SOCKET;
    Logger   log_;
    AuthService auth_;
    World    world_;
    std::map<std::string, Player> players_;   // ключ - "IP:port" клиента
};

int main(int argc, char** argv) {
    net::setupConsole();
    uint16_t port = argc > 1 ? uint16_t(std::stoi(argv[1])) : kDefaultPort;

    if (!net::init()) {
        std::cerr << "Не удалось инициализировать сокеты" << std::endl;
        return 1;
    }
    {
        GameServer server(port);
        if (!server.start()) { net::shutdown(); return 1; }
        server.run();
    }
    net::shutdown();
    return 0;
}
