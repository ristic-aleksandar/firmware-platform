/**
 * @file test_e32p_ble.c
 * @brief Unity tests for e32p_ble. The advertising-payload assembly and UUID
 *        helpers are pure and fully covered. The NimBLE peripheral needs the
 *        Bluetooth host (and a central to connect); in a radio-free build the
 *        device functions return ESP_ERR_NOT_SUPPORTED, which is checked here.
 *        The real peripheral is exercised by the 27_ble demo (connect with a
 *        phone / nRF Connect).
 */
#include "unity.h"
#include <string.h>
#include "e32p_ble.h"

TEST_CASE("e32p_ble: uuid16 / uuid128 helpers", "[e32p_ble]")
{
    e32p_ble_uuid_t u16 = e32p_ble_uuid16(0x180F);
    TEST_ASSERT_EQUAL_UINT8(2, u16.len);
    TEST_ASSERT_EQUAL_HEX8(0x0F, u16.bytes[0]);   /* little-endian */
    TEST_ASSERT_EQUAL_HEX8(0x18, u16.bytes[1]);

    uint8_t raw[16];
    for (int i = 0; i < 16; i++) raw[i] = (uint8_t)i;
    e32p_ble_uuid_t u128 = e32p_ble_uuid128(raw);
    TEST_ASSERT_EQUAL_UINT8(16, u128.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, u128.bytes, 16);
}

TEST_CASE("e32p_ble: adv payload — flags + 16-bit service + name", "[e32p_ble]")
{
    const uint8_t expect[] = {
        0x02, 0x01, 0x06,                   /* flags */
        0x03, 0x03, 0xFF, 0x00,             /* complete 16-bit service list: 0x00FF */
        0x05, 0x09, 'e', '3', '2', 'p',     /* complete local name */
    };
    uint8_t buf[31];
    e32p_ble_uuid_t svc = e32p_ble_uuid16(0x00FF);
    int n = e32p_ble_adv_build(buf, sizeof(buf), E32P_BLE_ADV_FLAGS_DEFAULT, "e32p", &svc);
    TEST_ASSERT_EQUAL_INT(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, sizeof(expect));
}

TEST_CASE("e32p_ble: adv payload — flags only, and 128-bit service", "[e32p_ble]")
{
    uint8_t buf[31];
    int n = e32p_ble_adv_build(buf, sizeof(buf), 0x06, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[2]);

    uint8_t raw[16] = { 0 };
    e32p_ble_uuid_t u128 = e32p_ble_uuid128(raw);
    n = e32p_ble_adv_build(buf, sizeof(buf), 0x06, "x", &u128);
    TEST_ASSERT_EQUAL_INT(3 + 18 + 3, n);      /* flags + 128-bit list + name "x" */
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[4]);      /* complete 128-bit UUID list type */
}

TEST_CASE("e32p_ble: adv payload rejects an oversized packet", "[e32p_ble]")
{
    uint8_t buf[31];
    /* 30-char name pushes past the 31-byte legacy limit. */
    const char *too_long = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123";
    TEST_ASSERT_EQUAL_INT(-1, e32p_ble_adv_build(buf, sizeof(buf), 0x06, too_long, NULL));
    TEST_ASSERT_EQUAL_INT(-1, e32p_ble_adv_build(NULL, sizeof(buf), 0x06, "x", NULL));
}

#if !CONFIG_BT_NIMBLE_ENABLED
TEST_CASE("e32p_ble: peripheral reports NOT_SUPPORTED without the BLE host", "[e32p_ble]")
{
    e32p_ble_char_t c = { .uuid = e32p_ble_uuid16(0x2A56), .props = E32P_BLE_PROP_READ };
    e32p_ble_config_t cfg = { .device_name = "e32p", .service_uuid = e32p_ble_uuid16(0x00FF),
                              .chars = &c, .n_chars = 1 };
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, e32p_ble_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, e32p_ble_notify(0, "x", 1));
    TEST_ASSERT_FALSE(e32p_ble_is_connected());
}
#endif
