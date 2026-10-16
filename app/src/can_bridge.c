/*
 * SPDX-License-Identifier: Apache-2.0
 * Implementation: can_bridge.c
 * Industrial CAN bus telemetry bridge for real-time sensor data broadcasting.
 *
 * Publishes sensor telemetry over CAN 2.0B standard frames (11-bit IDs) at 4 Hz.
 * Three frames are transmitted per sensor sample cycle:
 *   - 0x110 (DLC 8): Environmental data (temperature, humidity, pressure)
 *   - 0x111 (DLC 6): Accelerometer XYZ (milli-g)
 *   - 0x112 (DLC 6): Gyroscope XYZ (milli-degrees per second)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>
#include "can_bridge.h"

LOG_MODULE_REGISTER(can_bridge, CONFIG_LOG_DEFAULT_LEVEL);

/* CAN Frame Identifiers (11-bit Standard IDs) */
#define CAN_ID_ENVIRONMENTAL  0x110  /* Temperature, Humidity, Pressure */
#define CAN_ID_ACCELEROMETER  0x111  /* Accel X, Y, Z */
#define CAN_ID_GYROSCOPE      0x112  /* Gyro X, Y, Z */

/* CAN Device from Devicetree */
#if DT_NODE_EXISTS(DT_NODELABEL(can0))
static const struct device *const s_can_dev = DEVICE_DT_GET(DT_NODELABEL(can0));
#elif DT_NODE_EXISTS(DT_CHOSEN(zephyr_canbus))
static const struct device *const s_can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
#else
static const struct device *const s_can_dev = NULL;
#warning "No CAN controller found in devicetree (can0 or zephyr,canbus). CAN bridge disabled."
#endif

static bool s_can_initialized = false;

int can_bridge_init(void)
{
	if (s_can_dev == NULL) {
		LOG_WRN("CAN bridge: No CAN controller in devicetree, bridge disabled");
		return -ENODEV;
	}

	if (!device_is_ready(s_can_dev)) {
		LOG_ERR("CAN bridge: Device %s not ready", s_can_dev->name);
		return -ENODEV;
	}

	/* Start CAN controller (required before can_send) */
	int rc = can_start(s_can_dev);
	if (rc != 0 && rc != -EALREADY) {
		LOG_ERR("CAN bridge: Failed to start controller (rc=%d)", rc);
		return rc;
	}

	s_can_initialized = true;
	LOG_INF("CAN bridge initialized on device: %s", s_can_dev->name);
	return 0;
}

int can_bridge_publish_telemetry(const sensor_reading_t *reading)
{
	if (!s_can_initialized || s_can_dev == NULL) {
		LOG_DBG("CAN bridge: Not initialized, skipping telemetry publish");
		return -ENODEV;
	}

	if (reading == NULL) {
		return -EINVAL;
	}

	int rc;
	struct can_frame frame;

	/* ========================================================================
	 * Frame 1 (0x110): Environmental Data
	 * DLC=8, little-endian packing
	 * [0-1]: Temperature (int16_t, 0.01°C) = temperature_centi_c
	 * [2-3]: Humidity (uint16_t, 0.01% RH) = humidity_centi_pct
	 * [4-7]: Pressure (uint32_t, Pa) = pressure_pa
	 * ======================================================================== */
	frame.id = CAN_ID_ENVIRONMENTAL;
	frame.flags = 0;  /* Standard 11-bit ID, data frame */
	frame.dlc = 8;

	int16_t temp = (int16_t)reading->temperature_centi_c;
	uint16_t hum = (uint16_t)reading->humidity_centi_pct;
	uint32_t press = reading->pressure_pa;

	frame.data[0] = (uint8_t)(temp & 0xFF);
	frame.data[1] = (uint8_t)((temp >> 8) & 0xFF);
	frame.data[2] = (uint8_t)(hum & 0xFF);
	frame.data[3] = (uint8_t)((hum >> 8) & 0xFF);
	frame.data[4] = (uint8_t)(press & 0xFF);
	frame.data[5] = (uint8_t)((press >> 8) & 0xFF);
	frame.data[6] = (uint8_t)((press >> 16) & 0xFF);
	frame.data[7] = (uint8_t)((press >> 24) & 0xFF);

	rc = can_send(s_can_dev, &frame, K_MSEC(100), NULL, NULL);
	if (rc != 0) {
		LOG_WRN("CAN bridge: Failed to send environmental frame (rc=%d)", rc);
		return rc;
	}

	/* ========================================================================
	 * Frame 2 (0x111): Accelerometer Data
	 * DLC=6, little-endian packing
	 * [0-1]: Accel X (int16_t, mg)
	 * [2-3]: Accel Y (int16_t, mg)
	 * [4-5]: Accel Z (int16_t, mg)
	 * ======================================================================== */
	frame.id = CAN_ID_ACCELEROMETER;
	frame.flags = 0;
	frame.dlc = 6;

	int16_t accel_x = reading->accel_x_mg;
	int16_t accel_y = reading->accel_y_mg;
	int16_t accel_z = reading->accel_z_mg;

	frame.data[0] = (uint8_t)(accel_x & 0xFF);
	frame.data[1] = (uint8_t)((accel_x >> 8) & 0xFF);
	frame.data[2] = (uint8_t)(accel_y & 0xFF);
	frame.data[3] = (uint8_t)((accel_y >> 8) & 0xFF);
	frame.data[4] = (uint8_t)(accel_z & 0xFF);
	frame.data[5] = (uint8_t)((accel_z >> 8) & 0xFF);

	rc = can_send(s_can_dev, &frame, K_MSEC(100), NULL, NULL);
	if (rc != 0) {
		LOG_WRN("CAN bridge: Failed to send accelerometer frame (rc=%d)", rc);
		return rc;
	}

	/* ========================================================================
	 * Frame 3 (0x112): Gyroscope Data
	 * DLC=6, little-endian packing
	 * [0-1]: Gyro X (int16_t, mdps)
	 * [2-3]: Gyro Y (int16_t, mdps)
	 * [4-5]: Gyro Z (int16_t, mdps)
	 * ======================================================================== */
	frame.id = CAN_ID_GYROSCOPE;
	frame.flags = 0;
	frame.dlc = 6;

	int16_t gyro_x = reading->gyro_x_mdps;
	int16_t gyro_y = reading->gyro_y_mdps;
	int16_t gyro_z = reading->gyro_z_mdps;

	frame.data[0] = (uint8_t)(gyro_x & 0xFF);
	frame.data[1] = (uint8_t)((gyro_x >> 8) & 0xFF);
	frame.data[2] = (uint8_t)(gyro_y & 0xFF);
	frame.data[3] = (uint8_t)((gyro_y >> 8) & 0xFF);
	frame.data[4] = (uint8_t)(gyro_z & 0xFF);
	frame.data[5] = (uint8_t)((gyro_z >> 8) & 0xFF);

	rc = can_send(s_can_dev, &frame, K_MSEC(100), NULL, NULL);
	if (rc != 0) {
		LOG_WRN("CAN bridge: Failed to send gyroscope frame (rc=%d)", rc);
		return rc;
	}

	LOG_DBG("CAN bridge: Published 3 telemetry frames (0x110, 0x111, 0x112)");
	return 0;
}
