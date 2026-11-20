/**
 * @file test_e32p_twai.c
 * @brief Unity tests for e32p_twai. Headless-safe: exercises install / start /
 *        transmit / stop (NO_ACK transmit queues without an ACK). A frame
 *        loopback needs a TX->RX wire or transceiver — that is I-slot hardware.
 */
#include "unity.h"
#include "driver/gpio.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "e32p_twai.h"

TEST_CASE("e32p_twai: install / start / transmit / stop (no wire)", "[e32p_twai]")
{
    e32p_twai_config_t cfg = { .tx_gpio = 21, .rx_gpio = 22, .bitrate_bps = 500000,
                               .mode = E32P_TWAI_MODE_NO_ACK };
    TEST_ESP_OK(e32p_twai_init(&cfg));
    TEST_ESP_OK(e32p_twai_start());

    e32p_twai_message_t tx = { .id = 0x123, .self = true, .len = 4, .data = { 1, 2, 3, 4 } };
    TEST_ESP_OK(e32p_twai_transmit(&tx, 100));   /* queues in NO_ACK; RX needs a wire */

    TEST_ESP_OK(e32p_twai_stop());
    TEST_ESP_OK(e32p_twai_deinit());
}

TEST_CASE("e32p_twai: rejects bad args", "[e32p_twai]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_twai_init(NULL));

    e32p_twai_config_t cfg = { .tx_gpio = 21, .rx_gpio = 22, .bitrate_bps = 999,  /* unsupported */
                               .mode = E32P_TWAI_MODE_NO_ACK };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_twai_init(&cfg));
}

TEST_CASE("e32p_twai: install keeps the TX pin recessive before start", "[e32p_twai]")
{
    /* IDF 5.3.1's install drives the TX pin from its GPIO output register for a few
       ms before the TWAI controller takes the pin. That bit must be 1 (recessive),
       or every bring-up pulls the bus dominant. Start from the reset value, 0. */
    TEST_ESP_OK(gpio_set_level(GPIO_NUM_21, 0));
    e32p_twai_config_t cfg = { .tx_gpio = 21, .rx_gpio = 22, .bitrate_bps = 500000,
                               .mode = E32P_TWAI_MODE_NO_ACK };
    TEST_ESP_OK(e32p_twai_init(&cfg));
    const uint32_t out_bit = (REG_READ(GPIO_OUT_REG) >> 21) & 1u;
    TEST_ESP_OK(e32p_twai_deinit());           /* before the assert: never leave it installed */
    TEST_ASSERT_EQUAL_UINT32(1u, out_bit);
}
