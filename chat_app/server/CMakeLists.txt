cmake_minimum_required(VERSION 3.10)
project(ChatServer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SERVER_SOURCES
    server.c
    chat_handler.c
    server_socket.c
)

find_package(Threads REQUIRED)

add_executable(server ${SERVER_SOURCES})

target_include_directories(server 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(server
    common
    ${CMAKE_THREAD_LIBS_INIT}
)

install(TARGETS server DESTINATION bin)

target_compile_definitions(server PRIVATE
    SERVER_PORT=54321
    BUFFER_SIZE=4096
    MAX_CLIENTS=100
    _GNU_SOURCE
) 