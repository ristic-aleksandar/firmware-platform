/**
 * @file e32p_uds_isotp.c
 * @brief Transport glue: two ISO-TP channels, one UDS server, one task that owns it.
 *
 * Why a task of its own, unlike the OBD-II glue: the UDS server needs poll() on a
 * steady beat (S3, the 0x78 cadence, the pending cap) and must accept complete()
 * from other tasks, but the ISO-TP link offers no periodic hook and serialises its
 * engine with a lock it does not export. So the pump only copies requests into a
 * queue; this task feeds the server, polls it and sends answers with send_sync —
 * the one send that is safe outside the pump. One task owns the server: no lock.
 *
 * Only the physical channel transmits. A broadcast request is answered from the
 * ECU's own physical response ID, exactly as the OBD-II glue does.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "e32p_isotp.h"
#include "e32p_isotp_twai.h"
#include "e32p_uds_isotp.h"

static const char *TAG = "e32p_uds";

#define FUNCTIONAL_ID_11   0x7DFu
#define FUNCTIONAL_TA_29   0x33u
#define REQ_ID_11(ecu)     (0x7E0u + ((ecu) - 0x10u))
#define RESP_ID_11(ecu)    (0x7E8u + ((ecu) - 0x10u))
#define REQ_QUEUE_DEPTH    3u
#define CMPL_QUEUE_DEPTH   2u
#define SEND_TIMEOUT_MS    1000u
#define STOP_TIMEOUT_MS    1000u
#define STMIN_MAX_MS       127u   /* STmin 0x00..0x7F is milliseconds (ISO 15765-2) */

typedef struct {
    bool                  stop;          /* tells the task to leave its loop */
    e32p_uds_addressing_t addressing;
    uint16_t              len;
    uint8_t               data[E32P_UDS_MAX_BLOCK_LEN];
} req_item_t;

typedef struct {
    e32p_uds_nrc_t nrc;
    uint16_t       len;
    uint8_t        data[E32P_UDS_ISOTP_COMPLETE_MAX];
} cmpl_item_t;

static struct {
    bool                running;
    e32p_uds_handle_t   server;
    e32p_isotp_handle_t physical;
    e32p_isotp_handle_t functional;
    QueueHandle_t       req_q;
    QueueHandle_t       cmpl_q;
    SemaphoreHandle_t   done;
    TaskHandle_t        task;
    uint16_t            poll_ms;
    uint32_t            dropped;
    req_item_t          pump_item;       /* only the pump task fills this  */
    req_item_t          task_item;       /* only the glue task reads this  */
} s;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---- pump side ---------------------------------------------------------------- */

static void enqueue(const uint8_t *data, uint16_t len, e32p_uds_addressing_t addressing)
{
    if (len == 0u || len > E32P_UDS_MAX_BLOCK_LEN) {
        return;
    }
    s.pump_item.stop = false;
    s.pump_item.addressing = addressing;
    s.pump_item.len = len;
    memcpy(s.pump_item.data, data, len);
    if (xQueueSend(s.req_q, &s.pump_item, 0) != pdTRUE) {
        s.dropped++;
        ESP_LOGW(TAG, "request dropped, queue full (total %u)", (unsigned)s.dropped);
    }
}

static void on_physical(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    enqueue(data, len, E32P_UDS_ADDR_PHYSICAL);
}

static void on_functional(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    enqueue(data, len, E32P_UDS_ADDR_FUNCTIONAL);
}

/* ---- glue task ---------------------------------------------------------------- */

static esp_err_t send_answer(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    const esp_err_t err = e32p_isotp_twai_send_sync(s.physical, data, len, SEND_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "answer 0x%02X not sent: %s", data[0], esp_err_to_name(err));
    }
    return err;
}

static void glue_task(void *arg)
{
    (void)arg;
    const TickType_t wait = pdMS_TO_TICKS(s.poll_ms) > 0 ? pdMS_TO_TICKS(s.poll_ms) : 1;
    cmpl_item_t cmpl;
    for (;;) {
        if (xQueueReceive(s.req_q, &s.task_item, wait) == pdTRUE) {
            if (s.task_item.stop) {
                break;
            }
            (void)e32p_uds_request(s.server, s.task_item.addressing, s.task_item.data,
                                   s.task_item.len, now_ms());
        }
        while (xQueueReceive(s.cmpl_q, &cmpl, 0) == pdTRUE) {
            (void)e32p_uds_complete(s.server, cmpl.nrc, cmpl.len ? cmpl.data : NULL, cmpl.len, now_ms());
        }
        (void)e32p_uds_poll(s.server, now_ms());
    }
    xSemaphoreGive(s.done);
    vTaskDelete(NULL);
}

/* ---- API ---------------------------------------------------------------------- */

static void cleanup(void)
{
    if (s.functional != NULL) {
        (void)e32p_isotp_twai_destroy_channel(s.functional);
    }
    if (s.physical != NULL) {
        (void)e32p_isotp_twai_destroy_channel(s.physical);
    }
    (void)e32p_uds_deinit(s.server);
    if (s.req_q != NULL) {
        vQueueDelete(s.req_q);
    }
    if (s.cmpl_q != NULL) {
        vQueueDelete(s.cmpl_q);
    }
    if (s.done != NULL) {
        vSemaphoreDelete(s.done);
    }
    memset(&s, 0, sizeof(s));
}

