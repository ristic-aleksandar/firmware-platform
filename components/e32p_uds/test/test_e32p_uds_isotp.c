/**
 * @file test_e32p_uds_isotp.c
 * @brief Unity tests for the e32p_uds bus glue — on-target only.
 *
 * The glue needs a running ISO-TP link, so most of it is proven on hardware (the HIL
 * suite and example 33). What needs no bus is tested here: argument checks that run
 * before the glue touches the link, and the reflash routines' "results before a start"
 * answer, which touches no flash. The host runner does not build this file.
 */
#include "unity.h"
#include "sdkconfig.h"
#include "e32p_uds.h"
#include "e32p_uds_isotp.h"
#if CONFIG_E32P_OTA_ENABLE
#include "e32p_uds_ota.h"
#endif

static esp_err_t no_send(const uint8_t *data, uint16_t len, void *user)
{
    (void)data;
    (void)len;
    (void)user;
    return ESP_OK;
}

TEST_CASE("uds glue: rx_stmin_ms above 127 ms is refused before anything starts",
          "[e32p_uds]")
{
    const e32p_uds_config_t server = { .send = no_send };
    e32p_uds_isotp_config_t glue = { .server = &server, .rx_stmin_ms = 128u };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, e32p_uds_isotp_start(&glue));
    TEST_ASSERT_FALSE(e32p_uds_isotp_running());

    /* 0..127 pass the check and reach the next one: no ISO-TP link is running here. */
    glue.rx_stmin_ms = 0u;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, e32p_uds_isotp_start(&glue));
    glue.rx_stmin_ms = 127u;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, e32p_uds_isotp_start(&glue));
    TEST_ASSERT_FALSE(e32p_uds_isotp_running());
}

#if CONFIG_E32P_OTA_ENABLE
/* Asks for results only (control 0x03), never starts a routine: starting FF00 erases the
   inactive OTA slot. The "started since boot" flags are set by a start alone, and no
   other case on the runner sends one, so both routines still count as never started. */
TEST_CASE("uds ota: results (31 03) before a start answer 0x24 and touch no flash",
          "[e32p_uds]")
{
    uint8_t status[4] = { 0xEEu, 0xEEu, 0xEEu, 0xEEu };
    uint16_t status_len = 0xFFFFu;
    TEST_ASSERT_EQUAL(E32P_UDS_NRC_SEQUENCE_ERROR,
                      e32p_uds_ota_routine(E32P_UDS_RID_ERASE_MEMORY, 0x03u, NULL, 0u,
                                           status, sizeof(status), &status_len, NULL));
    TEST_ASSERT_EQUAL(E32P_UDS_NRC_SEQUENCE_ERROR,
                      e32p_uds_ota_routine(E32P_UDS_RID_CHECK_PROGRAMMING, 0x03u, NULL, 0u,
                                           status, sizeof(status), &status_len, NULL));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, status[0]);          /* no status record was written */
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, status_len);
}
#endif
