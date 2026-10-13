#!/usr/bin/env bash
# ==============================================================================
# nRF52840 BLE Sensor Hub — Automated Build, Sign, and DFU Package Script
# ==============================================================================

set -euo pipefail

BOARD="${1:-nrf52840dk_nrf52840}"
BUILD_DIR="build"
APP_DIR="app"
VERSION="${2:-1.0.0}"
KEY_FILE="keys/mcuboot_ecdsa_p256.pem"

echo "=========================================================="
echo " Building nRF52840 BLE Sensor Hub Firmware"
echo " Board   : ${BOARD}"
echo " Version : ${VERSION}"
echo "=========================================================="

# 1. Ensure signing key exists (generate development key if missing)
if [ ! -f "${KEY_FILE}" ]; then
    echo "[!] Signing key ${KEY_FILE} not found. Generating development ECDSA P-256 key..."
    mkdir -p keys
    if command -v imgtool &> /dev/null; then
        imgtool keygen -k "${KEY_FILE}" -t ecdsa-p256
    else
        echo "[!] 'imgtool' not installed. Please install via: pip install imgtool"
        echo "[!] Using self-signed fallback key placeholder."
        touch "${KEY_FILE}"
    fi
fi

# 2. Build application with MCUboot awareness using west
echo "[*] Executing west build..."
west build -b "${BOARD}" "${APP_DIR}" -d "${BUILD_DIR}" -- -DCONFIG_BOOTLOADER_MCUBOOT=y

# 3. Check for binary output
ZEPHYR_BIN="${BUILD_DIR}/zephyr/zephyr.bin"
if [ ! -f "${ZEPHYR_BIN}" ]; then
    echo "[-] Error: ${ZEPHYR_BIN} not found after build!"
    exit 1
fi

echo "[+] Compilation successful: ${ZEPHYR_BIN}"

# 4. Sign binary with MCUboot imgtool for secondary slot OTA
SIGNED_BIN="${BUILD_DIR}/zephyr/zephyr.signed.bin"
SIGNED_HEX="${BUILD_DIR}/zephyr/zephyr.signed.hex"

if command -v imgtool &> /dev/null && [ -s "${KEY_FILE}" ]; then
    echo "[*] Signing firmware binary with MCUboot imgtool..."
    imgtool sign \
        --key "${KEY_FILE}" \
        --header-size 0x200 \
        --align 4 \
        --version "${VERSION}" \
        --slot-size 0x6E000 \
        --pad \
        "${ZEPHYR_BIN}" \
        "${SIGNED_BIN}"

    echo "[+] Signed binary created: ${SIGNED_BIN}"
else
    echo "[!] Skipping signing step (imgtool missing or key empty). Using default merged output."
fi

echo "=========================================================="
echo " Build and Packaging Complete!"
echo " Flash using: west flash -d ${BUILD_DIR}"
echo "=========================================================="
