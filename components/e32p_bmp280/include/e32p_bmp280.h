#pragma once
/**
 * @file e32p_bmp280.h
 * @brief Bosch BMP280 pressure + temperature sensor over e32p_i2c.
 *
 * Split into a PURE part (calibration parse + Bosch compensation math — no bus,
 * fully unit-testable) and a THIN I2C part (chip probe, calib read, config,
 * burst sample). Opaque-handle style; one handle per sensor.
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"
#include "e32p_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

#define E32P_BMP280_ADDR_PRIMARY    0x76  /*!< SDO tied low (module default). */
#define E32P_BMP280_ADDR_SECONDARY  0x77  /*!< SDO tied high. */
#define E32P_BMP280_CHIP_ID         0x58  /*!< Value at register 0xD0 for BMP280. */

/** @brief Temperature/pressure oversampling (osrs_t / osrs_p). */
typedef enum {
    E32P_BMP280_OSR_SKIP = 0,  /*!< Measurement skipped (output 0x80000). */
    E32P_BMP280_OSR_X1   = 1,
    E32P_BMP280_OSR_X2   = 2,
    E32P_BMP280_OSR_X4   = 3,
    E32P_BMP280_OSR_X8   = 4,
    E32P_BMP280_OSR_X16  = 5,
} e32p_bmp280_osr_t;

/** @brief Power mode (ctrl_meas[1:0]). */
typedef enum {
    E32P_BMP280_MODE_SLEEP  = 0,
    E32P_BMP280_MODE_FORCED = 1,  /*!< One-shot, returns to sleep. */
    E32P_BMP280_MODE_NORMAL = 3,  /*!< Continuous, standby between samples. */
} e32p_bmp280_mode_t;

/** @brief IIR filter coefficient (config[4:2]). */
typedef enum {
    E32P_BMP280_FILTER_OFF = 0,
    E32P_BMP280_FILTER_2   = 1,
    E32P_BMP280_FILTER_4   = 2,
    E32P_BMP280_FILTER_8   = 3,
    E32P_BMP280_FILTER_16  = 4,
} e32p_bmp280_filter_t;

/** @brief Standby time between samples in normal mode (config[7:5]). */
typedef enum {
    E32P_BMP280_STANDBY_0P5  = 0,  /*!< 0.5 ms */
    E32P_BMP280_STANDBY_62P5 = 1,  /*!< 62.5 ms */
    E32P_BMP280_STANDBY_125  = 2,
    E32P_BMP280_STANDBY_250  = 3,
    E32P_BMP280_STANDBY_500  = 4,
    E32P_BMP280_STANDBY_1000 = 5,
    E32P_BMP280_STANDBY_2000 = 6,
    E32P_BMP280_STANDBY_4000 = 7,
} e32p_bmp280_standby_t;

/**
 * @brief Factory calibration coefficients (registers 0x88..0x9F).
 * Field order and signedness follow the BMP280 datasheet §3.11.2.
 */
typedef struct {
    uint16_t dig_t1;
    int16_t  dig_t2, dig_t3;
    uint16_t dig_p1;
    int16_t  dig_p2, dig_p3, dig_p4, dig_p5, dig_p6, dig_p7, dig_p8, dig_p9;
} e32p_bmp280_calib_t;

/** @brief A compensated sample. */
typedef struct {
    int32_t  temperature_c100;  /*!< Temperature in 0.01 °C (2508 = 25.08 °C). */
    uint32_t pressure_pa;       /*!< Absolute pressure in pascals. */
} e32p_bmp280_reading_t;

/** @brief Sensor configuration. `bus` must be an initialized e32p_i2c bus. */
typedef struct {
    e32p_i2c_bus_handle_t bus;          /*!< Existing I2C master bus. */
    uint8_t               addr;         /*!< 0x76 or 0x77. */
    uint32_t              scl_speed_hz; /*!< Per-device clock, e.g. 400000. */
    e32p_bmp280_osr_t     osr_temp;     /*!< Temperature oversampling. */
    e32p_bmp280_osr_t     osr_press;    /*!< Pressure oversampling. */
    e32p_bmp280_mode_t    mode;         /*!< Power mode. */
    e32p_bmp280_filter_t  filter;       /*!< IIR filter. */
    e32p_bmp280_standby_t standby;      /*!< Standby time (normal mode). */
} e32p_bmp280_config_t;

