cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_SDK_PATH "/home/pi/pico/pico-sdk")

include(pico_sdk_import.cmake)

project(wump C CXX ASM)

option(CHEAT "Include cheat code" OFF)

pico_sdk_init()

add_executable(wump wumpus.c )

pico_set_program_name(wump "wump")
pico_set_program_version(wump "0.2")

pico_enable_stdio_uart(wump 1)
pico_enable_stdio_usb(wump 0)

target_link_libraries(wump pico_stdlib)

pico_add_extra_outputs(wump)

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

