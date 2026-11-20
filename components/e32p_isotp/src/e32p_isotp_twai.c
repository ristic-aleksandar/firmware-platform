/**
 * @file e32p_isotp_twai.c
 * @brief e32p_isotp bound to the single TWAI controller: pump task, ID routing,
 *        blocking helpers. Thin — all protocol logic lives in the engine.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "e32p_time.h"
#include "e32p_isotp_twai.h"

static const char *TAG = "e32p_isotp_twai";

#ifndef CONFIG_E32P_ISOTP_MAX_CHANNELS
#  define CONFIG_E32P_ISOTP_MAX_CHANNELS 4
#endif
#ifndef CONFIG_E32P_TWAI_RX_QUEUE_LEN
#  define E32P_ISOTP_DRAIN_MAX 5
#else
#  define E32P_ISOTP_DRAIN_MAX CONFIG_E32P_TWAI_RX_QUEUE_LEN
#endif

#define PUMP_BLOCK_MS  ((uint32_t)portTICK_PERIOD_MS)
#define JOIN_BUDGET_MS 100u
#define DEF_STACK      4096u
#define DEF_PRIO       10

/* The pump must BLOCK, never spin: a free-running task at this priority starves
   the idle task and trips the task watchdog. Guarded so a tick-rate change is a
   build error rather than a bench mystery. */
E32P_ASSERT_BLOCKING_MS(PUMP_BLOCK_MS);

typedef struct {
    e32p_isotp_handle_t  h;
    bool                 in_use;
    bool                 sync_rx;      /* created with on_rx == NULL */

    SemaphoreHandle_t    tx_done;      /* given when a transfer finishes */
    esp_err_t            tx_status;

    /* Sync-mode receive pool. One producer (pump), one consumer (recv_sync),
       so a slot ring plus an index queue needs no extra locking. Preallocated
       so the pump never mallocs. */
    QueueHandle_t        idx_q;
    uint8_t             *pool;         /* depth * msg_max bytes */
    uint16_t            *lens;
    uint16_t             msg_max;
    uint8_t              depth;
    uint8_t              next_slot;

    e32p_isotp_rx_fn     app_on_rx;    /* server mode */
    e32p_isotp_txdone_fn app_on_txdone;
    void                *app_user;
} chan_t;

static struct {
    bool              started;
    volatile bool     stop_req;
    TaskHandle_t      task;
    SemaphoreHandle_t done;      /* given by the pump as its last action */
    SemaphoreHandle_t lock;      /* recursive: serialises engine access   */
    uint32_t          block_ms;
    uint8_t           depth;
    chan_t            ch[CONFIG_E32P_ISOTP_MAX_CHANNELS];
} s_link;

#define LOCK()   xSemaphoreTakeRecursive(s_link.lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGiveRecursive(s_link.lock)

static chan_t *find_chan(e32p_isotp_handle_t h)
{
    for (int i = 0; i < CONFIG_E32P_ISOTP_MAX_CHANNELS; i++) {
        if (s_link.ch[i].in_use && s_link.ch[i].h == h) {
            return &s_link.ch[i];
        }
    }
    return NULL;
}

/** @brief Like find_chan, but also finds a detached record. stop() detaches every
 *         channel without freeing it, so destroy must still find it afterwards -
 *         otherwise the buffers and semaphores leak. */
static chan_t *find_chan_any(e32p_isotp_handle_t h)
{
    for (int i = 0; i < CONFIG_E32P_ISOTP_MAX_CHANNELS; i++) {
        if (s_link.ch[i].h == h) {
            return &s_link.ch[i];
        }
    }
    return NULL;
}

/* ========================= engine-facing callbacks ====================== */

static esp_err_t sink_twai(const e32p_twai_message_t *frame, void *user)
{
    (void)user;
    /* Non-blocking: a full TX queue is back-pressure, and the engine resumes
       the remainder on the next poll. Blocking here would stall the pump and
       with it every other channel's timers. */
    return e32p_twai_transmit(frame, 0);
}

