# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# Expose the custom BSP directory to Zephyr's board discovery mechanism.
# BOARD_ROOT tells west/cmake to search <BOARD_ROOT>/boards/ for board definitions.
# This allows: west build -b custom_sensor_node app
# Supported boards in this repo:
#   custom_sensor_node   — Production custom nRF52840 hardware
#   nrf52840dk_nrf52840  — Nordic DK (development fallback)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Locate Zephyr package via environment or west workspace
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf52840_ble_sensor_hub LANGUAGES C CXX)

# Target source files
target_sources(app PRIVATE
    src/main.c
    src/sensor_manager.c
    src/ble_service.c
    src/ota_manager.c
    src/crash_logger.c
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
