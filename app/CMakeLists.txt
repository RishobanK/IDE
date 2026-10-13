# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# Locate Zephyr package via environment or west workspace
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf52840_ble_sensor_hub LANGUAGES C CXX)

# Target source files
target_sources(app PRIVATE
    src/main.c
    src/sensor_manager.c
    src/ble_service.c
    src/ota_manager.c
    ../drivers/custom_sensor_bme280.c
)

# Header search paths
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivers
)

# Compiler warnings and optimization flags
target_compile_options(app PRIVATE
    -Wall
    -Wextra
    -Werror=implicit-function-declaration
    -Wno-unused-parameter
    -O2
)
