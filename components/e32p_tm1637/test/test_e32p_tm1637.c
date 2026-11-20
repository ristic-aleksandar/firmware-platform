/**
 * @file test_e32p_tm1637.c
 * @brief Unity tests for e32p_tm1637. The font and integer formatting are pure
 *        and fully covered here; the bit-banged transport needs a real display
 *        (covered by the HIL suite), so only its argument checks are exercised.
 */
#include "unity.h"
#include "e32p_tm1637.h"

#define SEG_0  0x3F
#define SEG_1  0x06
#define SEG_2  0x5B
#define SEG_5  0x6D
#define SEG_A  0x77
#define SEG_F  0x71

TEST_CASE("e32p_tm1637: encode_digit maps hex nibbles and blanks the rest", "[e32p_tm1637]")
{
    TEST_ASSERT_EQUAL_HEX8(SEG_0, e32p_tm1637_encode_digit(0));
    TEST_ASSERT_EQUAL_HEX8(SEG_1, e32p_tm1637_encode_digit(1));
    TEST_ASSERT_EQUAL_HEX8(SEG_A, e32p_tm1637_encode_digit(0xA));
    TEST_ASSERT_EQUAL_HEX8(SEG_F, e32p_tm1637_encode_digit(0xF));
    TEST_ASSERT_EQUAL_HEX8(0x00, e32p_tm1637_encode_digit(16));   /* out of range -> blank */
}

TEST_CASE("e32p_tm1637: format_int lays out digits right-aligned", "[e32p_tm1637]")
{
    uint8_t s[4];
    TEST_ASSERT_EQUAL(ESP_OK, e32p_tm1637_format_int(1205, false, false, s));
    TEST_ASSERT_EQUAL_HEX8(SEG_1, s[0]);
    TEST_ASSERT_EQUAL_HEX8(SEG_2, s[1]);
    TEST_ASSERT_EQUAL_HEX8(SEG_0, s[2]);
    TEST_ASSERT_EQUAL_HEX8(SEG_5, s[3]);
}

TEST_CASE("e32p_tm1637: format_int blanks vs. pads leading positions", "[e32p_tm1637]")
{
    uint8_t s[4];
    TEST_ASSERT_EQUAL(ESP_OK, e32p_tm1637_format_int(5, false, false, s));
    TEST_ASSERT_EQUAL_HEX8(0x00, s[0]);   /* blank */
    TEST_ASSERT_EQUAL_HEX8(0x00, s[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s[2]);
    TEST_ASSERT_EQUAL_HEX8(SEG_5, s[3]);

    TEST_ASSERT_EQUAL(ESP_OK, e32p_tm1637_format_int(5, true, false, s));
    TEST_ASSERT_EQUAL_HEX8(SEG_0, s[0]);   /* padded with '0' */
    TEST_ASSERT_EQUAL_HEX8(SEG_0, s[1]);
    TEST_ASSERT_EQUAL_HEX8(SEG_0, s[2]);
    TEST_ASSERT_EQUAL_HEX8(SEG_5, s[3]);
}

TEST_CASE("e32p_tm1637: format_int handles colon and negatives", "[e32p_tm1637]")
{
    uint8_t s[4];
    /* Colon lights bit7 of position 1 (e.g. a clock 12:05). */
    TEST_ASSERT_EQUAL(ESP_OK, e32p_tm1637_format_int(1205, true, true, s));
    TEST_ASSERT_EQUAL_HEX8(SEG_2 | E32P_TM1637_SEG_COLON, s[1]);

    /* -12 -> [blank]['-']['1']['2'] */
    TEST_ASSERT_EQUAL(ESP_OK, e32p_tm1637_format_int(-12, false, false, s));
    TEST_ASSERT_EQUAL_HEX8(0x00, s[0]);
    TEST_ASSERT_EQUAL_HEX8(E32P_TM1637_SEG_MINUS, s[1]);
    TEST_ASSERT_EQUAL_HEX8(SEG_1, s[2]);
    TEST_ASSERT_EQUAL_HEX8(SEG_2, s[3]);
}

TEST_CASE("e32p_tm1637: format_int rejects out-of-range values", "[e32p_tm1637]")
{
    uint8_t s[4];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_format_int(10000, false, false, s));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_format_int(-1000, false, false, s));  /* sign won't fit */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_format_int(0, false, false, NULL));
}

TEST_CASE("e32p_tm1637: device functions reject bad args", "[e32p_tm1637]")
{
    e32p_tm1637_handle_t h = NULL;
    e32p_tm1637_config_t cfg = { .clk_pin = 18, .dio_pin = 19, .brightness = 4 };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_init(NULL, &h));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_init(&cfg, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_set_brightness(NULL, 3));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_set_segments(NULL, (const uint8_t *)&h, 4, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_show_int(NULL, 0, false, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_tm1637_clear(NULL));
}
