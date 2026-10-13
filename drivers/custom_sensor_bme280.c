/*
 * SPDX-License-Identifier: Apache-2.0
 * Driver: custom_sensor_bme280.c
 * Register-level driver for Bosch BME280 Environmental Sensor over I2C.
 * Implements 32-bit fixed-point compensation math conforming to Bosch specification.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bme280_custom, CONFIG_LOG_DEFAULT_LEVEL);

/* --- Register Map --- */
#define BME280_REG_ID              0xD0
#define BME280_CHIP_ID_VAL         0x60
#define BME280_REG_RESET           0xE0
#define BME280_RESET_VAL           0xB6
#define BME280_REG_CTRL_HUM        0xF2
#define BME280_REG_STATUS          0xF3
#define BME280_REG_CTRL_MEAS       0xF4
#define BME280_REG_CONFIG          0xF5
#define BME280_REG_DATA_START      0xF7
#define BME280_DATA_BURST_LEN      8

/* Calibration Register Banks */
#define BME280_REG_CALIB_00_25     0x88
#define BME280_REG_CALIB_26_41     0xE1

/* Calibration parameter structure */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;

    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;

    int32_t  t_fine; /* Intermediate fine temperature for pressure/humidity compensation */
} bme280_calib_t;

static const struct device *s_i2c_dev = NULL;
static uint8_t s_i2c_addr = 0x76;
static bme280_calib_t s_calib;
static bool s_initialized = false;

/* Helper: Read multi-byte register bank */
static int bme280_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    if (!s_i2c_dev) {
        return -ENODEV;
    }
    return i2c_write_read(s_i2c_dev, s_i2c_addr, &reg, 1, data, len);
}

/* Helper: Write single register byte */
static int bme280_write_reg(uint8_t reg, uint8_t val)
{
    if (!s_i2c_dev) {
        return -ENODEV;
    }
    uint8_t buf[2] = { reg, val };
    return i2c_write(s_i2c_dev, buf, sizeof(buf), s_i2c_addr);
}

/* Parse calibration data from non-volatile trim fuses */
static int bme280_read_calibration(void)
{
    uint8_t buf1[26];
    uint8_t buf2[7];
    int rc;

    rc = bme280_read_regs(BME280_REG_CALIB_00_25, buf1, sizeof(buf1));
    if (rc < 0) {
        LOG_ERR("Failed to read calib bank 1: %d", rc);
        return rc;
    }

    rc = bme280_read_regs(BME280_REG_CALIB_26_41, buf2, sizeof(buf2));
    if (rc < 0) {
        LOG_ERR("Failed to read calib bank 2: %d", rc);
        return rc;
    }

    /* Temperature trimming */
    s_calib.dig_T1 = (uint16_t)(buf1[0] | (buf1[1] << 8));
    s_calib.dig_T2 = (int16_t)(buf1[2] | (buf1[3] << 8));
    s_calib.dig_T3 = (int16_t)(buf1[4] | (buf1[5] << 8));

    /* Pressure trimming */
    s_calib.dig_P1 = (uint16_t)(buf1[6] | (buf1[7] << 8));
    s_calib.dig_P2 = (int16_t)(buf1[8] | (buf1[9] << 8));
    s_calib.dig_P3 = (int16_t)(buf1[10] | (buf1[11] << 8));
    s_calib.dig_P4 = (int16_t)(buf1[12] | (buf1[13] << 8));
    s_calib.dig_P5 = (int16_t)(buf1[14] | (buf1[15] << 8));
    s_calib.dig_P6 = (int16_t)(buf1[16] | (buf1[17] << 8));
    s_calib.dig_P7 = (int16_t)(buf1[18] | (buf1[19] << 8));
    s_calib.dig_P8 = (int16_t)(buf1[20] | (buf1[21] << 8));
    s_calib.dig_P9 = (int16_t)(buf1[22] | (buf1[23] << 8));

    /* Humidity trimming */
    s_calib.dig_H1 = buf1[25];
    s_calib.dig_H2 = (int16_t)(buf2[0] | (buf2[1] << 8));
    s_calib.dig_H3 = buf2[2];
    s_calib.dig_H4 = (int16_t)((buf2[3] << 4) | (buf2[4] & 0x0F));
    s_calib.dig_H5 = (int16_t)((buf2[5] << 4) | ((buf2[4] >> 4) & 0x0F));
    s_calib.dig_H6 = (int8_t)buf2[6];

    LOG_DBG("BME280 calib loaded: T1=%u, P1=%u, H1=%u",
            s_calib.dig_T1, s_calib.dig_P1, s_calib.dig_H1);
    return 0;
}

/* Bosch 32-bit fixed point temperature compensation (returns 0.01 deg C) */
static int32_t bme280_compensate_temperature(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) *
                    ((int32_t)s_calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)s_calib.dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)s_calib.dig_T1))) >> 12) *
                    ((int32_t)s_calib.dig_T3)) >> 14;

    s_calib.t_fine = var1 + var2;
    return (s_calib.t_fine * 5 + 128) >> 8;
}

