/*
 * SPDX-License-Identifier: Apache-2.0
 * Header: crash_logger.h
 *
 * Non-Volatile Storage (NVS) based crash and reset event recorder.
 *
 * Records hardware reset reasons, crash counts, and execution breadcrumbs
 * into the storage_partition on every boot. Data survives power cycles and
 * watchdog-triggered hard resets, enabling post-mortem field diagnostics
 * without a physical debugger attached.
 *
 * Storage layout (NVS namespace "crash_log"):
 *   Key 1 (NVS_KEY_CRASH_COUNT)      : uint32_t — total abnormal reset count
 *   Key 2 (NVS_KEY_LAST_RESET_REASON): uint32_t — bitmask from hwinfo API
 *   Key 3 (NVS_KEY_BOOT_COUNT)       : uint32_t — every-boot monotonic counter
 *   Key 4 (NVS_KEY_BREADCRUMB_0)     : char[BREADCRUMB_MAX_LEN] — newest crumb
 *   Key 5 (NVS_KEY_BREADCRUMB_1)     : char[BREADCRUMB_MAX_LEN] — one older
 *   Key 6 (NVS_KEY_BREADCRUMB_2)     : char[BREADCRUMB_MAX_LEN] — oldest
 */

#ifndef CRASH_LOGGER_H_
#define CRASH_LOGGER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum string length for a breadcrumb entry (including null terminator) */
#define BREADCRUMB_MAX_LEN  48

/* Number of rolling breadcrumb slots stored in NVS */
#define BREADCRUMB_SLOTS    3

/**
 * @brief Decoded reset reason flags.
 *
 * Maps Zephyr's hwinfo reset cause bits to human-readable enum values.
 * Multiple bits may be set simultaneously.
 */
typedef enum {
    RESET_REASON_UNKNOWN    = 0x00, /* No bits set in RESETREAS — cold start */
    RESET_REASON_PIN        = 0x01, /* External pin (RESET line asserted) */
    RESET_REASON_WATCHDOG   = 0x02, /* Hardware watchdog timeout expired */
    RESET_REASON_SOFT       = 0x04, /* Software-triggered sys_reboot() call */
    RESET_REASON_CPU_LOCKUP = 0x08, /* CPU lockup (ARM HardFault non-recovery) */
    RESET_REASON_BROWNOUT   = 0x10, /* Power supply voltage drop (BOD) */
} reset_reason_flags_t;

/**
 * @brief Complete snapshot of the system's crash state at boot time.
 * Populated by crash_logger_init() and available for inspection.
 */
typedef struct {
    uint32_t raw_reset_reason;      /* Raw bitmask from hwinfo_get_reset_cause() */
    uint32_t crash_count;           /* Cumulative abnormal reset count (NVS) */
    uint32_t boot_count;            /* Total boots (normal + abnormal) */
    bool     is_watchdog_reset;     /* Convenience flag: WDT bit was set */
    bool     is_first_boot;         /* True if NVS was blank (erased flash) */
} crash_boot_info_t;

/**
 * @brief Initialize the crash logger subsystem.
 *
 * Must be called once at the very start of main(), before any other
 * subsystem initializes.
 *
 * Actions performed:
 *   1. Mount the NVS filesystem on storage_partition.
 *   2. Read and clear hardware reset reason register via hwinfo API.
 *   3. Increment boot counter and crash counter (if reset was abnormal) and
 *      record the decoded reset reason.
 *   4. Write the new reset state to NVS.
 *   5. Populate the boot_info struct returned by crash_logger_get_boot_info().
 *
 * @return 0 on success, negative Zephyr error code on NVS init failure.
 */
int crash_logger_init(void);

/**
 * @brief Record the current hardware reset reason into NVS.
 *
 * Reads the hardware reset cause (hwinfo_get_reset_cause) and persists it
 * together with the incremented boot and crash counters to NVS. This allows
 * the reset reason to be recorded explicitly at an arbitrary point in the
 * application (e.g. from an exception handler or before a forced reboot)
 * instead of only implicitly at boot.
 */
void crash_logger_record_reset_reason(void);

/**
 * @brief Print the complete stored crash history to the RTT console.
 *
 * Outputs boot count, crash count, last reset reason (with decoded flag names),
 * and all stored breadcrumb strings. Call after crash_logger_init().
 * Alias for crash_logger_dump_logs().
 */
void crash_logger_dump_logs(void);

/**
 * @brief Store an execution breadcrumb string in NVS rolling buffer.
 *
 * Breadcrumbs are short string tags (max BREADCRUMB_MAX_LEN bytes) written
 * before entering a risky or non-deterministic operation. On an unexpected
 * reset, the stored breadcrumbs identify which code region was executing.
 *
 * Example usage:
 *   crash_logger_store_breadcrumb("flash_erase_start");
 *   flash_erase(...);
 *   crash_logger_store_breadcrumb("flash_erase_done");
 *
 * @param crumb Null-terminated string, max BREADCRUMB_MAX_LEN chars.
 */
void crash_logger_store_breadcrumb(const char *crumb);

/**
 * @brief Retrieve the boot information struct populated at init time.
 * @return Pointer to the static crash_boot_info_t, valid after crash_logger_init().
 */
const crash_boot_info_t *crash_logger_get_boot_info(void);

/**
 * @brief Decode the stored reset reason into human-readable text.
 *
 * Populates `buffer` with a pipe-separated list of cause names
 * (e.g. "WATCHDOG|CPU_LOCKUP"). The buffer must be at least 128 bytes.
 * @return 0 on success, -ENOENT if no reset cause is stored.
 */
int crash_logger_decode_reset_reason(char *buffer, size_t len);

/**
 * @brief Clear all crash records from NVS.
 * Resets crash count, boot count, reset reason, and all breadcrumbs to zero.
 * Intended for factory reset or post-debugging cleanup.
 * @return 0 on success, negative error code on failure.
 */
int crash_logger_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_LOGGER_H_ */