static void on_rx_trampoline(const uint8_t *data, uint16_t len, void *user)
{
    chan_t *c = (chan_t *)user;

    if (!c->sync_rx) {
        if (c->app_on_rx != NULL) {
            c->app_on_rx(data, len, c->app_user);
        }
        return;
    }
    if (len > c->msg_max) {
        ESP_LOGW(TAG, "payload %u > channel max %u; dropped", len, c->msg_max);
        return;
    }
    const uint8_t slot = c->next_slot;
    memcpy(&c->pool[(size_t)slot * c->msg_max], data, len);
    c->lens[slot] = len;
    if (xQueueSend(c->idx_q, &slot, 0) != pdTRUE) {
        ESP_LOGW(TAG, "rx backlog full; dropped %u bytes", len);
        return;                       /* slot not advanced: reuse it */
    }
    c->next_slot = (uint8_t)((slot + 1u) % c->depth);
}

static void on_txdone_trampoline(esp_err_t status, void *user)
{
    chan_t *c = (chan_t *)user;
    c->tx_status = status;
    xSemaphoreGive(c->tx_done);
    if (c->app_on_txdone != NULL) {
        c->app_on_txdone(status, c->app_user);
    }
}

/* ================================ pump ================================= */

static void route(const e32p_twai_message_t *m)
{
    for (int i = 0; i < CONFIG_E32P_ISOTP_MAX_CHANNELS; i++) {
        chan_t *c = &s_link.ch[i];
        if (c->in_use && e32p_isotp_rx_id(c->h) == m->id &&
            e32p_isotp_is_extended(c->h) == m->extended) {
            e32p_isotp_on_frame(c->h, m);
            return;                   /* IDs are unique per link */
        }
    }
}

static void pump_task(void *arg)
{
    (void)arg;
    e32p_twai_message_t m;

    while (!s_link.stop_req) {
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        LOCK();
        for (int i = 0; i < CONFIG_E32P_ISOTP_MAX_CHANNELS; i++) {
            if (s_link.ch[i].in_use) {
                e32p_isotp_poll(s_link.ch[i].h, now);
            }
        }
        UNLOCK();

        /* Blocking receive: wakes on the RX interrupt, so this is not a
           latency floor - it only bounds how long an idle pump sleeps. */
        if (e32p_twai_receive(&m, s_link.block_ms) == ESP_OK) {
            LOCK();
            route(&m);
            /* Drain the burst, BOUNDED. An unbounded drain on a saturated bus
               never returns to poll(), which is what sends flow control and
               services N_Bs/N_Cr. poll() must run at least once per wake. */
            for (int k = 0; k < E32P_ISOTP_DRAIN_MAX &&
                            e32p_twai_receive(&m, 0) == ESP_OK; k++) {
                route(&m);
            }
            UNLOCK();
        }
    }

    /* Teardown contract: nothing TWAI-related after the loop, and the give is
       the LAST action, so the window before vTaskDelete is safe even if the
       caller proceeds immediately. */
    s_link.task = NULL;
    xSemaphoreGive(s_link.done);
    vTaskDelete(NULL);
}

/* ============================== public API ============================= */

