/*
 * SPDX-License-Identifier: Apache-2.0
 * Application: nRF52840 BLE Sensor Hub
 * File: crash_logger.c
 *
 * NVS-based crash and reset event recorder for the custom_sensor_node board.
 *
 * Uses Zephyr's Non-Volatile Storage driver (zephyr/fs/nvs.h) to persist
 * reset reasons, boot counts, and execution breadcrumbs across power cycles
 * and watchdog-triggered hard resets. Reads the hardware reset cause
 * (hwinfo_get_reset_cause) on boot and records it in NVS namespace "crash_log"
 * for post-mortem diagnostics via the RTT console.
 *
 * Storage layout (NVS namespace "crash_log"):
 *   Key 1 (NVS_KEY_CRASH_COUNT)       : uint32_t — cumulative abnormal reset count
 *   Key 2 (NVS_KEY_LAST_RESET_REASON) : uint32_t — reset-cause bitmask
 *   Key 3 (NVS_KEY_BOOT_COUNT)        : uint32_t — total boots (normal + abnormal)
 *   Key 4 (NVS_KEY_BREADCRUMB_0)      : char[BREADCRUMB_MAX_LEN] — newest crumb
 *   Key 5 (NVS_KEY_BREADCRUMB_1)      : char[BREADCRUMB_MAX_LEN] — one older
 *   Key 6 (NVS_KEY_BREADCRUMB_2)      : char[BREADCRUMB_MAX_LEN] — oldest
 */

