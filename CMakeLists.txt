# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")
set(PICO_PLATFORM rp2040 CACHE STRING "Board platform")

# --- Pico SDK import (bundled; standalone) ---------------------------------
# Prefer the project-local copy so this tree does not depend on the parent repo.
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
else()
    include(${CMAKE_CURRENT_LIST_DIR}/../pico_sdk_import.cmake)
endif()

project(mayflash_passthrough C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Pico-PIO-USB ----------------------------------------------------------
# MUST be set before pico_sdk_init(): the SDK creates tinyusb_pico_pio_usb (and
# its pio_usb.h include path) during init, and only if this path already points
# at a valid Pico-PIO-USB checkout.
#
# Prefer the vendored submodule (lib/Pico-PIO-USB) so the project is standalone.
# Fall back to the parent repo's patched checkout if the submodule has not been
# initialized yet -- that patched copy references the `interval_override` symbol,
# so we compile PicoPeripherals/interval_override.cpp only in that case.
set(PASSTHRU_NEED_INTERVAL_OVERRIDE OFF)
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/Pico-PIO-USB/CMakeLists.txt)
    set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/Pico-PIO-USB)
elseif(EXISTS ${CMAKE_CURRENT_LIST_DIR}/../lib/pico_pio_usb/CMakeLists.txt)
    message(WARNING "mayflash-passthrough: using the parent repo's Pico-PIO-USB "
                    "(not standalone). Run 'git submodule update --init' to vendor "
                    "lib/Pico-PIO-USB.")
    set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/../lib/pico_pio_usb)
    set(PASSTHRU_NEED_INTERVAL_OVERRIDE ON)
else()
    message(FATAL_ERROR "Pico-PIO-USB not found. Run: git submodule update --init "
                        "(expected at lib/Pico-PIO-USB).")
endif()

pico_sdk_init()

# mbedtls single-DES (compiled directly from the SDK's bundled mbedtls).
set(MBEDTLS_DIR ${PICO_SDK_PATH}/lib/mbedtls)

set(PASSTHRU_SOURCES
    src/main.cpp
    src/dongle.cpp
    src/buttons.cpp
    src/ssd1306.cpp
    src/led.cpp
    src/usb_descriptors.c
    ${MBEDTLS_DIR}/library/des.c
    ${MBEDTLS_DIR}/library/platform_util.c
)

set(PASSTHRU_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${MBEDTLS_DIR}/include
)

# Only when falling back to the parent's patched Pico-PIO-USB.
if(PASSTHRU_NEED_INTERVAL_OVERRIDE)
    set(PICO_PERIPHERALS_DIR ${CMAKE_CURRENT_LIST_DIR}/../lib/PicoPeripherals)
    list(APPEND PASSTHRU_SOURCES ${PICO_PERIPHERALS_DIR}/interval_override.cpp)
    list(APPEND PASSTHRU_INCLUDES ${PICO_PERIPHERALS_DIR})
endif()

add_executable(mayflash_passthrough ${PASSTHRU_SOURCES})

target_include_directories(mayflash_passthrough PRIVATE ${PASSTHRU_INCLUDES})

# Generate the WS2812 PIO header (ws2812.pio.h) into the build include path.
pico_generate_pio_header(mayflash_passthrough
    ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

# Use our minimal mbedtls config (DES only).
target_compile_definitions(mayflash_passthrough PRIVATE
    MBEDTLS_CONFIG_FILE="mbedtls_config_min.h"
)

target_link_libraries(mayflash_passthrough PRIVATE
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_pio
    hardware_dma
    tinyusb_device
    tinyusb_host
    tinyusb_pico_pio_usb
)

# No stdio over USB (the native USB port is the DualSense device); UART only.
pico_enable_stdio_usb(mayflash_passthrough 0)
pico_enable_stdio_uart(mayflash_passthrough 0)

pico_add_extra_outputs(mayflash_passthrough)
