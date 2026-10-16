/*
 * SPDX-License-Identifier: Apache-2.0
 * Application: nRF52840 BLE Sensor Hub
 * Main firmware entry point: orchestrates threads, queues, and system supervision.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include "sensor_manager.h"
#include "ble_service.h"
#include "ota_manager.h"
#include "crash_logger.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Thread Stack Allocations */
#define SENSOR_THREAD_STACK_SIZE    2048
#define BLE_THREAD_STACK_SIZE       2048
#define SUPERVISOR_THREAD_STACK_SIZE 1024

/* Thread Priorities (Lower numeric value = higher execution priority) */
#define SENSOR_THREAD_PRIORITY      5
#define BLE_THREAD_PRIORITY         6
#define SUPERVISOR_THREAD_PRIORITY  7

K_THREAD_STACK_DEFINE(sensor_stack, SENSOR_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(ble_stack, BLE_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(supervisor_stack, SUPERVISOR_THREAD_STACK_SIZE);

static struct k_thread sensor_thread_data;
static struct k_thread ble_thread_data;
static struct k_thread supervisor_thread_data;

/* Hardware Status LED (nRF52840-DK LED1 on P0.13) */
static const struct gpio_dt_spec s_led0 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_status), gpios, {0});

/* Hardware watchdog device (10-second timeout, 5-second feed interval) */
static const struct device *s_watchdog_dev;
static int s_watchdog_channel_id;

/**
 * @brief Thread 1: High-priority periodic sensor acquisition loop.
 */
static void sensor_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Sensor Acquisition Thread started [Priority %d]", SENSOR_THREAD_PRIORITY);

    while (1) {
        /* Resume I2C and SPI buses for active sampling */
        sensor_manager_resume_buses();

        /* Perform 4 Hz sensor acquisition */
        sensor_reading_t sample;
        int rc = sensor_manager_sample_all(&sample);
        if (rc == 0) {
            LOG_DBG("Sample #%u: T=%d.%02d C, P=%u Pa, H=%u.%02d%%",
                    sample.sequence_num,
                    sample.temperature_centi_c / 100,
                    sample.temperature_centi_c % 100,
                    sample.pressure_pa,
                    sample.humidity_centi_pct / 100,
                    sample.humidity_centi_pct % 100);
        } else {
            LOG_WRN("Sensor sample acquisition fault (rc=%d)", rc);
        }

        /* Suspend buses to save power during idle period */
        sensor_manager_suspend_buses();

        /* Sample period: 250 ms (4 Hz update rate) */
        k_msleep(250);
    }
}

/**
 * @brief Thread 2: Event-driven BLE GATT notification dispatcher.
 * Blocks on the sensor message queue; wakes instantly on new sample.
 */
static void ble_dispatch_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct k_msgq *msgq = sensor_manager_get_msgq();
    sensor_reading_t reading;

    LOG_INF("BLE Dispatch Thread started [Priority %d]", BLE_THREAD_PRIORITY);

    while (1) {
        /* Pend on message queue indefinitely until new reading arrives */
        int rc = k_msgq_get(msgq, &reading, K_FOREVER);
        if (rc == 0) {
            if (ble_service_is_connected() && ble_service_notifications_enabled()) {
                int notify_rc = ble_service_notify_sensor_data(&reading);
                if (notify_rc == 0) {
                    LOG_DBG("GATT Notification pushed: seq=%u", reading.sequence_num);
                }
            }
        }
    }
}

/**
 * @brief Thread 3: Low-priority supervisory health monitor and stack analyzer.
 */
static void supervisor_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Supervisory Health Monitor Thread started [Priority %d]", SUPERVISOR_THREAD_PRIORITY);

    while (1) {
        k_sleep(K_SECONDS(5));

        /* Toggle status heartbeat LED */
        if (s_led0.port && device_is_ready(s_led0.port)) {
            gpio_pin_toggle_dt(&s_led0);
        }

        /* Feed the hardware watchdog to prevent timeout reset */
        if (s_watchdog_dev && device_is_ready(s_watchdog_dev)) {
            int rc = wdt_feed(s_watchdog_dev, s_watchdog_channel_id);
            if (rc != 0) {
                LOG_ERR("Watchdog feed failed (rc=%d)", rc);
            }
        }

        /* Check thread stack watermarks for safety */
        size_t sensor_unused = 0, ble_unused = 0;
        k_thread_stack_space_get(&sensor_thread_data, &sensor_unused);
        k_thread_stack_space_get(&ble_thread_data, &ble_unused);

        sensor_metrics_t metrics;
        sensor_manager_get_metrics(&metrics);

        LOG_INF("[HEARTBEAT] Uptime: %llu s | StackUnused: Sns=%u B, BLE=%u B | Samples: Pushed=%u Dropped=%u",
                k_uptime_get() / 1000,
                (unsigned int)sensor_unused,
                (unsigned int)ble_unused,
                metrics.samples_pushed,
                metrics.samples_dropped);
    }
}

