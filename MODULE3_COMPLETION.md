# Module 3 Completion: System Hardening (Hardware Watchdog & NVS Crash Logger)

## Summary
Module 3 has been successfully integrated into the nRF52840 BLE Sensor Hub project. All required changes have been implemented and are ready for compilation.

## Changes Completed

### 1. Crash Logger Files
✅ **Verified existing files:**
- `include/crash_logger.h` - Complete API with reset reason tracking, breadcrumb storage, and NVS persistence
- `app/src/crash_logger.c` - Full implementation with NVS integration and hwinfo support

### 2. Configuration Updates (`app/prj.conf`)
✅ **Added NVS configuration:**
```kconfig
CONFIG_NVS=y
```

**Existing configurations already present:**
- `CONFIG_HWINFO=y` (line 14)
- `CONFIG_FLASH=y` (line 63)
- `CONFIG_FLASH_MAP=y` (line 65)
- `CONFIG_WATCHDOG=y` (line 72)
- `CONFIG_WDT_DISABLE_AT_BOOT=n` (line 73)

### 3. Build System Updates (`app/CMakeLists.txt`)
✅ **Added crash_logger.c to build:**
```cmake
target_sources(app PRIVATE
    src/main.c
    src/sensor_manager.c
    src/ble_service.c
    src/ota_manager.c
    src/crash_logger.c        # ← Added
    ../drivers/custom_sensor_bme280.c
)
```

### 4. Main Application Updates (`app/src/main.c`)

✅ **Added includes:**
```c
#include <zephyr/drivers/watchdog.h>
#include "crash_logger.h"
```

✅ **Added watchdog device globals:**
```c
static const struct device *s_watchdog_dev;
static int s_watchdog_channel_id;
```

✅ **Added crash logger initialization in main():**
```c
/* 0. Initialize Crash Logger and Record Reset Reason */
int rc = crash_logger_init();
if (rc == 0) {
    crash_logger_record_reset_reason();
    crash_logger_dump_logs();
} else {
    LOG_ERR("Crash logger initialization failed (rc=%d)", rc);
}
```

✅ **Added hardware watchdog initialization in main():**
```c
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
```

✅ **Added watchdog feeding in supervisor_thread_entry():**
```c
/* Feed the hardware watchdog to prevent timeout reset */
if (s_watchdog_dev && device_is_ready(s_watchdog_dev)) {
    int rc = wdt_feed(s_watchdog_dev, s_watchdog_channel_id);
    if (rc != 0) {
        LOG_ERR("Watchdog feed failed (rc=%d)", rc);
    }
}
```

### 5. Crash Logger Implementation Updates (`app/src/crash_logger.c`)

✅ **Added missing functions:**
- `crash_logger_record_reset_reason()` - Stub for API compatibility (recording happens in init)
- `crash_logger_dump_logs()` - Renamed from `crash_logger_log_reset_reason()` to match header
- `crash_logger_decode_reset_reason()` - Full implementation to decode reset cause bitmask

## Features Implemented

### Hardware Watchdog
- **Timeout:** 10 seconds
- **Feed interval:** 5 seconds (in supervisor thread)
- **Behavior:** System reset on timeout (WDT_FLAG_RESET_SOC)
- **Debug mode:** Pauses when debugger is attached (WDT_OPT_PAUSE_HALTED_BY_DBG)

### NVS Crash Logger
- **Storage partition:** 64KB dedicated flash region
- **Persistent data:**
  - Boot count (total boots)
  - Crash count (abnormal resets only)
  - Last reset reason (hardware register bitmask)
  - 3 rolling breadcrumb slots for execution tracking
- **Reset reasons tracked:**
  - Watchdog timeout
  - CPU lockup (HardFault)
  - External pin reset
  - Software reboot
  - Brownout
  - Power-on reset
  - Low-power wake

### Execution Flow
1. **On boot:** Crash logger initializes first, reads hwinfo reset cause
2. **Increment counters:** Boot count always increments, crash count only on abnormal resets
3. **Persist to NVS:** All data written to non-volatile storage
4. **Display logs:** Reset history and breadcrumbs printed via RTT console
5. **Watchdog start:** 10-second hardware watchdog installed and armed
6. **Normal operation:** Supervisor thread feeds watchdog every 5 seconds
7. **On hang/crash:** Watchdog expires → system resets → crash logged on next boot

## Build Instructions

### Using Docker (Recommended - matches CI environment)
```bash
docker run --rm -v "$(pwd):/workspace" -w /workspace \
  nordicsemi/nrfconnect-sdk:v2.6-branch \
  bash -c "west build -b custom_sensor_node app -d build"
```

### Using Local Zephyr/NCS Installation
```bash
west build -b custom_sensor_node app -d build
```

### Using Build Script
```bash
./build_and_sign.sh custom_sensor_node 1.0.0
```

## Verification Checklist

- [x] crash_logger.h and crash_logger.c are present
- [x] prj.conf includes CONFIG_NVS=y and other required configs
- [x] CMakeLists.txt includes src/crash_logger.c
- [x] main.c includes watchdog.h and crash_logger.h
- [x] main() calls crash_logger_init(), record_reset_reason(), dump_logs()
- [x] main() installs 10-second hardware watchdog
- [x] supervisor_thread feeds watchdog every 5 seconds
- [x] All required functions implemented in crash_logger.c

## Testing Notes

To test the crash logger and watchdog functionality:

1. **First boot test:** Flash firmware and observe RTT console showing:
   - First boot flag = yes
   - Boot count = 1
   - Crash count = 0
   - Reset reason = POR (power-on reset)

2. **Watchdog test:** Comment out `wdt_feed()` in supervisor thread and observe:
   - System resets after ~10 seconds
   - Next boot shows WATCHDOG reset reason
   - Crash count incremented

3. **Breadcrumb test:** Add `crash_logger_store_breadcrumb("test_point")` before risky operations

4. **Normal operation:** Verify watchdog is fed every 5 seconds in supervisor heartbeat logs

## File Modifications Summary

| File | Status | Changes |
|------|--------|---------|
| `app/prj.conf` | Modified | Added CONFIG_NVS=y |
| `app/CMakeLists.txt` | Modified | Added src/crash_logger.c to build |
| `app/src/main.c` | Modified | Added crash logger + watchdog init and feeding |
| `app/src/crash_logger.c` | Modified | Fixed function names and added missing implementations |
| `include/crash_logger.h` | No change | Already complete |

## Next Steps

1. Build the project using one of the methods above
2. Flash to custom_sensor_node hardware or nrf52840dk_nrf52840
3. Monitor RTT console for crash logger output on boot
4. Verify watchdog feeding in supervisor heartbeat logs (every 5 seconds)
5. Test crash recovery by inducing a watchdog timeout

---
**Module 3 Status:** ✅ Implementation Complete - Ready for Build
**Date:** 2026-10-16