esp_err_t e32p_uds_isotp_start(const e32p_uds_isotp_config_t *cfg)
{
    E32P_CHECK_ARG(cfg != NULL && cfg->server != NULL, TAG);
    E32P_CHECK_ARG(cfg->rx_stmin_ms <= STMIN_MAX_MS, TAG);
    E32P_RETURN_ON_FALSE(!s.running, ESP_ERR_INVALID_STATE, TAG, "already running");
    E32P_RETURN_ON_FALSE(e32p_isotp_twai_running(), ESP_ERR_INVALID_STATE, TAG,
                         "start the ISO-TP link first");
    const uint8_t ecu = cfg->ecu_addr ? cfg->ecu_addr : E32P_UDS_ECU_ADDR_DEFAULT;
    const uint8_t tester = cfg->tester_addr ? cfg->tester_addr : E32P_UDS_TESTER_ADDR_DEFAULT;
    E32P_CHECK_ARG(ecu >= 0x10u && ecu <= 0x17u, TAG);

    memset(&s, 0, sizeof(s));
    s.poll_ms = cfg->poll_ms ? cfg->poll_ms : 5u;
    s.req_q = xQueueCreate(REQ_QUEUE_DEPTH, sizeof(req_item_t));
    s.cmpl_q = xQueueCreate(CMPL_QUEUE_DEPTH, sizeof(cmpl_item_t));
    s.done = xSemaphoreCreateBinary();
    if (s.req_q == NULL || s.cmpl_q == NULL || s.done == NULL) {
        cleanup();
        return ESP_ERR_NO_MEM;
    }

    e32p_uds_config_t server_cfg = *cfg->server;
    server_cfg.send = send_answer;
    esp_err_t err = e32p_uds_init(&server_cfg, &s.server);
    if (err != ESP_OK) {
        cleanup();
        return err;
    }

    e32p_isotp_config_t phys;
    e32p_isotp_config_defaults(&phys, RESP_ID_11(ecu), REQ_ID_11(ecu), ecu, tester, false);
    phys.max_msg_bytes = E32P_UDS_MAX_BLOCK_LEN;
    phys.rx_stmin_ms = cfg->rx_stmin_ms;             /* our Flow Control asks for this gap */
    phys.on_rx = on_physical;
    err = e32p_isotp_twai_channel(&phys, &s.physical);
    if (err == ESP_OK && !cfg->no_functional) {
        /* Receive-only; in 29-bit mode the source is the functional address 0x33
           and the target the tester, as in the OBD-II glue. */
        e32p_isotp_config_t func;
        e32p_isotp_config_defaults(&func, RESP_ID_11(ecu), FUNCTIONAL_ID_11, FUNCTIONAL_TA_29,
                                   tester, true);
        func.max_msg_bytes = 64u;   /* broadcasts are single frames */
        func.on_rx = on_functional;
        err = e32p_isotp_twai_channel(&func, &s.functional);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "channel setup failed: %s", esp_err_to_name(err));
        cleanup();
        return err;
    }

    const uint32_t stack = cfg->task_stack ? cfg->task_stack : 4096u;
    const int prio = cfg->task_prio ? cfg->task_prio : 5;
    if (xTaskCreate(glue_task, "uds", stack, NULL, prio, &s.task) != pdPASS) {
        cleanup();
        return ESP_ERR_NO_MEM;
    }
    s.running = true;
    ESP_LOGI(TAG, "uds on the bus: physical rx=0x%X tx=0x%X%s (%s)",
             (unsigned)e32p_isotp_rx_id(s.physical), (unsigned)RESP_ID_11(ecu),
             s.functional ? ", broadcasts too" : ", no broadcasts",
             e32p_isotp_is_extended(s.physical) ? "29-bit" : "11-bit");
    return ESP_OK;
}

esp_err_t e32p_uds_isotp_stop(void)
{
    if (!s.running) {
        return ESP_OK;
    }
    /* Detach first so the pump stops feeding the queue, then stop the task. */
    if (s.functional != NULL) {
        (void)e32p_isotp_twai_destroy_channel(s.functional);
        s.functional = NULL;
    }
    (void)e32p_isotp_twai_destroy_channel(s.physical);
    s.physical = NULL;
    static req_item_t stop_item;
    stop_item.stop = true;
    (void)xQueueSend(s.req_q, &stop_item, pdMS_TO_TICKS(STOP_TIMEOUT_MS));
    if (xSemaphoreTake(s.done, pdMS_TO_TICKS(STOP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "uds task did not stop");
        return ESP_ERR_TIMEOUT;
    }
    cleanup();
    return ESP_OK;
}

bool e32p_uds_isotp_running(void)
{
    return s.running;
}

esp_err_t e32p_uds_isotp_complete(e32p_uds_nrc_t nrc, const uint8_t *data, uint16_t len)
{
    E32P_CHECK_ARG(len <= E32P_UDS_ISOTP_COMPLETE_MAX && (len == 0u || data != NULL), TAG);
    E32P_RETURN_ON_FALSE(s.running, ESP_ERR_INVALID_STATE, TAG, "not running");
    cmpl_item_t item = { .nrc = nrc, .len = len };
    if (len > 0u) {
        memcpy(item.data, data, len);
    }
    return (xQueueSend(s.cmpl_q, &item, pdMS_TO_TICKS(100)) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

uint32_t e32p_uds_isotp_dropped(void)
{
    return s.dropped;
}
