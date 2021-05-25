cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_SDK_PATH "/home/pi/pico/pico-sdk")

include(pico_sdk_import.cmake)

project(wumpus C CXX ASM)

option(CHEAT "Include cheat code" OFF)

pico_sdk_init()

add_executable(wumpus wumpus.c )

pico_set_program_name(wumpus "wumpus")
pico_set_program_version(wumpus "0.1")

pico_enable_stdio_uart(wumpus 1)
pico_enable_stdio_usb(wumpus 0)

target_link_libraries(wumpus pico_stdlib)

pico_add_extra_outputs(wumpus)

if (NOT ("${CMAKE_BUILD_TYPE}" MATCHES "Debug"))
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections -Wl,–gc-sections -O3")
endif()
if (CHEAT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCHEAT=1")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

message(STATUS "Build type ${CMAKE_BUILD_TYPE}, Cheat ${CHEAT}, Echo ${TERM_ECHO}")
message(STATUS "CFlags ${CMAKE_C_FLAGS}")
message(STATUS "${PICO_COMPILER_DIR}")
message(STATUS "${PICO_COMPILER_CC}")

