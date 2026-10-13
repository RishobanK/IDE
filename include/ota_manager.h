/*
 * SPDX-License-Identifier: Apache-2.0
 * Header: ota_manager.h
 * MCUboot DFU lifecycle management, dual-bank validation, and rollback safety.
 */

#ifndef OTA_MANAGER_H_
#define OTA_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0
#define FW_BUILD_STRING  "v1.0.0-prod"

/**
 * @brief Initialize MCUboot status check, verify active slot, and begin self-test timer.
 * @return 0 on success, negative error code on failure.
 */
int ota_manager_init(void);

/**
 * @brief Mark the currently running image as confirmed (permanent boot).
 * Prevents MCUboot from reverting to the previous firmware slot on watchdog reset.
 * @return 0 on success, negative error code on failure.
 */
int ota_manager_confirm_image(void);

/**
 * @brief Check if the currently running image has already been confirmed.
 * @return true if permanent, false if running in test/provisional mode.
 */
bool ota_manager_is_image_confirmed(void);

/**
 * @brief Log hardware partition layout, flash addresses, and MCUboot slot status.
 */
void ota_manager_log_system_info(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H_ */
