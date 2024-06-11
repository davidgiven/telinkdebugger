# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.5)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)

set(PICO_SDK_FETCH_FROM_GIT ON)
set(USERHOME $ENV{HOME})
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(telinkdebugger C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(telinkdebugger
  src/telinkdebugger.cpp
  src/usb-descriptors.cpp
  src/usb-uart.cpp
  src/stdio-queue.cpp
)

pico_set_program_name(telinkdebugger "telinkdebugger")
pico_set_program_version(telinkdebugger "0.1")

pico_generate_pio_header(telinkdebugger ${CMAKE_CURRENT_LIST_DIR}/src/sws.pio)

target_include_directories(telinkdebugger PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR} # for our common lwipopts or any other standard includes, if required
)

target_include_directories(telinkdebugger PRIVATE
  pico-sdk/lib/tinyusb/src
)

target_link_libraries(telinkdebugger
        hardware_flash
        hardware_pio
        pico_multicore
        pico_stdlib
        tinyusb_device
)

pico_add_extra_outputs(telinkdebugger)

