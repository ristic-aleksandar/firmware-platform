/**
 * @file e32p_j1939.c
 * @brief e32p_j1939 implementation — J1939 codec + single-frame transfer over TWAI.
 */
#include <string.h>
#include "esp_log.h"
#include "e32p_twai.h"
#include "e32p_j1939.h"

static const char *TAG = "e32p_j1939";

/* PDU-Format threshold: PF < 240 is PDU1 (dest-specific), >= 240 is PDU2. */
#define E32P_J1939_PF_PDU2_MIN 240u

static struct {
    bool    inited;
    uint8_t src_addr;
    uint8_t default_priority;
    bool    self_reception;
} s_node;

/* --- Pure ID codec -------------------------------------------------------- */

bool e32p_j1939_pgn_is_pdu1(uint32_t pgn)
{
    return ((pgn >> 8) & 0xFFu) < E32P_J1939_PF_PDU2_MIN;
}

uint32_t e32p_j1939_id_encode(uint8_t priority, uint32_t pgn,
                              uint8_t src, uint8_t dst)
{
    const uint32_t edp = (pgn >> 17) & 0x1u;
    const uint32_t dp  = (pgn >> 16) & 0x1u;
    const uint32_t pf  = (pgn >> 8)  & 0xFFu;
    /* PDU1: PS carries the destination; PDU2: PS is the PGN's group extension. */
    const uint32_t ps  = (pf < E32P_J1939_PF_PDU2_MIN) ? dst : (pgn & 0xFFu);

    return ((uint32_t)(priority & 0x7u) << 26) |
           (edp << 25) | (dp << 24) |
           (pf << 16)  | (ps << 8)  | src;
}

void e32p_j1939_id_decode(uint32_t id, uint8_t *priority, uint32_t *pgn,
                          uint8_t *src, uint8_t *dst)
{
    const uint32_t edp = (id >> 25) & 0x1u;
    const uint32_t dp  = (id >> 24) & 0x1u;
    const uint32_t pf  = (id >> 16) & 0xFFu;
    const uint32_t ps  = (id >> 8)  & 0xFFu;

    if (priority) *priority = (uint8_t)((id >> 26) & 0x7u);
    if (src)      *src      = (uint8_t)(id & 0xFFu);

    if (pf < E32P_J1939_PF_PDU2_MIN) {                 /* PDU1: dest-specific */
        if (pgn) *pgn = (edp << 17) | (dp << 16) | (pf << 8);
        if (dst) *dst = (uint8_t)ps;
    } else {                                           /* PDU2: broadcast    */
        if (pgn) *pgn = (edp << 17) | (dp << 16) | (pf << 8) | ps;
        if (dst) *dst = E32P_J1939_ADDR_GLOBAL;
    }
}

/* --- Node lifecycle ------------------------------------------------------- */

esp_err_t e32p_j1939_init(const e32p_j1939_config_t *cfg)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(cfg->src_addr <= 253u, TAG);

    s_node.src_addr = cfg->src_addr;
    s_node.default_priority = (cfg->default_priority <= 7u)
                                  ? cfg->default_priority
                                  : E32P_J1939_PRIORITY_DEFAULT;
    s_node.self_reception = cfg->self_reception;
    s_node.inited = true;
    ESP_LOGI(TAG, "j1939 node init: SA=0x%02X prio=%u",
             s_node.src_addr, s_node.default_priority);
    return ESP_OK;
}

esp_err_t e32p_j1939_deinit(void)
{
    s_node.inited = false;
    return ESP_OK;
}

/* --- Single-frame transfer ------------------------------------------------ */

esp_err_t e32p_j1939_send(const e32p_j1939_message_t *msg, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(msg != NULL, TAG);
    E32P_CHECK_ARG(msg->len <= 8u, TAG);
    E32P_RETURN_ON_FALSE(s_node.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    const uint8_t prio = (msg->priority <= 7u) ? msg->priority : s_node.default_priority;

    e32p_twai_message_t tx = {
        .id       = e32p_j1939_id_encode(prio, msg->pgn, s_node.src_addr, msg->dst_addr),
        .extended = true,
        .self     = s_node.self_reception,  /* true only for single-board loopback */
        .len      = msg->len,
    };
    memcpy(tx.data, msg->data, msg->len);
    return e32p_twai_transmit(&tx, timeout_ms);
}

esp_err_t e32p_j1939_receive(e32p_j1939_message_t *msg, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(msg != NULL, TAG);
    E32P_RETURN_ON_FALSE(s_node.inited, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    e32p_twai_message_t rx = {0};
    esp_err_t err = e32p_twai_receive(&rx, timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    /* Non-J1939 (standard 11-bit) frames are not decodable as J1939. */
    E32P_RETURN_ON_FALSE(rx.extended, ESP_ERR_INVALID_RESPONSE, TAG, "not a 29-bit frame");

    e32p_j1939_id_decode(rx.id, &msg->priority, &msg->pgn, &msg->src_addr, &msg->dst_addr);
    msg->len = (rx.len <= 8u) ? rx.len : 8u;
    memcpy(msg->data, rx.data, msg->len);
    return ESP_OK;
}

esp_err_t e32p_j1939_request(uint32_t requested_pgn, uint8_t dst, uint32_t timeout_ms)
{
    e32p_j1939_message_t msg = {
        .priority = 6u,
        .pgn      = E32P_J1939_PGN_REQUEST,
        .dst_addr = dst,
        .len      = 3u,
        .data     = { (uint8_t)(requested_pgn & 0xFFu),
                      (uint8_t)((requested_pgn >> 8) & 0xFFu),
                      (uint8_t)((requested_pgn >> 16) & 0xFFu) },
    };
    return e32p_j1939_send(&msg, timeout_ms);
}
