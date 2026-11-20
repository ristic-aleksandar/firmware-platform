/**
 * @file test_e32p_bmp280.c
 * @brief Unity tests for e32p_bmp280 — the pure compensation path is driven with
 *        the Bosch datasheet reference vector (no I2C bus required). The device
 *        layer is only checked for argument validation, since a real BMP280 must
 *        be wired for the transfer paths (covered by the HIL suite).
 */
#include "unity.h"
#include <string.h>
#include "e32p_bmp280.h"

/* Bosch reference calibration (datasheet worked example; identical to BME280). */
static const e32p_bmp280_calib_t REF = {
    .dig_t1 = 27504, .dig_t2 = 26435, .dig_t3 = -1000,
    .dig_p1 = 36477, .dig_p2 = -10685, .dig_p3 = 3024, .dig_p4 = 2855, .dig_p5 = 140,
    .dig_p6 = -7, .dig_p7 = 15500, .dig_p8 = -14600, .dig_p9 = 6000,
};

/* Reference raw ADC codes and their published compensated outputs. */
#define REF_ADC_T   519888
#define REF_ADC_P   415148
#define REF_T_FINE  128422
#define REF_TEMP    2508      /* 25.08 °C */
#define REF_PRESS   100653    /* Pa */

static void put16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)(v & 0xFF); b[1] = (uint8_t)(v >> 8); }

TEST_CASE("e32p_bmp280: parse_calib decodes the little-endian block", "[e32p_bmp280]")
{
    uint8_t raw[24];
    put16(&raw[0],  REF.dig_t1);            put16(&raw[2],  (uint16_t)REF.dig_t2);
    put16(&raw[4],  (uint16_t)REF.dig_t3);  put16(&raw[6],  REF.dig_p1);
    put16(&raw[8],  (uint16_t)REF.dig_p2);  put16(&raw[10], (uint16_t)REF.dig_p3);
    put16(&raw[12], (uint16_t)REF.dig_p4);  put16(&raw[14], (uint16_t)REF.dig_p5);
    put16(&raw[16], (uint16_t)REF.dig_p6);  put16(&raw[18], (uint16_t)REF.dig_p7);
    put16(&raw[20], (uint16_t)REF.dig_p8);  put16(&raw[22], (uint16_t)REF.dig_p9);

    e32p_bmp280_calib_t c;
    e32p_bmp280_parse_calib(raw, &c);
    TEST_ASSERT_EQUAL_UINT16(REF.dig_t1, c.dig_t1);
    TEST_ASSERT_EQUAL_INT16(REF.dig_t2, c.dig_t2);
    TEST_ASSERT_EQUAL_INT16(REF.dig_t3, c.dig_t3);   /* signed negative */
    TEST_ASSERT_EQUAL_UINT16(REF.dig_p1, c.dig_p1);
    TEST_ASSERT_EQUAL_INT16(REF.dig_p2, c.dig_p2);   /* signed negative */
    TEST_ASSERT_EQUAL_INT16(REF.dig_p6, c.dig_p6);   /* signed negative */
    TEST_ASSERT_EQUAL_INT16(REF.dig_p8, c.dig_p8);   /* signed negative */
    TEST_ASSERT_EQUAL_INT16(REF.dig_p9, c.dig_p9);
}

TEST_CASE("e32p_bmp280: temperature compensation matches the reference vector", "[e32p_bmp280]")
{
    int32_t t_fine = 0;
    int32_t temp = e32p_bmp280_compensate_temp(REF_ADC_T, &REF, &t_fine);
    TEST_ASSERT_EQUAL_INT32(REF_T_FINE, t_fine);
    TEST_ASSERT_EQUAL_INT32(REF_TEMP, temp);
}

TEST_CASE("e32p_bmp280: pressure compensation matches the reference vector", "[e32p_bmp280]")
{
    uint32_t press = e32p_bmp280_compensate_press(REF_ADC_P, &REF, REF_T_FINE);
    TEST_ASSERT_EQUAL_UINT32(REF_PRESS, press);
}

TEST_CASE("e32p_bmp280: pressure compensation guards divide-by-zero", "[e32p_bmp280]")
{
    e32p_bmp280_calib_t bad = REF;
    bad.dig_p1 = 0;   /* forces var1 == 0 in the Bosch formula */
    TEST_ASSERT_EQUAL_UINT32(0, e32p_bmp280_compensate_press(REF_ADC_P, &bad, REF_T_FINE));
}

TEST_CASE("e32p_bmp280: device functions reject bad args", "[e32p_bmp280]")
{
    e32p_bmp280_handle_t h = NULL;
    /* NULL config / NULL out. */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_bmp280_init(NULL, &h));
    e32p_bmp280_config_t cfg = { .bus = NULL, .addr = E32P_BMP280_ADDR_PRIMARY };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_bmp280_init(&cfg, &h));   /* NULL bus */
    /* Bad address is rejected before the bus is touched, so a dummy bus is safe. */
    e32p_bmp280_config_t cfg2 = { .bus = (e32p_i2c_bus_handle_t)1, .addr = 0x50 };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_bmp280_init(&cfg2, &h));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_bmp280_read(NULL, NULL));
}
