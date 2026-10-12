// protocol.h - игровой протокол поверх UDP, общий для клиента и сервера.
//
// Формат пакета (все многобайтовые поля - big-endian, сетевой порядок байт):
//
//   +--------+-----------+-------------+----------+-----------------+
//   | type   | seqNum    | payloadSize | checksum | payload ...     |
//   | uint8  | uint16    | uint16      | uint32   | payloadSize байт|
//   +--------+-----------+-------------+----------+-----------------+
//   0        1           3             5          9
//
// Сериализация выполняется поле за полем (ByteWriter/ByteReader), а не через
// memcpy структуры целиком: так результат не зависит от выравнивания полей,
// порядка байт процессора и компилятора. Подробности - docs/Protocol_Specification.md
#pragma once

#include "crc32.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace proto {

constexpr uint16_t kDefaultPort   = 8080;
constexpr size_t   kHeaderSize    = 9;     // 1 + 2 + 2 + 4
constexpr size_t   kMaxPacketSize = 512;   // с запасом меньше типичного MTU
constexpr size_t   kMaxPayload    = kMaxPacketSize - kHeaderSize;
constexpr size_t   kMaxCredential = 32;    // макс. длина логина/пароля

// ---------------------------------------------------------------- типы пакетов
enum class PacketType : uint8_t {
    Movement = 0x01,   // C->S  желаемая позиция игрока
    Shoot    = 0x02,   // C->S  выстрел
    Ack      = 0x03,   // S->C  подтверждение команды (статус)
    Login    = 0x04,   // C->S  логин и пароль
    State    = 0x05,   // S->C  авторитетная позиция игрока после MOVEMENT
};

enum class Status : uint8_t {
    Ok             = 0x00,
    NotAuthorized  = 0x01,   // команда до успешного LOGIN
    BadCredentials = 0x02,   // неверный логин/пароль
    BadPayload     = 0x03,   // размер/содержимое payload не соответствует типу
    UnknownWeapon  = 0x04,   // weaponId вне допустимого диапазона
    UnknownType    = 0x05,   // неизвестный PacketType
};

inline const char* toString(PacketType t) {
    switch (t) {
        case PacketType::Movement: return "MOVEMENT";
        case PacketType::Shoot:    return "SHOOT";
        case PacketType::Ack:      return "ACK";
        case PacketType::Login:    return "LOGIN";
        case PacketType::State:    return "STATE";
    }
    return "UNKNOWN";
}

inline const char* toString(Status s) {
    switch (s) {
        case Status::Ok:             return "OK";
        case Status::NotAuthorized:  return "NOT_AUTHORIZED";
        case Status::BadCredentials: return "BAD_CREDENTIALS";
        case Status::BadPayload:     return "BAD_PAYLOAD";
        case Status::UnknownWeapon:  return "UNKNOWN_WEAPON";
        case Status::UnknownType:    return "UNKNOWN_TYPE";
    }
    return "?";
}

// ------------------------------------------------------- игровые структуры
struct PacketHeader {
    PacketType type        = PacketType::Ack;
    uint16_t   seqNum      = 0;
    uint16_t   payloadSize = 0;
    uint32_t   checksum    = 0;
};

struct MovementPayload {       // 12 байт; тот же формат у STATE
    float x = 0, y = 0, z = 0;
};

struct ShootPayload {          // 1 байт
    uint8_t weaponId = 0;
};

struct LoginPayload {          // 2 + len(login) + len(password) байт
    std::string login;
    std::string password;
};

struct AckPayload {            // 2 байта
    PacketType ackedType = PacketType::Ack;
    Status     status    = Status::Ok;
};

// ------------------------------------------------ побайтовая сериализация
class ByteWriter {
public:
    void u8(uint8_t v)   { buf_.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v >> 8)); u8(uint8_t(v)); }
    void u32(uint32_t v) { u16(uint16_t(v >> 16)); u16(uint16_t(v)); }
    // float (IEEE-754 binary32) передаём как его 32-битное представление в big-endian
    void f32(float v) {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    void bytes(const void* data, size_t n) {
        auto p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + n);
    }
    // Короткая строка: uint8 длина + байты
    void str8(const std::string& s) {
        u8(uint8_t(s.size()));
        bytes(s.data(), s.size());
    }
    std::vector<uint8_t>& data() { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : p_(data), size_(size) {}

    uint8_t u8() {
        if (!need(1)) return 0;
        return p_[pos_++];
    }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = uint16_t(p_[pos_] << 8 | p_[pos_ + 1]);
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        uint32_t hi = u16();
        uint32_t lo = u16();
        return hi << 16 | lo;
    }
    float f32() {
        uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    std::string str8() {
        uint8_t n = u8();
        if (!need(n)) return {};
        std::string s(reinterpret_cast<const char*>(p_ + pos_), n);
        pos_ += n;
        return s;
    }

    bool   ok() const        { return ok_; }
    size_t remaining() const { return size_ - pos_; }
    // Всё прочитано без ошибок и без лишних байтов в конце
    bool   finished() const  { return ok_ && pos_ == size_; }

private:
    bool need(size_t n) {
        if (!ok_ || size_ - pos_ < n) { ok_ = false; return false; }
        return true;
    }
    const uint8_t* p_;
    size_t size_;
    size_t pos_ = 0;
    bool   ok_  = true;
};