/** @brief Sensible defaults: 400 kHz, weather-ish oversampling, normal mode. */
#define E32P_BMP280_CONFIG_DEFAULT(bus_h) ((e32p_bmp280_config_t){ \
    .bus = (bus_h), .addr = E32P_BMP280_ADDR_PRIMARY, .scl_speed_hz = 400000, \
    .osr_temp = E32P_BMP280_OSR_X2, .osr_press = E32P_BMP280_OSR_X16,         \
    .mode = E32P_BMP280_MODE_NORMAL, .filter = E32P_BMP280_FILTER_16,         \
    .standby = E32P_BMP280_STANDBY_250 })

/** @brief Opaque per-sensor handle. */
typedef struct e32p_bmp280_ctx *e32p_bmp280_handle_t;

/* ---- Pure functions (no I2C; unit-testable) ------------------------------- */

/**
 * @brief Parse the 24-byte calibration block (regs 0x88..0x9F, little-endian).
 * @param[in]  raw  24 bytes as read from the sensor.
 * @param[out] out  Receives the decoded coefficients.
 */
void e32p_bmp280_parse_calib(const uint8_t raw[24], e32p_bmp280_calib_t *out);

/**
 * @brief Compensate a raw temperature reading (Bosch int32 formula).
 * @param[in]  adc_t   20-bit raw temperature.
 * @param[in]  c       Calibration coefficients.
 * @param[out] t_fine  Receives the fine resolution value (feeds pressure comp).
 * @return Temperature in 0.01 °C.
 */
int32_t e32p_bmp280_compensate_temp(int32_t adc_t, const e32p_bmp280_calib_t *c, int32_t *t_fine);

/**
 * @brief Compensate a raw pressure reading (Bosch int64 formula).
 * @param[in] adc_p   20-bit raw pressure.
 * @param[in] c       Calibration coefficients.
 * @param[in] t_fine  From e32p_bmp280_compensate_temp().
 * @return Absolute pressure in pascals (0 if calibration would divide by zero).
 */
uint32_t e32p_bmp280_compensate_press(int32_t adc_p, const e32p_bmp280_calib_t *c, int32_t t_fine);

/* ---- Device functions (I2C) ----------------------------------------------- */

/**
 * @brief Attach to a BMP280: verify chip id, read calibration, apply config.
 * @param[in]  cfg  Configuration (must not be NULL; cfg->bus must be valid).
 * @param[out] out  Receives the sensor handle.
 * @return ESP_OK; ESP_ERR_INVALID_ARG; ESP_ERR_NOT_FOUND (wrong/absent chip id);
 *         ESP_ERR_NO_MEM; or a driver error.
 */
esp_err_t e32p_bmp280_init(const e32p_bmp280_config_t *cfg, e32p_bmp280_handle_t *out);

/** @brief Detach and free (NULL is a no-op). Does not delete the shared bus. */
esp_err_t e32p_bmp280_deinit(e32p_bmp280_handle_t h);

/**
 * @brief Read and compensate one sample.
 * @param[in]  h    Valid handle.
 * @param[out] out  Receives temperature + pressure.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_bmp280_read(e32p_bmp280_handle_t h, e32p_bmp280_reading_t *out);

/**
 * @brief Read the raw (uncompensated) 20-bit ADC values.
 * @param[in]  h      Valid handle.
 * @param[out] adc_t  Raw temperature (may be NULL).
 * @param[out] adc_p  Raw pressure (may be NULL).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or a driver error.
 */
esp_err_t e32p_bmp280_read_raw(e32p_bmp280_handle_t h, int32_t *adc_t, int32_t *adc_p);

/** @brief Access the cached calibration coefficients (valid after init). */
const e32p_bmp280_calib_t *e32p_bmp280_calib(e32p_bmp280_handle_t h);

#ifdef __cplusplus
}
#endif
