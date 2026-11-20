/**
 * @file test_e32p_modbus.c
 * @brief Unity tests for e32p_modbus. The protocol core and the slave handler are
 *        pure, so a complete master<->slave exchange is verified headless. The
 *        master's UART transport needs a wire/slave device (the 24_modbus demo);
 *        here only its init + argument validation are exercised.
 */
#include "unity.h"
#include <string.h>
#include "e32p_modbus.h"

TEST_CASE("e32p_modbus: CRC-16 matches the catalogue vector", "[e32p_modbus]")
{
    TEST_ASSERT_EQUAL_HEX16(0x4B37, e32p_modbus_crc16((const uint8_t *)"123456789", 9));
}

TEST_CASE("e32p_modbus: build read request matches the textbook frame", "[e32p_modbus]")
{
    /* Read 10 holding registers from slave 1 at address 0. */
    const uint8_t expect[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD };
    uint8_t buf[16];
    int n = e32p_modbus_build_read(buf, sizeof(buf), 1, E32P_MODBUS_FC_READ_HOLDING, 0, 10);
    TEST_ASSERT_EQUAL_INT(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, sizeof(expect));
}

TEST_CASE("e32p_modbus: build write requests match reference frames", "[e32p_modbus]")
{
    uint8_t buf[32];

    const uint8_t wsingle[] = { 0x01, 0x06, 0x00, 0x05, 0x12, 0x34, 0x94, 0xBC };
    int n = e32p_modbus_build_write_single(buf, sizeof(buf), 1, 5, 0x1234);
    TEST_ASSERT_EQUAL_INT(sizeof(wsingle), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wsingle, buf, sizeof(wsingle));

    const uint16_t vals[] = { 0x00AA, 0x00BB };
    const uint8_t wmulti[] = { 0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0xAA, 0x00, 0xBB, 0x93, 0xFC };
    n = e32p_modbus_build_write_multiple(buf, sizeof(buf), 1, 0, 2, vals);
    TEST_ASSERT_EQUAL_INT(sizeof(wmulti), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wmulti, buf, sizeof(wmulti));
}

TEST_CASE("e32p_modbus: adu_check validates the trailing CRC", "[e32p_modbus]")
{
    uint8_t frame[] = { 0x01, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x0B, 0x9B, 0xF6 };
    TEST_ASSERT_TRUE(e32p_modbus_adu_check(frame, sizeof(frame)));
    frame[4] ^= 0xFF;   /* corrupt a data byte */
    TEST_ASSERT_FALSE(e32p_modbus_adu_check(frame, sizeof(frame)));
    TEST_ASSERT_FALSE(e32p_modbus_adu_check(frame, 3));   /* too short */
}

TEST_CASE("e32p_modbus: parse read response and exception replies", "[e32p_modbus]")
{
    const uint8_t resp[] = { 0x01, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x0B, 0x9B, 0xF6 };
    uint16_t regs[4] = { 0 };
    size_t count = 0;
    uint8_t exc = 0xEE;
    TEST_ASSERT_EQUAL(ESP_OK, e32p_modbus_parse_read_response(resp, sizeof(resp), 1,
                                    E32P_MODBUS_FC_READ_HOLDING, regs, 4, &count, &exc));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_HEX16(0x000A, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0x000B, regs[1]);
    TEST_ASSERT_EQUAL_UINT8(0, exc);

    const uint8_t err_reply[] = { 0x01, 0x83, 0x02, 0xC0, 0xF1 };   /* illegal data address */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, e32p_modbus_parse_read_response(err_reply, sizeof(err_reply),
                                    1, E32P_MODBUS_FC_READ_HOLDING, regs, 4, &count, &exc));
    TEST_ASSERT_EQUAL_UINT8(E32P_MODBUS_EXC_ILLEGAL_DATA_ADDRESS, exc);

    uint8_t bad[] = { 0x01, 0x03, 0x02, 0x00, 0x0A, 0x00, 0x00 };   /* wrong CRC */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, e32p_modbus_parse_read_response(bad, sizeof(bad), 1,
                                    E32P_MODBUS_FC_READ_HOLDING, regs, 4, NULL, NULL));
}

