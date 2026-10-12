# Практическая работа №1 - UDP-протокол и архитектура игры

Основа сетевой части сквозного проекта - арена-шутера с топологией **Dedicated Server**.
Консольные UDP-сервер и клиент на C++17 (Berkeley Sockets: Winsock на Windows, POSIX на Linux).

**Что реализовано**

- UDP эхо-сервер и эхо-клиент (`src/echo`);
- игровой протокол: заголовок `type / seqNum / payloadSize / checksum` + payload,
  побайтовая сериализация в сетевом порядке байт;
- команды `MOVEMENT` (x, y, z) и `SHOOT` (weaponId), ответы `STATE` и `ACK`;
- авторитетный сервер: ограничивает скорость и границы мира, ведёт лог команд (консоль + `server.log`);
- клиент отправляет команды с заданной периодичностью (по умолчанию раз в секунду) и выводит ответы;
- доп. задание: контрольная сумма **CRC32** в заголовке и **авторизация** по логину/паролю;
- отбрасывание дубликатов и устаревших пакетов по `seqNum`;
- тесты сериализации (`tests/test_protocol.cpp`).

## Документация

- [Архитектура: выбор топологии, классы, обязанности](docs/Architecture_Design.md)
- [Спецификация протокола: формат пакетов и всех полей](docs/Protocol_Specification.md)

## Сборка

**Windows (MSVC)** - запустить `build.bat` (Visual Studio находится автоматически), бинарники появятся в `build\`.

**CMake (Windows / Linux):**

```sh
cmake -S . -B build
cmake --build build
```

**Linux, вручную:**

```sh
g++ -std=c++17 -O2 src/server/udp_server.cpp -o udp_server
g++ -std=c++17 -O2 src/client/udp_client.cpp -o udp_client
```

## Запуск

```sh
# эхо
build\echo_server.exe            # порт 9000
build\echo_client.exe "Hello"    # сообщение [host] [port]

# игровой протокол
build\udp_server.exe [port]
build\udp_client.exe --login player1 --password qwerty --count 10 --interval 1000
build\udp_client.exe --test-errors   # дополнительно проверяет NOT_AUTHORIZED, BAD_CREDENTIALS, CRC, UNKNOWN_WEAPON

build\test_protocol.exe          # тесты сериализации
```

Параметры клиента: `--host 127.0.0.1 --port 8080 --login player1 --password qwerty --count 10 --interval 1000 --test-errors`.
Учётные записи: `player1/qwerty`, `player2/12345`, `admin/admin`.

### Пример вывода клиента

```
-> [seq=3] LOGIN login="player1"  (24 байт)
<- [seq=3] ACK LOGIN -> OK
Авторизация успешна

-> [seq=4] MOVEMENT с испорченным байтом (ожидается отброс по CRC)
<- нет ответа за 2 с
-> [seq=6] MOVEMENT (-4.00, -65.70, -98.00)  (21 байт)
<- [seq=6] STATE позиция на сервере (-0.34, -5.57, -8.30)
-> [seq=7] SHOOT weaponId=1  (10 байт)
<- [seq=7] ACK SHOOT -> OK
```

### Пример лога сервера (`server.log`)

```
[2026-10-12 18:00:38] Сервер запущен на порту 8080
[2026-10-12 18:00:39] 127.0.0.1:56271 seq=1 SHOOT - клиент не авторизован
[2026-10-12 18:00:39] 127.0.0.1:56271 seq=2 LOGIN login="player1" -> отказ
[2026-10-12 18:00:39] 127.0.0.1:56271 seq=3 LOGIN login="player1" -> успех
[2026-10-12 18:00:39] ОТБРОШЕН пакет от 127.0.0.1:56271 (21 байт): неверная контрольная сумма CRC32
[2026-10-12 18:00:41] 127.0.0.1:56271 [player1] seq=6 MOVEMENT запрошено (-4.00, -65.70, -98.00) -> принято (-0.34, -5.57, -8.30)
[2026-10-12 18:00:41] 127.0.0.1:56271 [player1] seq=7 SHOOT weaponId=1 из (-0.34, -5.57, -8.30) (выстрел #1)
```

## Структура

```
src/common/   net.h, crc32.h, protocol.h - общий код
src/server/   udp_server.cpp
src/client/   udp_client.cpp
src/echo/     echo_server.cpp, echo_client.cpp
tests/        test_protocol.cpp
docs/         Architecture_Design.md, Protocol_Specification.md
```

## Работа с Git (GitHub)

| Ветка                  | Содержимое |
|------------------------|------------|
| `main`                 | стартовый README, .gitignore |
| `feature/udp-echo`     | обёртка над сокетами, эхо-сервер и клиент |
| `feature/game-protocol`| протокол, игровой сервер/клиент, CRC32, авторизация, тесты, документация |
