/**
 * @file e32p_uds_ota.c
 * @brief e32p_uds download sink and erase / check routines on top of e32p_ota.
 *
 * Erasing a 1.5 MB slot and validating an image both take far longer than the 50 ms
 * a UDS server has to answer, so each runs in a short-lived worker task while the
 * server answers 0x78, and the worker reports through e32p_uds_isotp_complete().
 * While a worker runs, the server answers every other supported service 0x21 (an
 * unsupported one stays 0x11), so the sink callbacks never run alongside it. The one
 * exception is an abort from the pending cap: then the worker owns the handle and
 * cleans up itself.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "e32p_ota.h"
#include "e32p_uds_isotp.h"
#include "e32p_uds_ota.h"

static const char *TAG = "e32p_uds";

#define WORKER_STACK 4096u
#define WORKER_PRIO  4
#define STATUS_OK    0x00u
#define STATUS_FAIL  0x01u

static struct {
    e32p_ota_handle_t h;               /* the slot being written, once erased    */
    volatile bool     busy;            /* a worker owns the handle               */
    volatile bool     abort_requested; /* the pending cap fired during the work  */
    bool              erased;          /* the last erase succeeded               */
    bool              validated;       /* the last image was accepted for boot   */
    bool              erase_started;   /* 31 01 FF00 ran since boot              */
    bool              check_started;   /* 31 01 FF01 ran since boot              */
} s_ota;

static e32p_uds_nrc_t sink_begin(uint32_t size, void *user);
static e32p_uds_nrc_t sink_write(const uint8_t *data, uint16_t len, void *user);
static e32p_uds_nrc_t sink_end(void *user);
static void sink_abort(void *user);

static e32p_uds_sink_t s_sink = { 0u, sink_begin, sink_write, sink_end, sink_abort };

/* ---- workers ------------------------------------------------------------------ */

static void erase_worker(void *arg)
{
    (void)arg;
    e32p_ota_handle_t h = NULL;
    const esp_err_t err = e32p_ota_begin(&h);   /* erases the inactive slot */
    if (err == ESP_OK && s_ota.abort_requested) {
        (void)e32p_ota_abort(h);
        h = NULL;
    }
    s_ota.h = (err == ESP_OK) ? h : NULL;
    s_ota.erased = (s_ota.h != NULL);
    s_ota.abort_requested = false;
    s_ota.busy = false;
    const uint8_t status = s_ota.erased ? STATUS_OK : STATUS_FAIL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
    }
    (void)e32p_uds_isotp_complete(err == ESP_OK ? E32P_UDS_NRC_OK : E32P_UDS_NRC_PROGRAMMING_FAILURE,
                                  &status, 1u);
    vTaskDelete(NULL);
}

static void validate_worker(void *arg)
{
    (void)arg;
    const esp_err_t err = e32p_ota_end(s_ota.h);   /* validates, sets boot, frees */
    s_ota.h = NULL;
    s_ota.erased = false;
    s_ota.validated = (err == ESP_OK);
    s_ota.abort_requested = false;
    s_ota.busy = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image rejected: %s", esp_err_to_name(err));
    }
    (void)e32p_uds_isotp_complete(err == ESP_OK ? E32P_UDS_NRC_OK : E32P_UDS_NRC_PROGRAMMING_FAILURE,
                                  NULL, 0u);
    vTaskDelete(NULL);
}

static bool spawn(TaskFunction_t fn, const char *name)
{
    s_ota.busy = true;
    if (xTaskCreate(fn, name, WORKER_STACK, NULL, WORKER_PRIO, NULL) != pdPASS) {
        s_ota.busy = false;
        return false;
    }
    return true;
}

/* ---- sink --------------------------------------------------------------------- */

static e32p_uds_nrc_t sink_begin(uint32_t size, void *user)
{
    (void)user;
    if (s_ota.busy || s_ota.h == NULL || !s_ota.erased) {
        return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;   /* erase first: 31 01 FF00 */
    }
    if (size > s_sink.capacity) {
        return E32P_UDS_NRC_DOWNLOAD_NOT_ACCEPTED;
    }
    s_ota.validated = false;
    return E32P_UDS_NRC_OK;
}

static e32p_uds_nrc_t sink_write(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    if (s_ota.h == NULL) {
        return E32P_UDS_NRC_PROGRAMMING_FAILURE;
    }
    return (e32p_ota_write(s_ota.h, data, len) == ESP_OK) ? E32P_UDS_NRC_OK
                                                          : E32P_UDS_NRC_PROGRAMMING_FAILURE;
}

static e32p_uds_nrc_t sink_end(void *user)
{
    (void)user;
    if (s_ota.h == NULL) {
        return E32P_UDS_NRC_PROGRAMMING_FAILURE;
    }
    return spawn(validate_worker, "uds_validate") ? E32P_UDS_NRC_PENDING
                                                  : E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;
}

static void sink_abort(void *user)
{
    (void)user;
    if (s_ota.busy) {
        s_ota.abort_requested = true;   /* the worker holds the handle */
        return;
    }
    if (s_ota.h != NULL) {
        (void)e32p_ota_abort(s_ota.h);
        s_ota.h = NULL;
    }
    s_ota.erased = false;
}

const e32p_uds_sink_t *e32p_uds_ota_sink(void)
{
    if (s_sink.capacity == 0u) {
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        s_sink.capacity = (next != NULL) ? next->size : 0u;
    }
    return &s_sink;
}

/* ---- routines ----------------------------------------------------------------- */

e32p_uds_nrc_t e32p_uds_ota_routine(uint16_t rid, uint8_t control, const uint8_t *option,
                                    uint16_t option_len, uint8_t *status, uint16_t status_cap,
                                    uint16_t *status_len, void *user)
{
    (void)option;
    (void)option_len;
    (void)user;
    if (status_cap < 1u) {
        return E32P_UDS_NRC_GENERAL_REJECT;
    }
    if (rid == E32P_UDS_RID_ERASE_MEMORY) {
        if (control == 0x03u) {                       /* results */
            if (!s_ota.erase_started) {
                return E32P_UDS_NRC_SEQUENCE_ERROR;   /* no results before a start (0x24) */
            }
            status[0] = s_ota.erased ? STATUS_OK : STATUS_FAIL;
            *status_len = 1u;
            return s_ota.busy ? E32P_UDS_NRC_SEQUENCE_ERROR : E32P_UDS_NRC_OK;
        }
        if (s_ota.busy) {
            return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;
        }
        if (s_ota.h != NULL) {                        /* a fresh erase drops the old one */
            (void)e32p_ota_abort(s_ota.h);
            s_ota.h = NULL;
        }
        s_ota.erased = false;
        s_ota.validated = false;
        *status_len = 0u;                             /* the worker's status comes later */
        if (!spawn(erase_worker, "uds_erase")) {
            return E32P_UDS_NRC_CONDITIONS_NOT_CORRECT;
        }
        s_ota.erase_started = true;
        return E32P_UDS_NRC_PENDING;
    }
    if (rid == E32P_UDS_RID_CHECK_PROGRAMMING) {
        if (control == 0x03u) {                       /* results */
            if (!s_ota.check_started) {
                return E32P_UDS_NRC_SEQUENCE_ERROR;
            }
        } else {
            s_ota.check_started = true;
        }
        status[0] = s_ota.validated ? STATUS_OK : STATUS_FAIL;
        *status_len = 1u;
        return E32P_UDS_NRC_OK;
    }
    return E32P_UDS_NRC_OUT_OF_RANGE;
}
