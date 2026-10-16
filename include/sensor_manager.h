/*
 * SPDX-License-Identifier: Apache-2.0
 * Header: sensor_manager.h
 * Thread-safe sensor acquisition and data distribution interface.
 */

#ifndef SENSOR_MANAGER_H_
#define SENSOR_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Packed sensor telemetry payload representing a consolidated sample.
 */
typedef struct __attribute__((packed)) {
    /* Environmental data (BME280) */
    int32_t  temperature_centi_c;   /* Temperature in 0.01 deg C (e.g., 2450 = 24.50 C) */
    uint32_t pressure_pa;           /* Barometric pressure in Pascals */
    uint32_t humidity_centi_pct;    /* Relative Humidity in 0.01 % RH (e.g., 5530 = 55.30%) */

    /* 6-DoF Motion data (LSM6DSO) */
    int16_t  accel_x_mg;            /* Acceleration X in milli-g (9.81m/s^2 / 1000) */
    int16_t  accel_y_mg;            /* Acceleration Y in milli-g */
    int16_t  accel_z_mg;            /* Acceleration Z in milli-g */
    int16_t  gyro_x_mdps;           /* Angular rate X in milli-degrees per second */
    int16_t  gyro_y_mdps;           /* Angular rate Y in milli-degrees per second */
    int16_t  gyro_z_mdps;           /* Angular rate Z in milli-degrees per second */

    /* Metadata */
    uint32_t timestamp_ms;          /* System uptime at sample capture */
    uint16_t sequence_num;          /* Monotonically increasing sequence number */
} sensor_reading_t;

/**
 * @brief Circular ring buffer status and metrics.
 */
typedef struct {
    uint32_t samples_pushed;
    uint32_t samples_dropped;
    uint32_t overrun_count;
} sensor_metrics_t;

/**
 * @brief Initialize all sensor hardware buses (I2C1, SPI1) and synchronization primitives.
 * @return 0 on success, negative error code on failure.
 */
int sensor_manager_init(void);

/**
 * @brief Sample hardware sensors, update shared cache, and enqueue into message queue.
 * @param out_reading Optional pointer to store the newly acquired sample.
 * @return 0 on success, negative error code on failure.
 */
int sensor_manager_sample_all(sensor_reading_t *out_reading);

/**
 * @brief Atomically retrieve the most recent cached reading.
 * @param out_reading Destination pointer for the cached sample.
 * @return 0 on success, -EINVAL if pointer is null, -ENODATA if no sample available.
 */
int sensor_manager_get_latest(sensor_reading_t *out_reading);

/**
 * @brief Get the Zephyr message queue handle used for inter-thread sensor dispatch.
 * @return Pointer to struct k_msgq.
 */
struct k_msgq *sensor_manager_get_msgq(void);

/**
 * @brief Retrieve current metrics for sensor pipeline health monitoring.
 * @param metrics Pointer to destination struct.
 */
void sensor_manager_get_metrics(sensor_metrics_t *metrics);

/**
 * @brief Suspend I2C and SPI buses to save power between sampling intervals.
 * @return 0 on success, negative error code on failure.
 */
int sensor_manager_suspend_buses(void);

/**
 * @brief Resume I2C and SPI buses for active sensor transactions.
 * @return 0 on success, negative error code on failure.
 */
int sensor_manager_resume_buses(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_MANAGER_H_ */