/* Bosch 32-bit fixed point pressure compensation (returns Pascals) */
static uint32_t bme280_compensate_pressure(int32_t adc_P)
{
    int64_t var1 = ((int64_t)s_calib.t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)s_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)s_calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s_calib.dig_P3) >> 8) +
           ((var1 * (int64_t)s_calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)s_calib.dig_P1) >> 33;

    if (var1 == 0) {
        return 0; /* Avoid division by zero */
    }

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)s_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)s_calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s_calib.dig_P7) << 4);

    return (uint32_t)(p >> 8);
}

/* Bosch 32-bit fixed point humidity compensation (returns 0.01 %RH) */
static uint32_t bme280_compensate_humidity(int32_t adc_H)
{
    int32_t v_x1_u32r = (s_calib.t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)s_calib.dig_H4) << 20) -
                    (((int32_t)s_calib.dig_H5) * v_x1_u32r)) +
                   ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)s_calib.dig_H6)) >> 10) *
                      (((v_x1_u32r * ((int32_t)s_calib.dig_H3)) >> 11) +
                       ((int32_t)32768))) >> 10) +
                    ((int32_t)2097152)) * ((int32_t)s_calib.dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                               ((int32_t)s_calib.dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);

    return (uint32_t)((v_x1_u32r >> 12) * 100 / 1024);
}

int custom_bme280_init(const struct device *i2c_dev, uint8_t i2c_addr)
{
    if (!i2c_dev || !device_is_ready(i2c_dev)) {
        LOG_ERR("I2C bus device not ready");
        return -ENODEV;
    }

    s_i2c_dev = i2c_dev;
    s_i2c_addr = i2c_addr;

    /* Verify Chip ID */
    uint8_t chip_id = 0;
    int rc = bme280_read_regs(BME280_REG_ID, &chip_id, 1);
    if (rc < 0 || chip_id != BME280_CHIP_ID_VAL) {
        LOG_ERR("BME280 chip ID mismatch! Expected 0x%02X, got 0x%02X (err %d)",
                BME280_CHIP_ID_VAL, chip_id, rc);
        return -ENXIO;
    }

    LOG_INF("BME280 detected (Chip ID: 0x%02X) on I2C address 0x%02X", chip_id, s_i2c_addr);

    /* Software Reset */
    bme280_write_reg(BME280_REG_RESET, BME280_RESET_VAL);
    k_msleep(10);

    /* Read calibration trim fuses */
    rc = bme280_read_calibration();
    if (rc < 0) {
        return rc;
    }

    /* Configure oversampling:
     * ctrl_hum: osrs_h = 1x (0x01)
     * ctrl_meas: osrs_t = 2x (0x02), osrs_p = 4x (0x03), mode = NORMAL (0x03)
     * config: t_sb = 62.5ms (0x01), filter = 4x (0x02)
     */
    bme280_write_reg(BME280_REG_CTRL_HUM, 0x01);
    bme280_write_reg(BME280_REG_CONFIG, (0x01 << 5) | (0x02 << 2));
    bme280_write_reg(BME280_REG_CTRL_MEAS, (0x02 << 5) | (0x03 << 2) | 0x03);

    s_initialized = true;
    LOG_INF("BME280 initialized in Normal continuous sampling mode");
    return 0;
}

int custom_bme280_read(int32_t *temp_centi_c, uint32_t *press_pa, uint32_t *hum_centi_pct)
{
    if (!s_initialized) {
        return -EAGAIN;
    }

    uint8_t raw[BME280_DATA_BURST_LEN];
    int rc = bme280_read_regs(BME280_REG_DATA_START, raw, sizeof(raw));
    if (rc < 0) {
        LOG_ERR("Failed to burst read BME280 raw data: %d", rc);
        return rc;
    }

    int32_t adc_P = (int32_t)(((uint32_t)raw[0] << 12) |
                             ((uint32_t)raw[1] << 4) |
                             ((uint32_t)raw[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)raw[3] << 12) |
                             ((uint32_t)raw[4] << 4) |
                             ((uint32_t)raw[5] >> 4));
    int32_t adc_H = (int32_t)(((uint32_t)raw[6] << 8) |
                             (uint32_t)raw[7]);

    /* Order is critical: Temperature must be compensated first to compute t_fine */
    int32_t comp_t = bme280_compensate_temperature(adc_T);
    uint32_t comp_p = bme280_compensate_pressure(adc_P);
    uint32_t comp_h = bme280_compensate_humidity(adc_H);

    if (temp_centi_c) *temp_centi_c = comp_t;
    if (press_pa)     *press_pa = comp_p;
    if (hum_centi_pct)*hum_centi_pct = comp_h;

    return 0;
}
