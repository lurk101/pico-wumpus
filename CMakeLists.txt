cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(wump C CXX ASM)

option(CHEAT "Include cheat code" OFF)

pico_sdk_init()

add_executable(wump wumpus.c)

pico_set_program_name(wump "wump")
pico_set_program_version(wump "0.2")

pico_enable_stdio_uart(wump 1)
pico_enable_stdio_usb(wump 0)

target_link_libraries(wump pico_stdlib hardware_flash hardware_sync)

pico_add_extra_outputs(wump)

if (CHEAT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCHEAT=1")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

message(STATUS "Build type ${CMAKE_BUILD_TYPE}, Cheat ${CHEAT}")
