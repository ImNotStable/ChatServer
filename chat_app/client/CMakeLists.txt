cmake_minimum_required(VERSION 3.10)
project(ChatClient C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CLIENT_SOURCES
    client.c
    gui.c
    net_handler.c
)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0>=3.0)

include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})

add_executable(client ${CLIENT_SOURCES})

target_include_directories(client 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(client
    common
    ${GTK3_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

install(TARGETS client DESTINATION bin)

target_compile_definitions(client PRIVATE
    BUFFER_SIZE=4096
    MAX_USERNAME_LEN=32
    _GNU_SOURCE
) 