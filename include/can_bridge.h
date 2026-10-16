/*
 * SPDX-License-Identifier: Apache-2.0
 * Header: can_bridge.h
 * Industrial CAN bus telemetry bridge for sensor data broadcasting.
 */

#ifndef CAN_BRIDGE_H_
#define CAN_BRIDGE_H_

#include "sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the CAN bus controller and configure bit timing.
 *
 * Fetches the CAN device from devicetree (can0 or zephyr,canbus chosen node),
 * configures the controller to started mode, and prepares for frame transmission.
 *
 * @return 0 on success, negative error code on failure.
 */
int can_bridge_init(void);

/**
 * @brief Publish sensor telemetry data over CAN bus as three standard frames.
 *
 * Transmits three CAN 2.0B standard frames (11-bit IDs) containing:
 *   - Frame 0x110: Environmental data (temperature, humidity, pressure)
 *   - Frame 0x111: Accelerometer data (X, Y, Z in milli-g)
 *   - Frame 0x112: Gyroscope data (X, Y, Z in milli-degrees per second)
 *
 * @param reading Pointer to sensor_reading_t structure containing telemetry.
 * @return 0 on success, negative error code if CAN transmit fails.
 */
int can_bridge_publish_telemetry(const sensor_reading_t *reading);

#ifdef __cplusplus
}
#endif

#endif /* CAN_BRIDGE_H_ */
