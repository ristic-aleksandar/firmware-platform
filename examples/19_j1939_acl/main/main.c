/**
 * @file main.c
 * @brief 19_j1939_acl — proves e32p_j1939_acl on a bare board.
 *
 * Runs two address-claiming engines in one app (both prefer SA 0x80) wired
 * through an in-process frame queue — no CAN bus needed. Engine A has a lower
 * NAME, so it wins 0x80; engine B (arbitrary-address-capable) relocates to a
 * free address. The app logs the outcome.
 *
 * On real hardware, point each engine's tx sink at e32p_j1939_send (via
 * id_encode) and feed e32p_j1939_receive() frames to on_frame() — see the
 * component README.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "e32p_j1939_acl.h"

static const char *TAG = "j1939_acl";

static e32p_j1939_acl_handle_t s_a, s_b;
static struct { e32p_j1939_message_t f; int to; } s_q[64];
static int s_qh, s_qt;

static esp_err_t tx_a(const e32p_j1939_message_t *m, void *u)
{ (void)u; s_q[s_qt].f = *m; s_q[s_qt].to = 1; s_qt = (s_qt + 1) & 63; return ESP_OK; }
static esp_err_t tx_b(const e32p_j1939_message_t *m, void *u)
{ (void)u; s_q[s_qt].f = *m; s_q[s_qt].to = 0; s_qt = (s_qt + 1) & 63; return ESP_OK; }
static void pump(void)
{
    while (s_qh != s_qt) {
        e32p_j1939_message_t f = s_q[s_qh].f;
        int to = s_q[s_qh].to;
        s_qh = (s_qh + 1) & 63;
        e32p_j1939_acl_on_frame(to == 0 ? s_a : s_b, &f);
    }
}
static uint64_t name_of(uint32_t identity, bool arb)
{
    e32p_j1939_name_t n = { .arbitrary_address_capable = arb, .function = 130,
                            .manufacturer_code = 1234, .identity_number = identity };
    return e32p_j1939_name_encode(&n);
}

void app_main(void)
{
    ESP_LOGI(TAG, "two nodes both prefer SA 0x80; lower NAME wins, other relocates");

    uint32_t round = 0;
    for (;;) {
        s_qh = s_qt = 0;
        e32p_j1939_acl_config_t ca = { .name = name_of(100,  true), .preferred_sa = 0x80, .tx = tx_a };
        e32p_j1939_acl_config_t cb = { .name = name_of(9000, true), .preferred_sa = 0x80, .tx = tx_b };
        ESP_ERROR_CHECK(e32p_j1939_acl_init(&ca, &s_a));
        ESP_ERROR_CHECK(e32p_j1939_acl_init(&cb, &s_b));
        e32p_j1939_acl_start(s_a);
        e32p_j1939_acl_start(s_b);

        uint32_t base = (uint32_t)(esp_timer_get_time() / 1000);
        while ((uint32_t)(esp_timer_get_time() / 1000) - base < 500) {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            e32p_j1939_acl_poll(s_a, now);
            e32p_j1939_acl_poll(s_b, now);
            pump();
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        ESP_LOGI(TAG, "round %lu: A claimed=0x%02X (%s), B claimed=0x%02X (%s)",
                 (unsigned long)round,
                 e32p_j1939_acl_current_sa(s_a), e32p_j1939_acl_is_claimed(s_a) ? "ok" : "no",
                 e32p_j1939_acl_current_sa(s_b), e32p_j1939_acl_is_claimed(s_b) ? "ok" : "no");

        e32p_j1939_acl_deinit(s_a);
        e32p_j1939_acl_deinit(s_b);
        round++;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
