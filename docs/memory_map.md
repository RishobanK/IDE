# Nordic nRF52840 Memory Map & Partition Table

## 1. Physical Silicon Hardware Resources

- **SoC**: Nordic Semiconductor nRF52840 (ARM Cortex-M4F @ 64 MHz)
- **Non-Volatile Flash**: 1024 KB (1 MB) Internal NOR Flash (4 KB page size, 256 pages)
- **Static RAM (SRAM)**: 256 KB Internal High-Speed SRAM

---

## 2. Flash Memory Partition Table (MCUboot Managed)

The internal 1024 KB flash is partitioned into five distinct regions defined in the board devicetree:

```
Address Range            Size      Partition Name       Purpose / Description
--------------------------------------------------------------------------------------------------
0x0000_0000 - 0x0000_BFFF  48 KB     mcuboot              MCUboot First-Stage Bootloader executable
0x0000_C000 - 0x0007_9FFF  440 KB    slot0_partition      Primary Active Application Slot
0x0007_A000 - 0x000E_7FFF  440 KB    slot1_partition      Secondary Candidate / OTA Download Slot
0x000E_8000 - 0x000E_FFFF  32 KB     scratch_partition    Sector Swap Scratch Space (for MCUboot)
0x000F_0000 - 0x000F_FFFF  64 KB     storage_partition    Non-Volatile Storage (NVS) & BLE Bonding Keys
--------------------------------------------------------------------------------------------------
Total Flash:               1024 KB
```

### Flash Layout Diagram
```
+---------------------------------------------------------------------------+
| 0x0000_0000  [mcuboot] Bootloader Core (48 KB)                            |
+---------------------------------------------------------------------------+
| 0x0000_C000  [slot0_partition] Primary Slot (Active App Image) (440 KB)   |
|              ├── Vector Table Relocation Base                             |
|              ├── Code (.text) & Read-Only Data (.rodata)                  |
|              └── MCUboot Trailer (Flags, Image Hash, Magic)               |
+---------------------------------------------------------------------------+
| 0x0007_A000  [slot1_partition] Secondary Slot (OTA Candidate) (440 KB)    |
|              └── Staging area for incoming firmware binaries via BLE      |
+---------------------------------------------------------------------------+
| 0x000E_8000  [scratch_partition] Swap Scratch Area (32 KB)                |
|              └── Temporary sector buffer used during swap operations      |
+---------------------------------------------------------------------------+
| 0x000F_0000  [storage_partition] NVS / Settings / BLE Keys (64 KB)        |
|              └── Flash Data Storage (FDS) for bonding & persistent config |
+---------------------------------------------------------------------------+
```

---

## 3. SRAM Memory Allocation (256 KB Total)

```
SRAM Region                 Allocated Size     Purpose
--------------------------------------------------------------------------------------------------
RAM Vector Table            1 KB (0x400)       ARM Cortex-M4 Relocated Vector Table
Application .data / .bss    ~24 KB             Global variables, static structs, BLE GATT tables
Zephyr Kernel Heap          4 KB               CONFIG_HEAP_MEM_POOL_SIZE
BLE SoftDevice Controller   ~32 KB             Nordic Controller RX/TX buffers (251-byte ACL)
Thread Stacks:
  - Main Thread             2048 B             CONFIG_MAIN_STACK_SIZE
  - System Workqueue        2048 B             CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE
  - sensor_worker Thread    2048 B             sensor_stack (I2C/SPI execution)
  - ble_dispatcher Thread   2048 B             ble_stack (GATT serialization)
  - supervisor Thread       1024 B             supervisor_stack (Diagnostics)
  - Idle Thread             512 B              CONFIG_IDLE_STACK_SIZE
Message Queue Ringbuffer    640 B              s_sensor_msgq (16 items * 40 bytes)
Free Unallocated RAM        ~186 KB            Available for heap expansion / dynamic buffers
--------------------------------------------------------------------------------------------------
Total RAM:                  256 KB
```
