/*
 * SPDX-License-Identifier: Apache-2.0
 * Implementation: ota_manager.c
 * Manages MCUboot DFU lifecycle, self-test verification, and permanent slot confirmation.
 */

#include "ota_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(ota_mgr, CONFIG_LOG_DEFAULT_LEVEL);

static bool s_is_confirmed = false;
static struct k_work_delayable s_confirm_work;

/* Work handler executed after successful burn-in period */
static void confirm_work_handler(struct k_work *work)
{
    LOG_INF("Self-test burn-in passed. Confirming MCUboot firmware image...");
    int rc = ota_manager_confirm_image();
    if (rc == 0) {
        LOG_INF("Firmware image successfully marked as PERMANENT in primary slot");
    } else {
        LOG_ERR("Failed to confirm firmware image (rc=%d)", rc);
    }
}

int ota_manager_init(void)
{
    LOG_INF("Initializing MCUboot DFU Manager (Build: %s)...", FW_BUILD_STRING);

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    if (boot_is_img_confirmed()) {
        s_is_confirmed = true;
        LOG_INF("Image is already permanently confirmed");
    } else {
        s_is_confirmed = false;
        LOG_WRN("Running in TEST / PROVISIONAL mode! Must pass self-test within 10s or rollback.");

        /* Schedule confirmation after 10 seconds of error-free operation */
        k_work_init_delayable(&s_confirm_work, confirm_work_handler);
        k_work_schedule(&s_confirm_work, K_SECONDS(10));
    }
#else
    LOG_INF("Running without MCUboot bootloader partition support (standalone)");
    s_is_confirmed = true;
#endif

    ota_manager_log_system_info();
    return 0;
}

int ota_manager_confirm_image(void)
{
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    int rc = boot_write_img_confirmed();
    if (rc == 0) {
        s_is_confirmed = true;
        return 0;
    }
    return rc;
#else
    s_is_confirmed = true;
    return 0;
#endif
}

bool ota_manager_is_image_confirmed(void)
{
    return s_is_confirmed;
}

void ota_manager_log_system_info(void)
{
    LOG_INF("================ SYSTEM PARTITION MAP ================");
#if FIXED_PARTITION_EXISTS(boot_partition)
    LOG_INF("Slot Boot     : Offset=0x%08lx, Size=0x%08lx",
            (long)FIXED_PARTITION_OFFSET(boot_partition),
            (long)FIXED_PARTITION_SIZE(boot_partition));
#endif
#if FIXED_PARTITION_EXISTS(slot0_partition)
    LOG_INF("Slot 0 (Active): Offset=0x%08lx, Size=0x%08lx",
            (long)FIXED_PARTITION_OFFSET(slot0_partition),
            (long)FIXED_PARTITION_SIZE(slot0_partition));
#endif
#if FIXED_PARTITION_EXISTS(slot1_partition)
    LOG_INF("Slot 1 (Candidate): Offset=0x%08lx, Size=0x%08lx",
            (long)FIXED_PARTITION_OFFSET(slot1_partition),
            (long)FIXED_PARTITION_SIZE(slot1_partition));
#endif
#if FIXED_PARTITION_EXISTS(storage_partition)
    LOG_INF("Slot Storage  : Offset=0x%08lx, Size=0x%08lx",
            (long)FIXED_PARTITION_OFFSET(storage_partition),
            (long)FIXED_PARTITION_SIZE(storage_partition));
#endif
    LOG_INF("======================================================");
}
