cmake_minimum_required(VERSION 3.10)
project(ChatCommon C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(COMMON_SOURCES
    logger.c
    protocol.c
)

add_library(common STATIC ${COMMON_SOURCES})

target_include_directories(common
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

install(TARGETS common
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

target_compile_definitions(common PRIVATE
    BUFFER_SIZE=4096
    MAX_USERNAME_LEN=32
    MAX_PASSWORD_LEN=64
    SERVER_PORT=54321
    _GNU_SOURCE
) 