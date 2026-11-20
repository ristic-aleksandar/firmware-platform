/**
 * @file e32p_obd2_isotp.c
 * @brief Transport glue: two ISO-TP channels, one OBD-II server.
 *
 * Two channels because a functional and a physical request can be in flight at
 * the same time, and each ISO-TP channel owns one reassembly buffer.
 *
 * Only the physical channel ever transmits. A functionally addressed request is
 * answered from the ECU's own physical response ID (ISO 15765-4), which is
 * exactly what sending on the physical channel does — no second transmit path,
 * and no special case for 11-bit versus 29-bit addressing.
 */
#include <string.h>
#include "e32p_isotp.h"
#include "e32p_isotp_twai.h"
#include "e32p_obd2.h"
#include "e32p_obd2_isotp.h"

static const char *TAG = "e32p_obd2";

#define FUNCTIONAL_ID_11   0x7DFu   /* 11-bit broadcast request ID            */
#define FUNCTIONAL_TA_29   0x33u    /* 29-bit functional target address       */
#define REQ_ID_11(ecu)     (0x7E0u + ((ecu) - E32P_OBD2_ECU_ADDR_DEFAULT))
#define RESP_ID_11(ecu)    (0x7E8u + ((ecu) - E32P_OBD2_ECU_ADDR_DEFAULT))
#define MAX_MSG_DEFAULT    128u     /* longest response is 68 bytes           */

static struct {
    bool                started;
    e32p_obd2_handle_t  server;
    e32p_isotp_handle_t physical;
    e32p_isotp_handle_t functional;
    uint32_t            dropped;
} s_link;

/**
 * @brief Answer one reassembled request.
 *
 * Runs in the ISO-TP pump task, so it uses e32p_isotp_send() (arms and returns)
 * and never send_sync(), which would wait inside the pump for the pump.
 */
static void answer(const uint8_t *req, uint16_t len, e32p_obd2_addressing_t addressing)
{
    uint8_t resp[E32P_OBD2_RESP_MAX];
    uint16_t n = 0;

    if (e32p_obd2_request(s_link.server, addressing, req, len, resp, sizeof(resp), &n) != ESP_OK) {
        return;
    }
    if (n == 0u) {
        return;                     /* the silence rule: nothing to send */
    }
    if (e32p_isotp_tx_busy(s_link.physical)) {
        /* The previous answer is still going out. Dropping is honest: the tool
           that overlapped requests has already stopped waiting for this one. */
        s_link.dropped++;
        ESP_LOGW(TAG, "response dropped, transmitter busy (total %u)", (unsigned)s_link.dropped);
        return;
    }
    (void)e32p_isotp_send(s_link.physical, resp, n);
}

static void on_physical(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    answer(data, len, E32P_OBD2_ADDR_PHYSICAL);
}

static void on_functional(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    answer(data, len, E32P_OBD2_ADDR_FUNCTIONAL);
}

esp_err_t e32p_obd2_isotp_start(const e32p_obd2_isotp_config_t *cfg)
{
    E32P_CHECK_ARG(cfg != NULL && cfg->server != NULL, TAG);
    E32P_RETURN_ON_FALSE(!s_link.started, ESP_ERR_INVALID_STATE, TAG, "already started");
    E32P_RETURN_ON_FALSE(e32p_isotp_twai_running(), ESP_ERR_INVALID_STATE, TAG,
                         "start the ISO-TP link first");

    const uint8_t ecu = (cfg->ecu_addr != 0u) ? cfg->ecu_addr : E32P_OBD2_ECU_ADDR_DEFAULT;
    const uint8_t tester = (cfg->tester_addr != 0u) ? cfg->tester_addr : E32P_OBD2_TESTER_ADDR_DEFAULT;
    const uint16_t max_msg = (cfg->max_msg_bytes != 0u) ? cfg->max_msg_bytes : MAX_MSG_DEFAULT;
    E32P_CHECK_ARG(ecu >= E32P_OBD2_ECU_ADDR_DEFAULT && ecu < (E32P_OBD2_ECU_ADDR_DEFAULT + 8u), TAG);
    E32P_CHECK_ARG(max_msg >= E32P_OBD2_RESP_MAX, TAG);

    s_link.server = cfg->server;
    s_link.dropped = 0;

    e32p_isotp_config_t phys;
    e32p_isotp_config_defaults(&phys, RESP_ID_11(ecu), REQ_ID_11(ecu), ecu, tester, false);
    phys.max_msg_bytes = max_msg;
    phys.on_rx = on_physical;
    esp_err_t err = e32p_isotp_twai_channel(&phys, &s_link.physical);
    if (err != ESP_OK) {
        return err;
    }

    /* Receive-only: its answers go out on the physical channel (see the header).
       In 29-bit mode the engine listens on 0x18DB<source><target>, and a tester
       broadcasts on 0x18DB33F1 - so source is the functional address 0x33 and target
       is the tester. (Passing the ECU as source listened on 0x18DB1033, which no
       tester ever sends; 11-bit is unaffected.) */
    e32p_isotp_config_t func;
    e32p_isotp_config_defaults(&func, RESP_ID_11(ecu), FUNCTIONAL_ID_11, FUNCTIONAL_TA_29, tester, true);
    func.max_msg_bytes = max_msg;
    func.on_rx = on_functional;
    err = e32p_isotp_twai_channel(&func, &s_link.functional);
    if (err != ESP_OK) {
        (void)e32p_isotp_twai_destroy_channel(s_link.physical);
        s_link.physical = NULL;
        return err;
    }

    s_link.started = true;
    ESP_LOGI(TAG, "obd2 on the bus: physical rx=0x%X functional rx=0x%X (%s)",
             (unsigned)e32p_isotp_rx_id(s_link.physical),
             (unsigned)e32p_isotp_rx_id(s_link.functional),
             e32p_isotp_is_extended(s_link.physical) ? "29-bit" : "11-bit");
    return ESP_OK;
}

esp_err_t e32p_obd2_isotp_stop(void)
{
    if (!s_link.started) {
        return ESP_OK;
    }
    esp_err_t err = e32p_isotp_twai_destroy_channel(s_link.functional);
    const esp_err_t err2 = e32p_isotp_twai_destroy_channel(s_link.physical);
    if (err == ESP_OK) {
        err = err2;
    }
    memset(&s_link, 0, sizeof(s_link));
    return err;
}

bool e32p_obd2_isotp_running(void)
{
    return s_link.started;
}

uint32_t e32p_obd2_isotp_dropped(void)
{
    return s_link.dropped;
}
