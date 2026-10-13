# nRF52840 BLE Sensor Hub

[![Firmware CI](https://github.com/RishobanK/nrf52840-ble-sensor-hub/actions/workflows/firmware_ci.yml/badge.svg)](https://github.com/RishobanK/nrf52840-ble-sensor-hub/actions)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![Zephyr RTOS](https://img.shields.io/badge/Zephyr_RTOS-v3.x-green.svg)](https://zephyrproject.org/)
[![nRF Connect SDK](https://img.shields.io/badge/NCS-v2.6.x-blue)](https://www.nordicsemi.com/)

Production-grade edge telemetry firmware built for the **Nordic nRF52840 SoC** running **Zephyr RTOS**. Integrates environmental and inertial sensor telemetry over a custom **Bluetooth Low Energy (BLE 5.2)** GATT service, secured by **MCUboot dual-bank Over-the-Air (OTA)** updates and validated through automated **GitHub Actions CI**.

---

## 1. System Architecture & Concurrency Model

The firmware enforces clean task separation with preemptive priorities, lockless inter-thread queues, and mutex-protected shared caches.

```mermaid
graph TD
    subgraph Hardware_Peripherals [Hardware Sensors & Interfaces]
        BME280[Bosch BME280<br/>I2C1 @ 400kHz<br/>0x76]
        LSM6DSO[ST LSM6DSO IMU<br/>SPI1 @ 8MHz<br/>CS: P0.17]
        LED0[Status Heartbeat LED<br/>GPIO P0.13]
    end

    subgraph Zephyr_Kernel [Zephyr Real-Time Kernel]
        subgraph Thread_1 [Thread 1: Sensor Worker]
            T1_Desc[Priority: 5<br/>Period: 250 ms<br/>Stack: 2048 B]
        end

        subgraph Inter_Thread [Inter-Thread IPC]
            MSGQ[s_sensor_msgq<br/>k_msgq FIFO Queue<br/>Depth: 16 items]
            MUTEX[s_sensor_mutex<br/>k_mutex<br/>Atomic Cache Lock]
        end

        subgraph Thread_2 [Thread 2: BLE Dispatcher]
            T2_Desc[Priority: 6<br/>Event-Driven<br/>Stack: 2048 B]
        end

        subgraph Thread_3 [Thread 3: Supervisor]
            T3_Desc[Priority: 7<br/>Period: 5000 ms<br/>Stack: 1024 B]
        end
    end

    subgraph BLE_Subsystem [Bluetooth LE 5.2 GATT Profile]
        GATT_SVC[Custom Service<br/>UUID: 4a980001-...]
        CHRC_DATA[Telemetry Characteristic<br/>NOTIFY | READ<br/>UUID: 4a980002-...]
        CHRC_CFG[Config Characteristic<br/>WRITE | READ<br/>UUID: 4a980003-...]
        CHRC_STAT[Status Characteristic<br/>READ<br/>UUID: 4a980004-...]
    end

    %% Flow connections
    BME280 -->|TWIM1 DMA| T1_Desc
    LSM6DSO -->|SPIM1 DMA| T1_Desc
    T1_Desc -->|Enqueues Sample| MSGQ
    T1_Desc -->|Updates Cache| MUTEX

    MSGQ -->|k_msgq_get blocks| T2_Desc
    T2_Desc -->|GATT Notify| CHRC_DATA

    MUTEX -.->|Synchronous Read| CHRC_DATA
    CHRC_CFG -.->|Runtime Config Update| T1_Desc

    T3_Desc -->|Flashes| LED0
    T3_Desc -->|Monitors Stack Watermark| Thread_1
    T3_Desc -->|Monitors Stack Watermark| Thread_2
    T3_Desc -->|Confirms MCUboot Slot| MCUBOOT[MCUboot Bootloader<br/>Dual-Bank A/B Swap]
```

---

## 2. Hardware Pinout Table

Target evaluation platform: **Nordic Semiconductor nRF52840-DK (PCA10056)**.

| Peripheral | Signal / Function | nRF52840 Pin | Physical Header / Test Point | Electrical Specs |
|---|---|---|---|---|
| **Bosch BME280** | I2C1 SDA | **P0.26** | Arduino Header D14 | 3.3V, 4.7kΩ pull-up enabled |
| **Bosch BME280** | I2C1 SCL | **P0.27** | Arduino Header D15 | 3.3V, 4.7kΩ pull-up enabled |
| **ST LSM6DSO** | SPI1 SCK | **P0.31** | Arduino Header D13 | 8 MHz max, Mode 3 (CPOL=1, CPHA=1) |
| **ST LSM6DSO** | SPI1 MOSI | **P0.30** | Arduino Header D11 | 3.3V push-pull |
| **ST LSM6DSO** | SPI1 MISO | **P0.29** | Arduino Header D12 | 3.3V input |
| **ST LSM6DSO** | SPI1 CS | **P0.17** | Arduino Header D10 | Active-low GPIO chip-select |
| **ST LSM6DSO** | Data Ready INT | **P0.19** | Arduino Header D2 | Active-high rising-edge interrupt |
| **Status LED** | LED1 Heartbeat | **P0.13** | Onboard LED1 | Active-low |
| **BLE Activity**| LED2 Connected | **P0.14** | Onboard LED2 | Active-low |
| **SEGGER RTT** | Debug Logging | SWD Interface | Standard 10-pin Tag-Connect | High-speed zero-overhead logging |

---

## 3. Directory Layout

```
.
├── .github/workflows/
│   └── firmware_ci.yml         # GitHub Actions build & test workflow
├── app/
│   ├── CMakeLists.txt          # Zephyr application CMake definition
│   ├── prj.conf                # Zephyr Kconfig subsystem flags
│   ├── app.overlay             # Devicetree overlay (I2C1 & SPI1 buses)
│   └── src/
│       ├── main.c              # Thread spawning, queues, and supervisor
│       ├── sensor_manager.c    # Sensor bus acquisition and caching
│       ├── ble_service.c       # Custom GATT service, advertising, notify
│       └── ota_manager.c       # MCUboot self-test and confirmation logic
├── include/
│   ├── sensor_manager.h        # Sensor data structures & queue headers
│   ├── ble_service.h           # GATT UUIDs and configuration structs
│   └── ota_manager.h           # Bootloader lifecycle interface
├── drivers/
│   └── custom_sensor_bme280.c  # Register-level I2C driver with fixed-point math
├── tests/
│   └── test_sensor_ringbuf.c   # Unit tests for queue boundary and overflow
├── docs/
│   ├── architecture.md         # Detailed software architecture specification
│   └── memory_map.md           # 1 MB Flash partition table and SRAM map
├── build_and_sign.sh           # Automated build, imgtool signing, and packaging
└── README.md                   # Project overview and developer instructions
```

---

## 4. Build & Execution Instructions

### Prerequisites
- [nRF Connect SDK v2.6.x](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/installation.html) or Zephyr RTOS v3.5+
- `west` meta-tool (`pip install west`)
- GNU Arm Embedded Toolchain (`arm-zephyr-eabi`)
- `imgtool` Python utility (`pip install imgtool`)
- `nrf-command-line-tools` (for `nrfjprog`)

### Method 1: Building with `west` (Recommended)

1. **Initialize the workspace** (if not already done):
   ```bash
   west init -l app
   west update
   ```

2. **Compile the application**:
   ```bash
   west build -b nrf52840dk_nrf52840 app -d build
   ```

3. **Flash the board via J-Link**:
   ```bash
   west flash -d build
   ```

4. **Monitor debug output via SEGGER RTT**:
   ```bash
   JLinkRTTViewer --device NRF52840_XXAA --interface SWD --speed 4000
   ```

### Method 2: Building via Native `cmake`

```bash
mkdir build && cd build
cmake -GNinja -DBOARD=nrf52840dk_nrf52840 ../app
ninja
```

### Method 3: Automated Signing & Packaging

To generate an MCUboot-compliant, signed secondary-slot upgrade binary:
```bash
chmod +x build_and_sign.sh
./build_and_sign.sh nrf52840dk_nrf52840 1.0.0
```
This produces `build/zephyr/zephyr.signed.bin` ready for OTA transfer.

---

## 5. Running Unit Tests

Native host unit tests can be executed without target hardware:
```bash
gcc -Wall -Wextra -O2 tests/test_sensor_ringbuf.c -o test_ringbuf
./test_ringbuf
```

Output:
```
Running sensor_ringbuf unit tests...
  [PASS] test_ringbuf_empty_initial_state
  [PASS] test_ringbuf_fifo_ordering
  [PASS] test_ringbuf_overrun_drop_oldest
All 3 unit tests passed successfully!
```

---

## 6. BLE GATT Telemetry Verification

Connect to the peripheral using the **nRF Connect for Mobile** app (iOS / Android) or Nordic's desktop BLE tool:
1. Scan for advertised name: `nRF52840_SensorHub`.
2. Connect and bond.
3. Discover Custom Service `4a980001-1cc4-414e-98fe-57a00182f9d8`.
4. Enable Notifications on Characteristic `4a980002-...` to observe real-time 4 Hz binary sensor updates.

---

## 7. License
This project is licensed under the Apache 2.0 License. See [LICENSE](LICENSE) for details.