// --------------------------------------------- payload <-> байты
inline std::vector<uint8_t> encode(const MovementPayload& m) {
    ByteWriter w; w.f32(m.x); w.f32(m.y); w.f32(m.z);
    return std::move(w.data());
}
inline std::vector<uint8_t> encode(const ShootPayload& s) {
    ByteWriter w; w.u8(s.weaponId);
    return std::move(w.data());
}
inline std::vector<uint8_t> encode(const LoginPayload& l) {
    ByteWriter w; w.str8(l.login); w.str8(l.password);
    return std::move(w.data());
}
inline std::vector<uint8_t> encode(const AckPayload& a) {
    ByteWriter w; w.u8(uint8_t(a.ackedType)); w.u8(uint8_t(a.status));
    return std::move(w.data());
}

inline bool decode(const std::vector<uint8_t>& b, MovementPayload& m) {
    ByteReader r(b.data(), b.size());
    m.x = r.f32(); m.y = r.f32(); m.z = r.f32();
    return r.finished();
}
inline bool decode(const std::vector<uint8_t>& b, ShootPayload& s) {
    ByteReader r(b.data(), b.size());
    s.weaponId = r.u8();
    return r.finished();
}
inline bool decode(const std::vector<uint8_t>& b, LoginPayload& l) {
    ByteReader r(b.data(), b.size());
    l.login = r.str8(); l.password = r.str8();
    return r.finished() && !l.login.empty()
        && l.login.size() <= kMaxCredential && l.password.size() <= kMaxCredential;
}
inline bool decode(const std::vector<uint8_t>& b, AckPayload& a) {
    ByteReader r(b.data(), b.size());
    a.ackedType = PacketType(r.u8()); a.status = Status(r.u8());
    return r.finished();
}

// ------------------------------------------------ пакет целиком
struct Packet {
    PacketHeader         header;
    std::vector<uint8_t> payload;
};

// CRC32 считается по всему пакету, в котором поле checksum временно равно 0
inline std::vector<uint8_t> encodePacket(PacketType type, uint16_t seq,
                                         const std::vector<uint8_t>& payload) {
    ByteWriter w;
    w.u8(uint8_t(type));
    w.u16(seq);
    w.u16(uint16_t(payload.size()));
    w.u32(0);                                     // место под checksum
    w.bytes(payload.data(), payload.size());

    auto& buf = w.data();
    uint32_t c = crc::crc32(buf.data(), buf.size());
    buf[5] = uint8_t(c >> 24); buf[6] = uint8_t(c >> 16);
    buf[7] = uint8_t(c >> 8);  buf[8] = uint8_t(c);
    return std::move(buf);
}

enum class DecodeError { None, TooShort, SizeMismatch, BadChecksum };

inline const char* toString(DecodeError e) {
    switch (e) {
        case DecodeError::None:         return "OK";
        case DecodeError::TooShort:     return "пакет короче заголовка";
        case DecodeError::SizeMismatch: return "payloadSize не совпадает с размером датаграммы";
        case DecodeError::BadChecksum:  return "неверная контрольная сумма CRC32";
    }
    return "?";
}

inline DecodeError decodePacket(const uint8_t* data, size_t size, Packet& out) {
    if (size < kHeaderSize) return DecodeError::TooShort;

    ByteReader r(data, size);
    out.header.type        = PacketType(r.u8());
    out.header.seqNum      = r.u16();
    out.header.payloadSize = r.u16();
    out.header.checksum    = r.u32();

    if (size != kHeaderSize + out.header.payloadSize) return DecodeError::SizeMismatch;

    // Пересчитываем CRC, обнулив поле checksum (байты 5..8)
    std::vector<uint8_t> copy(data, data + size);
    std::memset(copy.data() + 5, 0, 4);
    if (crc::crc32(copy.data(), copy.size()) != out.header.checksum)
        return DecodeError::BadChecksum;

    out.payload.assign(data + kHeaderSize, data + size);
    return DecodeError::None;
}

// true, если seq a "новее" b с учётом переполнения uint16 (65535 -> 0)
inline bool seqNewer(uint16_t a, uint16_t b) {
    return a != b && uint16_t(a - b) < 0x8000;
}

} // namespace proto
