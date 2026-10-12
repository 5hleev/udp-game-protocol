// test_protocol.cpp - проверка сериализации, порядка байт и CRC32 (без сети)
#include "../src/common/protocol.h"

#include <cstdio>

using namespace proto;

static int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
        else         { std::printf("ok   %s\n", #cond); }                \
    } while (0)

int main() {
    // CRC32: стандартное контрольное значение
    const char* s = "123456789";
    CHECK(crc::crc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u);

    // Порядок байт: заголовок в big-endian, размер ровно 9 байт
    auto pkt = encodePacket(PacketType::Shoot, 0x1234, encode(ShootPayload{7}));
    CHECK(pkt.size() == kHeaderSize + 1);
    CHECK(pkt[0] == 0x02);
    CHECK(pkt[1] == 0x12 && pkt[2] == 0x34);   // seqNum
    CHECK(pkt[3] == 0x00 && pkt[4] == 0x01);   // payloadSize
    CHECK(pkt[9] == 7);                        // weaponId

    // float 1.0f = 0x3F800000 -> байты 3F 80 00 00
    auto mv = encode(MovementPayload{1.0f, -2.5f, 0.0f});
    CHECK(mv.size() == 12);
    CHECK(mv[0] == 0x3F && mv[1] == 0x80 && mv[2] == 0x00 && mv[3] == 0x00);
    CHECK(mv[4] == 0xC0 && mv[5] == 0x20);     // -2.5f = 0xC0200000

    // Полный круг MOVEMENT: encode -> decodePacket -> decode
    MovementPayload in{12.5f, -99.75f, 3.0f};
    auto bytes = encodePacket(PacketType::Movement, 65535, encode(in));
    Packet p;
    CHECK(decodePacket(bytes.data(), bytes.size(), p) == DecodeError::None);
    CHECK(p.header.type == PacketType::Movement && p.header.seqNum == 65535);
    MovementPayload out;
    CHECK(decode(p.payload, out) && out.x == in.x && out.y == in.y && out.z == in.z);

    // LOGIN
    auto lb = encodePacket(PacketType::Login, 1, encode(LoginPayload{"player1", "qwerty"}));
    CHECK(decodePacket(lb.data(), lb.size(), p) == DecodeError::None);
    LoginPayload lp;
    CHECK(decode(p.payload, lp) && lp.login == "player1" && lp.password == "qwerty");

    // Ошибки
    auto bad = bytes;
    bad[10] ^= 0x01;
    CHECK(decodePacket(bad.data(), bad.size(), p) == DecodeError::BadChecksum);
    CHECK(decodePacket(bytes.data(), 5, p) == DecodeError::TooShort);
    CHECK(decodePacket(bytes.data(), bytes.size() - 1, p) == DecodeError::SizeMismatch);
    CHECK(!decode(std::vector<uint8_t>(11), out));   // неверная длина MOVEMENT

    // Сравнение seq с переполнением
    CHECK(seqNewer(2, 1));
    CHECK(seqNewer(0, 65535));
    CHECK(!seqNewer(65535, 0));
    CHECK(!seqNewer(5, 5));

    std::printf("\n%s (%d ошибок)\n", failures ? "ТЕСТЫ НЕ ПРОЙДЕНЫ" : "ВСЕ ТЕСТЫ ПРОЙДЕНЫ", failures);
    return failures ? 1 : 0;
}
