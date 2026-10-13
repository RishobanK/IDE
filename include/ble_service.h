/*
 * SPDX-License-Identifier: Apache-2.0
 * Header: ble_service.h
 * Custom BLE GATT Sensor Service and advertising manager.
 */

#ifndef BLE_SERVICE_H_
#define BLE_SERVICE_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include "sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Custom 128-bit UUID Definitions
 * Base: 4a98xxxx-1cc4-414e-98fe-57a00182f9d8
 */
#define BT_UUID_SENSOR_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x4a980001, 0x1cc4, 0x414e, 0x98fe, 0x57a00182f9d8)

#define BT_UUID_SENSOR_DATA_CHAR_VAL \
    BT_UUID_128_ENCODE(0x4a980002, 0x1cc4, 0x414e, 0x98fe, 0x57a00182f9d8)

#define BT_UUID_SENSOR_CONFIG_CHAR_VAL \
    BT_UUID_128_ENCODE(0x4a980003, 0x1cc4, 0x414e, 0x98fe, 0x57a00182f9d8)

#define BT_UUID_SENSOR_STATUS_CHAR_VAL \
    BT_UUID_128_ENCODE(0x4a980004, 0x1cc4, 0x414e, 0x98fe, 0x57a00182f9d8)

#define BT_UUID_SENSOR_SERVICE      BT_UUID_DECLARE_128(BT_UUID_SENSOR_SERVICE_VAL)
#define BT_UUID_SENSOR_DATA_CHAR    BT_UUID_DECLARE_128(BT_UUID_SENSOR_DATA_CHAR_VAL)
#define BT_UUID_SENSOR_CONFIG_CHAR  BT_UUID_DECLARE_128(BT_UUID_SENSOR_CONFIG_CHAR_VAL)
#define BT_UUID_SENSOR_STATUS_CHAR  BT_UUID_DECLARE_128(BT_UUID_SENSOR_STATUS_CHAR_VAL)

/**
 * @brief Runtime configuration structure writable via BLE characteristic.
 */
typedef struct __attribute__((packed)) {
    uint16_t sample_interval_ms;  /* e.g., 250 ms default */
    uint8_t  enable_motion;       /* 1 = enable IMU, 0 = low-power env only */
    uint8_t  tx_power_dbm;        /* Radio TX power (-40 to +8 dBm) */
} ble_sensor_config_t;

/**
 * @brief Initialize Bluetooth LE controller, register GATT service, and begin advertising.
 * @return 0 on success, negative error code on failure.
 */
int ble_service_init(void);

/**
 * @brief Transmit a sensor telemetry update via GATT notification to connected peer.
 * @param reading Pointer to valid sensor sample.
 * @return 0 on success, -ENOTCONN if not connected, -EACCES if notifications disabled.
 */
int ble_service_notify_sensor_data(const sensor_reading_t *reading);

/**
 * @brief Query current BLE connection status.
 * @return true if central device is connected and bonded/ready.
 */
bool ble_service_is_connected(void);

/**
 * @brief Query if central client has subscribed to CCC notifications.
 * @return true if notifications are active.
 */
bool ble_service_notifications_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_SERVICE_H_ */
