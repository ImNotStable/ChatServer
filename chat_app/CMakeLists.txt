cmake_minimum_required(VERSION 3.10)
project(ChatApp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (Debug/Release)" FORCE)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0>=3.0)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GTK3_INCLUDE_DIRS}
)
link_directories(${GTK3_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})

add_subdirectory(common)
add_subdirectory(server)
add_subdirectory(client)

set_target_properties(server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/server"
)

set_target_properties(client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client"
)

install(TARGETS server client
    RUNTIME DESTINATION bin
)

add_custom_target(clean_logs
    COMMAND ${CMAKE_COMMAND} -E remove -f server/server.log client/client.log
    COMMENT "Cleaning log files"
) 