TEST_CASE("e32p_modbus: full master<->slave round-trip (no wire)", "[e32p_modbus]")
{
    uint16_t holding[4] = { 111, 222, 333, 444 };
    uint16_t input[2]   = { 1000, 2000 };
    e32p_modbus_slave_t slave = { .address = 1, .holding = holding, .n_holding = 4,
                                  .input = input, .n_input = 2 };
    uint8_t req[32], resp[64];
    uint16_t out[4];
    size_t n;

    /* Read holding [1..2] -> 222, 333 */
    int rl = e32p_modbus_build_read(req, sizeof(req), 1, E32P_MODBUS_FC_READ_HOLDING, 1, 2);
    int sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    TEST_ASSERT_GREATER_THAN(0, sl);
    TEST_ASSERT_EQUAL(ESP_OK, e32p_modbus_parse_read_response(resp, sl, 1,
                                    E32P_MODBUS_FC_READ_HOLDING, out, 4, &n, NULL));
    TEST_ASSERT_EQUAL_UINT(2, n);
    TEST_ASSERT_EQUAL_UINT16(222, out[0]);
    TEST_ASSERT_EQUAL_UINT16(333, out[1]);

    /* Read input [0..1] -> 1000, 2000 */
    rl = e32p_modbus_build_read(req, sizeof(req), 1, E32P_MODBUS_FC_READ_INPUT, 0, 2);
    sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL(ESP_OK, e32p_modbus_parse_read_response(resp, sl, 1,
                                    E32P_MODBUS_FC_READ_INPUT, out, 4, &n, NULL));
    TEST_ASSERT_EQUAL_UINT16(1000, out[0]);
    TEST_ASSERT_EQUAL_UINT16(2000, out[1]);

    /* Write single holding[0] = 999 */
    rl = e32p_modbus_build_write_single(req, sizeof(req), 1, 0, 999);
    sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    TEST_ASSERT_GREATER_THAN(0, sl);
    TEST_ASSERT_EQUAL_UINT16(999, holding[0]);          /* slave applied the write */

    /* Write multiple holding[2..3] = {5, 6} */
    const uint16_t vals[] = { 5, 6 };
    rl = e32p_modbus_build_write_multiple(req, sizeof(req), 1, 2, 2, vals);
    sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    TEST_ASSERT_GREATER_THAN(0, sl);
    TEST_ASSERT_EQUAL_UINT16(5, holding[2]);
    TEST_ASSERT_EQUAL_UINT16(6, holding[3]);
}

TEST_CASE("e32p_modbus: slave replies with exceptions and ignores others", "[e32p_modbus]")
{
    uint16_t holding[2] = { 10, 20 };
    e32p_modbus_slave_t slave = { .address = 1, .holding = holding, .n_holding = 2 };
    uint8_t req[32], resp[64];
    uint8_t exc;
    uint16_t out[2];

    /* Read past the end -> illegal data address (0x02). */
    int rl = e32p_modbus_build_read(req, sizeof(req), 1, E32P_MODBUS_FC_READ_HOLDING, 0, 5);
    int sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, e32p_modbus_parse_read_response(resp, sl, 1,
                                    E32P_MODBUS_FC_READ_HOLDING, out, 2, NULL, &exc));
    TEST_ASSERT_EQUAL_UINT8(E32P_MODBUS_EXC_ILLEGAL_DATA_ADDRESS, exc);

    /* Unsupported function -> illegal function (0x01). */
    rl = e32p_modbus_build_read(req, sizeof(req), 1, 0x07, 0, 1);
    sl = e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp));
    TEST_ASSERT_GREATER_THAN(0, sl);
    TEST_ASSERT_EQUAL_HEX8(0x07 | E32P_MODBUS_FC_EXCEPTION_BIT, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(E32P_MODBUS_EXC_ILLEGAL_FUNCTION, resp[2]);

    /* Addressed to another slave -> no reply. */
    rl = e32p_modbus_build_read(req, sizeof(req), 2, E32P_MODBUS_FC_READ_HOLDING, 0, 1);
    TEST_ASSERT_EQUAL_INT(0, e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp)));

    /* Corrupt CRC -> rejected (-1, no reply). */
    rl = e32p_modbus_build_read(req, sizeof(req), 1, E32P_MODBUS_FC_READ_HOLDING, 0, 1);
    req[3] ^= 0xFF;
    TEST_ASSERT_EQUAL_INT(-1, e32p_modbus_slave_process(&slave, req, rl, resp, sizeof(resp)));
}

TEST_CASE("e32p_modbus: master init/deinit and argument validation", "[e32p_modbus]")
{
    uint16_t out[4];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_modbus_read_holding_registers(NULL, 1, 0, 1, out));

    e32p_modbus_master_config_t cfg = E32P_MODBUS_MASTER_CONFIG_DEFAULT();
    e32p_modbus_master_handle_t h = NULL;
    TEST_ESP_OK(e32p_modbus_master_init(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);

    /* These fail on argument checks before any bus traffic. */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_modbus_read_holding_registers(h, 1, 0, 200, out)); /* count>125 */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_modbus_read_holding_registers(h, 250, 0, 1, out)); /* slave>247 */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_modbus_read_holding_registers(h, 1, 0, 1, NULL));  /* out NULL */
    TEST_ASSERT_EQUAL_UINT8(0, e32p_modbus_last_exception(h));

    TEST_ESP_OK(e32p_modbus_master_deinit(h));
}