#define MODULE_NAME crash_logger
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(crash_logger, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <stdint.h>
#include <string.h>
#include "crash_logger.h"

/*
 * NVS key identifiers. The NVS filesystem stores entries in a fixed-size
 * lookup table keyed by 16-bit identifiers. We use descriptive keys so that
 * data written by this module can be inspected or cleared independently of
 * other subsystems (e.g. BLE bonding keys in the same storage_partition).
 */
#define NVS_KEY_BOOT_COUNT        1
#define NVS_KEY_CRASH_COUNT       2
#define NVS_KEY_LAST_RESET_REASON 3
#define NVS_KEY_BREADCRUMB_0      4
#define NVS_KEY_BREADCRUMB_1      5
#define NVS_KEY_BREADCRUMB_2      6

/*
 * NVS mounts on a named partition. The board device tree labels the 64-KB
 * storage region "storage_partition"; the mount string must match that label.
 *
 * On nRF52840 the default nvs sector size is 4096 bytes and lookup cache is
 * limited to 8 entries. These values are safe for a 64-KB partition with a
 * 24-entry lookup table, and must not exceed flash page size for the
 * underlying flash device (nrf52840_qiaa uses 4-KB flash pages).
 */
#define NVS_MOUNT_LABEL "storage_partition"
#define NVS_SECTOR_SIZE 4096
#define NVS_LOOKUP_SIZE 24

static struct nvs_fs nvs_fs;
static k_mutex_t nvs_lock;

/* Rolling breadcrumb ring buffer (3 entries, circular) */
static char breadcrumb_ring[BREADCRUMB_SLOTS][BREADCRUMB_MAX_LEN];
static uint8_t breadcrumb_head;

/* Persistent boot-info snapshot populated at init time */
static crash_boot_info_t boot_info;

/**
 * @brief Open an exclusive handle to the NVS filesystem.
 *
 * The NVS driver serializes access with its own internal mutex, but the
 * application-level lock below guarantees that a read-modify-write sequence
 * (crash count / breadcrumb) is atomic with respect to other NVS users.
 */
static int nvs_lock_open(void)
{
	int rc;

	rc = k_mutex_lock(&nvs_lock, K_FOREVER);
	if (rc != 0) {
		LOG_ERR("nvs: failed to lock mutex (rc=%d)", rc);
		return rc;
	}

	rc = nvs_mount(&nvs_fs);
	if (rc != 0) {
		LOG_ERR("nvs: mount failed (rc=%d)", rc);
	}

	return rc;
}

static void nvs_lock_close(void)
{
	k_mutex_unlock(&nvs_lock);
}

/**
 * @brief Helper: write a uint32_t to NVS.
 */
static int nvs_write_u32(uint16_t key, uint32_t value)
{
	int rc = nvs_lock_open();
	if (rc != 0) {
		return rc;
	}

	ssize_t written = nvs_write(&nvs_fs, key, &value, sizeof(value));
	nvs_lock_close();

	if (written < (ssize_t)sizeof(value)) {
		LOG_WRN("nvs: u32 write failed (rc=%zd)", written);
		return -EIO;
	}
	return 0;
}

/**
 * @brief Helper: read a uint32_t from NVS.
 */
static int nvs_read_u32(uint16_t key, uint32_t *value)
{
	int rc = nvs_lock_open();
	if (rc != 0) {
		return rc;
	}

	ssize_t read_bytes = nvs_read(&nvs_fs, key, value, sizeof(*value));
	nvs_lock_close();

	if (read_bytes < (ssize_t)sizeof(*value)) {
		*value = 0; /* Blank NVS => treat as zero */
	}
	return 0;
}

/**
 * @brief Helper: write a breadcrumb string to NVS.
 */
static int nvs_write_breadcrumb(uint16_t key, const char *crumb)
{
	int rc = nvs_lock_open();
	if (rc != 0) {
		return rc;
	}

	ssize_t written = nvs_write(&nvs_fs, key, crumb,
				    strlen(crumb) + 1);
	nvs_lock_close();

	if (written < (ssize_t)(strlen(crumb) + 1)) {
		LOG_WRN("nvs: breadcrumb write failed (rc=%zd)", written);
		return -EIO;
	}
	return 0;
}

/**
 * @brief Helper: read a breadcrumb string from NVS.
 */
static int nvs_read_breadcrumb(uint16_t key, char *buffer, size_t len)
{
	int rc = nvs_lock_open();
	if (rc != 0) {
		return rc;
	}

	ssize_t read_bytes = nvs_read(&nvs_fs, key, buffer, len - 1);
	nvs_lock_close();

	if (read_bytes <= 0) {
		buffer[0] = '\0';
		return -ENOENT;
	}

	buffer[MIN((size_t)read_bytes, len - 1)] = '\0';
	return 0;
}

/**
 * @brief Determine whether the last reset was abnormal.
 *
 * A reset is abnormal if any of the following cause bits is set in the
 * hardware reset-cause register:
 *   - RESET_WATCHDOG : watchdog expired
 *   - RESET_CPU_LOCKUP : HardFault (non-recovering CPU lockup)
 *   - RESET_PIN : external pin reset (usually a forced recovery)
 *   - RESET_HARDWARE : hardware reset event
 *   - RESET_SOFTWARE : explicit sys_reboot() call
 *
 * Power-on reset (RESET_POR) and low-power wake (RESET_LOW_POWER_WAKE) are
 * normal boots and do not count as crashes.
 */
static bool is_abnormal_reset(uint32_t raw_cause)
{
	return (raw_cause & (RESET_WATCHDOG | RESET_CPU_LOCKUP | RESET_PIN |
			     RESET_HARDWARE | RESET_SOFTWARE)) != 0;
}

int crash_logger_init(void)
{
	int rc;
	struct flash_pages_info info;

	/* Initialize NVS subsystem on storage_partition. */
	k_mutex_init(&nvs_lock);

	/* Get the flash device from the storage_partition devicetree node */
	nvs_fs.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
	if (!device_is_ready(nvs_fs.flash_device)) {
		LOG_ERR("crash_logger: Flash device %s not ready",
			nvs_fs.flash_device->name);
		return -ENODEV;
	}

	nvs_fs.offset = FIXED_PARTITION_OFFSET(storage_partition);
	nvs_fs.sector_size = NVS_SECTOR_SIZE;
	/* The storage_partition is 64 KB, so 64 KB / 4096 B = 16 sectors. */
	nvs_fs.sector_count = 16;

	/* Get the flash page info and verify sector size */
	rc = flash_get_page_info_by_offs(nvs_fs.flash_device, nvs_fs.offset, &info);
	if (rc == 0) {
		nvs_fs.sector_size = info.size;
		nvs_fs.sector_count = FIXED_PARTITION_SIZE(storage_partition) / info.size;
	}

	rc = nvs_lock_open();
	if (rc != 0) {
		LOG_ERR("crash_logger: NVS init failed (rc=%d)", rc);
		return rc;
	}

	/* Read the raw reset-cause bits from the SoC hardware register. */
	uint32_t raw_cause = 0;
	rc = hwinfo_get_reset_cause(&raw_cause);
	if (rc != 0) {
		LOG_WRN("crash_logger: hwinfo_get_reset_cause failed (rc=%d)", rc);
		nvs_lock_close();
		return rc;
	}

	/*
	 * hwinfo_get_reset_cause() returns bits accumulated since power-on.
	 * For meaningful diagnostics of the *current* boot we clear the register
	 * immediately after reading so the next boot reports only the new cause.
	 */
	(void)hwinfo_clear_reset_cause();

	/*
	 * Fetch the previously stored boot count and crash count. If NVS was
	 * blank (flash erased / first boot), both values read back as 0,
	 * meaning this is the first boot and crash_count is correctly 0.
	 */
	uint32_t prev_crash_count = 0;
	uint32_t prev_boot_count = 0;

	nvs_read_u32(NVS_KEY_BOOT_COUNT, &prev_boot_count);
	nvs_read_u32(NVS_KEY_CRASH_COUNT, &prev_crash_count);

	uint32_t new_crash_count = prev_crash_count;
	uint32_t new_boot_count = prev_boot_count + 1;

	/* Increment the crash counter only for abnormal resets. */
	if (is_abnormal_reset(raw_cause)) {
		new_crash_count = prev_crash_count + 1;
	}

	/* Persist the new counters and the current reset reason. */
	nvs_write_u32(NVS_KEY_BOOT_COUNT, new_boot_count);
	nvs_write_u32(NVS_KEY_CRASH_COUNT, new_crash_count);
	nvs_write_u32(NVS_KEY_LAST_RESET_REASON, raw_cause);

	/* Populate the boot-info snapshot for the application. */
	boot_info.raw_reset_reason = raw_cause;
	boot_info.crash_count = new_crash_count;
	boot_info.boot_count = new_boot_count;
	boot_info.is_watchdog_reset = (raw_cause & RESET_WATCHDOG) != 0;
	boot_info.is_first_boot = (prev_boot_count == 0 && prev_crash_count == 0);

	nvs_lock_close();

	if (boot_info.is_first_boot) {
		LOG_INF("crash_logger: first boot (NVS was blank)");
	} else {
		LOG_INF("crash_logger: boot %u, crash count %u, reset cause 0x%08X",
			new_boot_count, new_crash_count, raw_cause);
	}

	return 0;
}

void crash_logger_store_breadcrumb(const char *crumb)
{
	if (crumb == NULL) {
		return;
	}

	size_t len = strlen(crumb);
	if (len >= BREADCRUMB_MAX_LEN) {
		len = BREADCRUMB_MAX_LEN - 1;
	}

	char temp[BREADCRUMB_MAX_LEN];
	memcpy(temp, crumb, len);
	temp[len] = '\0';

	/* Circular buffer: push newest to breadcrumb_head, shift the rest down. */
	for (int i = 0; i < BREADCRUMB_SLOTS; i++) {
		if (i == (int)breadcrumb_head) {
			memcpy(breadcrumb_ring[i], temp, sizeof(breadcrumb_ring[i]));
		} else {
			memcpy(breadcrumb_ring[i], breadcrumb_ring[(i + 1) % BREADCRUMB_SLOTS],
			       sizeof(breadcrumb_ring[i]));
		}
	}
	breadcrumb_head = (breadcrumb_head + 1) % BREADCRUMB_SLOTS;

	(void)nvs_write_breadcrumb(NVS_KEY_BREADCRUMB_0, breadcrumb_ring[0]);
	(void)nvs_write_breadcrumb(NVS_KEY_BREADCRUMB_1, breadcrumb_ring[1]);
	(void)nvs_write_breadcrumb(NVS_KEY_BREADCRUMB_2, breadcrumb_ring[2]);

	LOG_DBG("crash_logger: breadcrumb stored [%s]", temp);
}

const crash_boot_info_t *crash_logger_get_boot_info(void)
{
	return &boot_info;
}

void crash_logger_record_reset_reason(void)
{
	/* This function is now handled in crash_logger_init() */
	/* Kept for API compatibility - does nothing as recording happens at init */
}

void crash_logger_dump_logs(void)
{
	const crash_boot_info_t *info = crash_logger_get_boot_info();
	if (info == NULL) {
		LOG_WRN("crash_logger: no boot info available");
		return;
	}

	/*
	 * Decode the raw reset-cause bitmask into human-readable flag names.
	 * Multiple bits may be set simultaneously.
	 */
	const char *flags[] = { "PIN", "SOFTWARE", "BROWNOUT", "POR", "WATCHDOG",
				"DEBUG", "SECURITY", "LOW_POWER_WAKE", "CPU_LOCKUP",
				"PARITY", "PLL", "CLOCK", "HARDWARE", "USER",
				"TEMPERATURE" };
	char reason[128] = "";
	size_t reason_len = 0;

	for (int i = 0; i < 15; i++) {
		if (info->raw_reset_reason & BIT(i)) {
			size_t flen = strlen(flags[i]);
			if (reason_len + flen + 2 < sizeof(reason)) {
				if (reason_len > 0) {
					reason[reason_len++] = '|';
				}
				memcpy(reason + reason_len, flags[i], flen);
				reason_len += flen;
			}
		}
	}

	LOG_INF("==================================================");
	LOG_INF("  RESET / CRASH HISTORY");
	LOG_INF("==================================================");
	LOG_INF("  Boot count      : %u", info->boot_count);
	LOG_INF("  Crash count     : %u", info->crash_count);
	LOG_INF("  Last reset cause: 0x%08X (%s)",
		info->raw_reset_reason,
		reason[0] != '\0' ? reason : "NONE (POR / normal boot)");
	LOG_INF("  Watchdog reset  : %s",
		info->is_watchdog_reset ? "yes" : "no");
	LOG_INF("  First boot      : %s",
		info->is_first_boot ? "yes" : "no");

	/* Rolling breadcrumbs from latest-to-oldest. */
	for (int i = 0; i < BREADCRUMB_SLOTS; i++) {
		char crumb[BREADCRUMB_MAX_LEN];
		int rc = nvs_read_breadcrumb(NVS_KEY_BREADCRUMB_0 + i, crumb, sizeof(crumb));
		if (rc == 0) {
			LOG_INF("  Breadcrumb %d     : %s", i, crumb);
		}
	}
	LOG_INF("==================================================");
}

int crash_logger_clear_all(void)
{
	/*
	 * Keys are namespace-scoped to "crash_log" by our naming convention,
	 * so writing zeros/empty strings clears only crash-logger data.
	 */
	int rc = nvs_lock_open();
	if (rc != 0) {
		return rc;
	}

	/* Reset counters to zero. */
	nvs_write_u32(NVS_KEY_BOOT_COUNT, 0);
	nvs_write_u32(NVS_KEY_CRASH_COUNT, 0);
	nvs_write_u32(NVS_KEY_LAST_RESET_REASON, 0);

	/* Remove all breadcrumbs (write empty string to delete each entry). */
	for (uint8_t i = 0; i < BREADCRUMB_SLOTS; i++) {
		nvs_write(&nvs_fs, NVS_KEY_BREADCRUMB_0 + i, "", 1);
	}

	nvs_lock_close();

	return 0;
}

int crash_logger_decode_reset_reason(char *buffer, size_t len)
{
	const crash_boot_info_t *info = crash_logger_get_boot_info();
	if (info == NULL || buffer == NULL || len < 2) {
		return -EINVAL;
	}

	if (info->raw_reset_reason == 0) {
		return -ENOENT;
	}

	const char *flags[] = { "PIN", "SOFTWARE", "BROWNOUT", "POR", "WATCHDOG",
				"DEBUG", "SECURITY", "LOW_POWER_WAKE", "CPU_LOCKUP",
				"PARITY", "PLL", "CLOCK", "HARDWARE", "USER",
				"TEMPERATURE" };
	size_t offset = 0;

	for (int i = 0; i < 15; i++) {
		if (info->raw_reset_reason & BIT(i)) {
			size_t flen = strlen(flags[i]);
			if (offset + flen + 2 < len) {
				if (offset > 0) {
					buffer[offset++] = '|';
				}
				memcpy(buffer + offset, flags[i], flen);
				offset += flen;
			}
		}
	}
	buffer[offset] = '\0';

	return 0;
}