esp_err_t e32p_isotp_twai_start(const e32p_isotp_twai_config_t *cfg)
{
    E32P_RETURN_ON_FALSE(!s_link.started, ESP_ERR_INVALID_STATE, TAG,
                         "link already started");

    const uint32_t block = (cfg && cfg->block_ms) ? cfg->block_ms : PUMP_BLOCK_MS;
    const uint32_t stack = (cfg && cfg->task_stack) ? cfg->task_stack : DEF_STACK;
    const int      prio  = (cfg && cfg->task_prio) ? cfg->task_prio : DEF_PRIO;

    memset(&s_link.ch, 0, sizeof(s_link.ch));
    s_link.depth    = (cfg && cfg->sync_depth) ? cfg->sync_depth : 1u;
    /* A configurable block time can round to zero ticks; clamp so a caller
       cannot turn the pump into a spin. */
    s_link.block_ms = block;
    if (pdMS_TO_TICKS(block) < 1) {
        ESP_LOGW(TAG, "block_ms=%lu rounds to 0 ticks; clamping to %lu",
                 (unsigned long)block, (unsigned long)PUMP_BLOCK_MS);
        s_link.block_ms = PUMP_BLOCK_MS;
    }

    if (s_link.done == NULL) { s_link.done = xSemaphoreCreateBinary(); }
    if (s_link.lock == NULL) { s_link.lock = xSemaphoreCreateRecursiveMutex(); }
    if (s_link.done == NULL || s_link.lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    while (xSemaphoreTake(s_link.done, 0) == pdTRUE) { }   /* drain stale gives */

    s_link.stop_req = false;
    s_link.started  = true;
    if (xTaskCreate(pump_task, "isotp_pump", stack, NULL, prio, &s_link.task)
        != pdPASS) {
        s_link.started = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "pump up: block=%lums prio=%d stack=%lu",
             (unsigned long)s_link.block_ms, prio, (unsigned long)stack);
    return ESP_OK;
}

esp_err_t e32p_isotp_twai_stop(void)
{
    if (!s_link.started || s_link.task == NULL) {
        s_link.started = false;
        return ESP_OK;                      /* idempotent */
    }
    s_link.stop_req = true;

    /* Hard join. The pump observes stop_req within one loop pass (its maximum
       block is block_ms), so this budget is ~50x headroom. */
    if (xSemaphoreTake(s_link.done, pdMS_TO_TICKS(JOIN_BUDGET_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "pump did not join in %ums - do NOT deinit TWAI",
                 (unsigned)JOIN_BUDGET_MS);
        return ESP_ERR_TIMEOUT;
    }
    LOCK();
    for (int i = 0; i < CONFIG_E32P_ISOTP_MAX_CHANNELS; i++) {
        s_link.ch[i].in_use = false;
    }
    UNLOCK();
    s_link.started = false;
    ESP_LOGI(TAG, "pump joined");
    return ESP_OK;
}

bool e32p_isotp_twai_running(void)
{
    return s_link.started && s_link.task != NULL;
}

esp_err_t e32p_isotp_twai_attach(e32p_isotp_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_RETURN_ON_FALSE(s_link.started, ESP_ERR_INVALID_STATE, TAG, "link stopped");

    LOCK();
    chan_t *slot = NULL;
    for (int i = 0; i < CONFIG_E32P_ISOTP_MAX_CHANNELS; i++) {
        chan_t *c = &s_link.ch[i];
        if (c->in_use && e32p_isotp_rx_id(c->h) == e32p_isotp_rx_id(h)) {
            UNLOCK();
            ESP_LOGE(TAG, "rx id 0x%lX already claimed",
                     (unsigned long)e32p_isotp_rx_id(h));
            return ESP_ERR_INVALID_STATE;
        }
        if (!c->in_use && slot == NULL) { slot = c; }
    }
    if (slot == NULL) {
        UNLOCK();
        return ESP_ERR_NO_MEM;
    }
    slot->h      = h;
    slot->in_use = true;
    UNLOCK();
    return ESP_OK;
}

esp_err_t e32p_isotp_twai_detach(e32p_isotp_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    LOCK();
    chan_t *c = find_chan(h);
    if (c != NULL) { c->in_use = false; }
    UNLOCK();
    return ESP_OK;
}

esp_err_t e32p_isotp_twai_channel(const e32p_isotp_config_t *cfg,
                                  e32p_isotp_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL && out != NULL, TAG);
    E32P_RETURN_ON_FALSE(s_link.started, ESP_ERR_INVALID_STATE, TAG, "link stopped");

    LOCK();
    chan_t *c = NULL;
    for (int i = 0; i < CONFIG_E32P_ISOTP_MAX_CHANNELS; i++) {
        if (!s_link.ch[i].in_use && s_link.ch[i].h == NULL) { c = &s_link.ch[i]; break; }
    }
    if (c == NULL) { UNLOCK(); return ESP_ERR_NO_MEM; }

    memset(c, 0, sizeof(*c));
    c->sync_rx       = (cfg->on_rx == NULL);
    c->app_on_rx     = cfg->on_rx;
    c->app_on_txdone = cfg->on_tx_done;
    c->app_user      = cfg->user;
    c->msg_max       = cfg->max_msg_bytes ? cfg->max_msg_bytes
                                          : (uint16_t)CONFIG_E32P_ISOTP_MAX_MSG_BYTES;
    c->depth         = s_link.depth;

    c->tx_done = xSemaphoreCreateBinary();
    if (c->tx_done == NULL) { UNLOCK(); return ESP_ERR_NO_MEM; }

    if (c->sync_rx) {
        c->pool  = malloc((size_t)c->depth * c->msg_max);
        c->lens  = malloc((size_t)c->depth * sizeof(uint16_t));
        c->idx_q = xQueueCreate(c->depth, sizeof(uint8_t));
        if (c->pool == NULL || c->lens == NULL || c->idx_q == NULL) {
            free(c->pool); free(c->lens);
            if (c->idx_q) { vQueueDelete(c->idx_q); }
            vSemaphoreDelete(c->tx_done);
            memset(c, 0, sizeof(*c));
            UNLOCK();
            return ESP_ERR_NO_MEM;
        }
    }

    /* The engine talks to the link's trampolines; the app's callbacks are
       chained from there. That is what makes send_sync/recv_sync possible
       without taking the callbacks away from a server-mode caller. */
    e32p_isotp_config_t ec = *cfg;
    ec.tx         = sink_twai;
    ec.on_rx      = on_rx_trampoline;
    ec.on_tx_done = on_txdone_trampoline;
    ec.user       = c;

    esp_err_t err = e32p_isotp_init(&ec, &c->h);
    if (err != ESP_OK) {
        free(c->pool); free(c->lens);
        if (c->idx_q) { vQueueDelete(c->idx_q); }
        vSemaphoreDelete(c->tx_done);
        memset(c, 0, sizeof(*c));
        UNLOCK();
        return err;
    }
    c->in_use = true;
    *out = c->h;
    UNLOCK();
    return ESP_OK;
}

esp_err_t e32p_isotp_twai_destroy_channel(e32p_isotp_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_RETURN_ON_FALSE(s_link.lock != NULL, ESP_ERR_INVALID_STATE, TAG, "link never started");
    LOCK();
    chan_t *c = find_chan_any(h);          /* works before or after stop() */
    if (c == NULL) { UNLOCK(); return ESP_ERR_INVALID_ARG; }
    c->in_use = false;
    e32p_isotp_deinit(c->h);
    free(c->pool); free(c->lens);
    if (c->idx_q)   { vQueueDelete(c->idx_q); }
    if (c->tx_done) { vSemaphoreDelete(c->tx_done); }
    memset(c, 0, sizeof(*c));
    UNLOCK();
    return ESP_OK;
}

esp_err_t e32p_isotp_twai_send_sync(e32p_isotp_handle_t h, const uint8_t *data,
                                    uint16_t len, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(h != NULL && data != NULL, TAG);
    /* Waiting here for the pump to make progress, from inside the pump, is a
       deadlock. Reject rather than hang - the message names the fix. */
    E32P_RETURN_ON_FALSE(xTaskGetCurrentTaskHandle() != s_link.task,
                         ESP_ERR_INVALID_STATE, TAG,
                         "send_sync called from a callback; use e32p_isotp_send");

    LOCK();
    chan_t *c = find_chan(h);
    if (c == NULL) { UNLOCK(); return ESP_ERR_INVALID_ARG; }
    while (xSemaphoreTake(c->tx_done, 0) == pdTRUE) { }   /* drain a stale give */
    c->tx_status = ESP_FAIL;
    esp_err_t err = e32p_isotp_send(h, data, len);
    UNLOCK();
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(c->tx_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return c->tx_status;
}

esp_err_t e32p_isotp_twai_recv_sync(e32p_isotp_handle_t h, uint8_t *buf,
                                    uint16_t cap, uint16_t *out_len,
                                    uint32_t timeout_ms)
{
    E32P_CHECK_ARG(h != NULL && buf != NULL && out_len != NULL, TAG);

    LOCK();
    chan_t *c = find_chan(h);
    UNLOCK();
    if (c == NULL) { return ESP_ERR_INVALID_ARG; }
    E32P_RETURN_ON_FALSE(c->sync_rx, ESP_ERR_INVALID_STATE, TAG,
                         "channel is in server mode (on_rx was set)");

    uint8_t slot;
    if (xQueueReceive(c->idx_q, &slot, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const uint16_t len = c->lens[slot];
    if (len > cap) {
        *out_len = len;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(buf, &c->pool[(size_t)slot * c->msg_max], len);
    *out_len = len;
    return ESP_OK;
}