int main(void)
{
    LOG_INF("==================================================");
    LOG_INF("  nRF52840 BLE Sensor Hub — Zephyr RTOS v3.x");
    LOG_INF("  Firmware: %s | Built: %s %s", FW_BUILD_STRING, __DATE__, __TIME__);
    LOG_INF("==================================================");

    /* 0. Initialize Crash Logger and Record Reset Reason */
    int rc = crash_logger_init();
    if (rc == 0) {
        crash_logger_record_reset_reason();
        crash_logger_dump_logs();
    } else {
        LOG_ERR("Crash logger initialization failed (rc=%d)", rc);
    }

    /* Initialize Hardware Watchdog (10-second timeout) */
    s_watchdog_dev = DEVICE_DT_GET(DT_NODELABEL(wdt0));
    if (!device_is_ready(s_watchdog_dev)) {
        LOG_ERR("Watchdog device not ready");
    } else {
        struct wdt_timeout_cfg wdt_config = {
            .window.min = 0U,
            .window.max = 10000U, /* 10 seconds */
            .callback = NULL,
            .flags = WDT_FLAG_RESET_SOC,
        };
        s_watchdog_channel_id = wdt_install_timeout(s_watchdog_dev, &wdt_config);
        if (s_watchdog_channel_id < 0) {
            LOG_ERR("Watchdog install failed (rc=%d)", s_watchdog_channel_id);
        } else {
            rc = wdt_setup(s_watchdog_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
            if (rc == 0) {
                LOG_INF("Hardware watchdog enabled: 10s timeout, 5s feed interval");
            } else {
                LOG_ERR("Watchdog setup failed (rc=%d)", rc);
            }
        }
    }

    /* Initialize Onboard Status LED */
    if (s_led0.port && device_is_ready(s_led0.port)) {
        gpio_pin_configure_dt(&s_led0, GPIO_OUTPUT_ACTIVE);
    }

    /* 1. Initialize Sensor Subsystem */
    rc = sensor_manager_init();
    if (rc < 0) {
        LOG_ERR("Fatal: Sensor subsystem initialization failed (rc=%d)", rc);
    }

    /* 2. Initialize Bluetooth LE Subsystem */
    rc = ble_service_init();
    if (rc < 0) {
        LOG_ERR("Fatal: BLE subsystem initialization failed (rc=%d)", rc);
    }

    /* 3. Initialize MCUboot OTA Manager */
    rc = ota_manager_init();
    if (rc < 0) {
        LOG_WRN("OTA manager warning (rc=%d)", rc);
    }

    /* 4. Spawn Real-Time Operating System Threads */
    k_thread_create(&sensor_thread_data, sensor_stack,
                    K_THREAD_STACK_SIZEOF(sensor_stack),
                    sensor_thread_entry, NULL, NULL, NULL,
                    SENSOR_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&sensor_thread_data, "sensor_worker");

    k_thread_create(&ble_dispatch_thread_data, ble_stack,
                    K_THREAD_STACK_SIZEOF(ble_stack),
                    ble_dispatch_thread_entry, NULL, NULL, NULL,
                    BLE_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&ble_dispatch_thread_data, "ble_dispatcher");

    k_thread_create(&supervisor_thread_data, supervisor_stack,
                    K_THREAD_STACK_SIZEOF(supervisor_stack),
                    supervisor_thread_entry, NULL, NULL, NULL,
                    SUPERVISOR_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&supervisor_thread_data, "supervisor");

    LOG_INF("All threads spawned successfully. Kernel scheduler in control.");
    return 0;
}
