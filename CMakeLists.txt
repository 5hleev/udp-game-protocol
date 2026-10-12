cmake_minimum_required(VERSION 3.10)
project(udp_game_protocol CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_executable(udp_server  src/server/udp_server.cpp)
add_executable(udp_client  src/client/udp_client.cpp)
add_executable(echo_server src/echo/echo_server.cpp)
add_executable(echo_client src/echo/echo_client.cpp)
add_executable(test_protocol tests/test_protocol.cpp)

if(WIN32)
    foreach(t udp_server udp_client echo_server echo_client)
        target_link_libraries(${t} ws2_32)
    endforeach()
endif()

enable_testing()
add_test(NAME protocol COMMAND test_protocol)
