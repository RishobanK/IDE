# ==============================================================================
# board.cmake — CMake flash/debug runner configuration for custom_sensor_node
#
# Configures the Zephyr west flash and west debug commands to use the
# Segger J-Link probe targeting the nRF52840 SoC.
# ==============================================================================

# Use Segger J-Link as the primary programming and debugging interface
set(BOARD_DEBUG_RUNNER jlink)
set(BOARD_FLASH_RUNNER jlink)

# nRF52840 J-Link device identifier
set(JLINK_DEVICE "nRF52840_XXAA")

# Use SWD (Serial Wire Debug) interface at 4000 kHz
set(JLINK_SPEED 4000)
set(JLINK_IF "SWD")

# Include the J-Link runner CMake integration from Zephyr
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)

# ---- Usage Notes ----
#
# Flash:  west flash -b custom_sensor_node
# Debug:  west debug -b custom_sensor_node  (opens GDB server on localhost:2331)
#
# Manual J-Link flash (without west):
#   JLinkExe -device nRF52840_XXAA -if SWD -speed 4000 -autoconnect 1
#   > loadfile build/zephyr/zephyr.hex
#   > r
#   > g
#   > exit
