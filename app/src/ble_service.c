/*
 * SPDX-License-Identifier: Apache-2.0
 * Implementation: ble_service.c
 * Production Custom BLE GATT Service, connection callbacks, and notification pipeline.
 */

#include "ble_service.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_svc, CONFIG_LOG_DEFAULT_LEVEL);

/* Active connection handle */
static struct bt_conn *s_current_conn = NULL;
static bool s_notify_enabled = false;

/* Configurable device parameters */
static ble_sensor_config_t s_active_config = {
    .sample_interval_ms = 250,
    .enable_motion = 1,
    .tx_power_dbm = 0,
};

/* Advertising payload */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_SENSOR_SERVICE_VAL),
};

/* GATT Callback: CCC (Client Characteristic Configuration) Changed */
static void sensor_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    s_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Sensor Telemetry Notifications %s", s_notify_enabled ? "ENABLED" : "DISABLED");
}

/* GATT Callback: Read Sensor Data Characteristic */
static ssize_t read_sensor_data(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    sensor_reading_t reading;
    int rc = sensor_manager_get_latest(&reading);
    if (rc < 0) {
        LOG_WRN("No sensor reading cached for GATT read");
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &reading, sizeof(reading));
}

/* GATT Callback: Read Config */
static ssize_t read_config(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &s_active_config, sizeof(s_active_config));
}

/* GATT Callback: Write Config */
static ssize_t write_config(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *buf, uint16_t len, uint16_t offset,
                            uint8_t flags)
{
    if (offset != 0 || len != sizeof(ble_sensor_config_t)) {
        LOG_WRN("Invalid config write payload size: len=%u offset=%u", len, offset);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const ble_sensor_config_t *new_cfg = (const ble_sensor_config_t *)buf;
    if (new_cfg->sample_interval_ms < 50 || new_cfg->sample_interval_ms > 10000) {
        LOG_WRN("Rejected invalid sampling rate: %u ms", new_cfg->sample_interval_ms);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    s_active_config = *new_cfg;
    LOG_INF("Updated configuration: interval=%u ms, motion=%u, tx_power=%d dBm",
            s_active_config.sample_interval_ms,
            s_active_config.enable_motion,
            s_active_config.tx_power_dbm);

    return len;
}

/* GATT Callback: Read Status string */
static ssize_t read_status(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset)
{
    static const char status_str[] = "FW:v1.0.0-prod | HW:nRF52840-DK | State:NOMINAL";
    return bt_gatt_attr_read(conn, attr, buf, len, offset, status_str, sizeof(status_str) - 1);
}

/* Custom GATT Service Declaration */
BT_GATT_SERVICE_DEFINE(sensor_hub_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_SENSOR_SERVICE),

    /* 1. Sensor Telemetry Characteristic (Read, Notify) */
    BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_DATA_CHAR,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_sensor_data, NULL, NULL),
    BT_GATT_CCC(sensor_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    /* 2. Sensor Configuration Characteristic (Read, Write) */
    BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_CONFIG_CHAR,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_config, write_config, &s_active_config),

    /* 3. System Status / Device Diagnostics (Read) */
    BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_STATUS_CHAR,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_status, NULL, NULL),
);

/* Connection Lifecycle Callbacks */
static void connected_cb(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("BLE Connection attempt failed: %u", err);
        return;
    }

    s_current_conn = bt_conn_ref(conn);
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("BLE Client Connected: %s", addr);

    /* Negotiate high-throughput connection parameters (7.5ms min, 15ms max) */
    struct bt_le_conn_param param = {
        .interval_min = 12, /* 15 ms */
        .interval_max = 24, /* 30 ms */
        .latency = 0,
        .timeout = 400,     /* 4000 ms */
    };
    int rc = bt_conn_le_param_update(conn, &param);
    if (rc && rc != -EALREADY) {
        LOG_WRN("Failed to request connection param update: %d", rc);
    }
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("BLE Client Disconnected: %s (reason: 0x%02X)", addr, reason);

    if (s_current_conn) {
        bt_conn_unref(s_current_conn);
        s_current_conn = NULL;
    }
    s_notify_enabled = false;

    /* Resume Advertising */
    int rc = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (rc) {
        LOG_ERR("Advertising restart failed: %d", rc);
    } else {
        LOG_INF("Advertising restarted successfully");
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected_cb,
    .disconnected = disconnected_cb,
};

int ble_service_init(void)
{
    LOG_INF("Initializing Bluetooth LE subsystem...");

    bt_conn_cb_register(&conn_callbacks);

    int rc = bt_enable(NULL);
    if (rc) {
        LOG_ERR("Bluetooth initialization failed: %d", rc);
        return rc;
    }

    LOG_INF("Bluetooth initialized successfully");

    rc = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (rc) {
        LOG_ERR("Advertising start failed: %d", rc);
        return rc;
    }

    LOG_INF("BLE Advertising active as '%s'", CONFIG_BT_DEVICE_NAME);
    return 0;
}

int ble_service_notify_sensor_data(const sensor_reading_t *reading)
{
    if (!s_current_conn) {
        return -ENOTCONN;
    }
    if (!s_notify_enabled) {
        return -EACCES;
    }

    /* Index 2 is the value attribute of the first characteristic in sensor_hub_svc */
    int rc = bt_gatt_notify(s_current_conn, &sensor_hub_svc.attrs[2], reading, sizeof(*reading));
    if (rc) {
        LOG_WRN("Failed to notify sensor sample (rc=%d)", rc);
        return rc;
    }

    return 0;
}

bool ble_service_is_connected(void)
{
    return (s_current_conn != NULL);
}

bool ble_service_notifications_enabled(void)
{
    return s_notify_enabled;
}
