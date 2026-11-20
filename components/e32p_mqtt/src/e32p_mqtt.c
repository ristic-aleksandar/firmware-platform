/**
 * @file e32p_mqtt.c
 * @brief e32p_mqtt implementation — pure topic matching + esp-mqtt client facade.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "e32p_mqtt.h"

static const char *TAG = "e32p_mqtt";

struct e32p_mqtt_ctx {
    esp_mqtt_client_handle_t client;
    e32p_mqtt_event_cb_t     cb;
    void                    *user;
    volatile bool            connected;
};

/* ---- Pure helpers --------------------------------------------------------- */

bool e32p_mqtt_topic_matches(const char *filter, const char *topic)
{
    if (filter == NULL || topic == NULL) {
        return false;
    }
    /* A filter beginning with a wildcard never matches a $-prefixed topic. */
    if (topic[0] == '$' && (filter[0] == '#' || filter[0] == '+')) {
        return false;
    }

    while (*filter) {
        if (*filter == '#') {
            return true;   /* multi-level wildcard: matches the rest, incl. parent */
        }

        /* Current filter segment [fseg, filter). */
        const char *fseg = filter;
        while (*filter && *filter != '/') {
            filter++;
        }
        size_t flen = (size_t)(filter - fseg);

        /* Current topic segment [tseg, topic). */
        const char *tseg = topic;
        while (*topic && *topic != '/') {
            topic++;
        }
        size_t tlen = (size_t)(topic - tseg);

        bool plus = (flen == 1 && fseg[0] == '+');
        if (!plus && (flen != tlen || strncmp(fseg, tseg, flen) != 0)) {
            return false;   /* literal segment mismatch */
        }

        if (*filter == '/') {
            filter++;   /* consume the separator */
        }
        if (*topic == '/') {
            topic++;
        } else {
            /* Topic exhausted. Match iff the filter is also done, or only a
               trailing "#" remains (which matches the parent level). */
            return (*filter == '\0') || (strcmp(filter, "#") == 0);
        }
    }
    /* Filter exhausted: the topic must be too. */
    return (*topic == '\0');
}

const char *e32p_mqtt_event_type_str(e32p_mqtt_event_type_t type)
{
    switch (type) {
    case E32P_MQTT_EVENT_CONNECTED:    return "connected";
    case E32P_MQTT_EVENT_DISCONNECTED: return "disconnected";
    case E32P_MQTT_EVENT_SUBSCRIBED:   return "subscribed";
    case E32P_MQTT_EVENT_UNSUBSCRIBED: return "unsubscribed";
    case E32P_MQTT_EVENT_PUBLISHED:    return "published";
    case E32P_MQTT_EVENT_DATA:         return "data";
    case E32P_MQTT_EVENT_ERROR:        return "error";
    default:                           return "other";
    }
}

/* ---- Event bridge --------------------------------------------------------- */

static e32p_mqtt_event_type_t map_event(int32_t id)
{
    switch (id) {
    case MQTT_EVENT_CONNECTED:    return E32P_MQTT_EVENT_CONNECTED;
    case MQTT_EVENT_DISCONNECTED: return E32P_MQTT_EVENT_DISCONNECTED;
    case MQTT_EVENT_SUBSCRIBED:   return E32P_MQTT_EVENT_SUBSCRIBED;
    case MQTT_EVENT_UNSUBSCRIBED: return E32P_MQTT_EVENT_UNSUBSCRIBED;
    case MQTT_EVENT_PUBLISHED:    return E32P_MQTT_EVENT_PUBLISHED;
    case MQTT_EVENT_DATA:         return E32P_MQTT_EVENT_DATA;
    case MQTT_EVENT_ERROR:        return E32P_MQTT_EVENT_ERROR;
    default:                      return E32P_MQTT_EVENT_OTHER;
    }
}

static void on_mqtt_event(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)base;
    struct e32p_mqtt_ctx *ctx = args;
    esp_mqtt_event_handle_t e = event_data;

    if (event_id == MQTT_EVENT_CONNECTED) {
        ctx->connected = true;
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ctx->connected = false;
    }

    if (ctx->cb != NULL) {
        e32p_mqtt_event_t ev = {
            .type      = map_event(event_id),
            .topic     = e ? e->topic : NULL,
            .topic_len = e ? e->topic_len : 0,
            .data      = e ? e->data : NULL,
            .data_len  = e ? e->data_len : 0,
            .msg_id    = e ? e->msg_id : 0,
            .retain    = (e && event_id == MQTT_EVENT_DATA) ? e->retain : false,
        };
        ctx->cb(&ev, ctx->user);
    }
}

/* ---- Lifecycle ------------------------------------------------------------ */

esp_err_t e32p_mqtt_init(const e32p_mqtt_config_t *cfg, e32p_mqtt_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->broker_uri != NULL, TAG);

    esp_mqtt_client_config_t mc = { 0 };
    mc.broker.address.uri = cfg->broker_uri;
    if (cfg->client_id) {
        mc.credentials.client_id = cfg->client_id;
    }
    if (cfg->username) {
        mc.credentials.username = cfg->username;
    }
    if (cfg->password) {
        mc.credentials.authentication.password = cfg->password;
    }
    if (cfg->keepalive_sec > 0) {
        mc.session.keepalive = cfg->keepalive_sec;
    }

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mc);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed for %s", cfg->broker_uri);
        return ESP_FAIL;
    }

    struct e32p_mqtt_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        esp_mqtt_client_destroy(client);
        return ESP_ERR_NO_MEM;
    }
    ctx->client = client;
    ctx->cb     = cfg->cb;
    ctx->user   = cfg->user;

    esp_err_t err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, on_mqtt_event, ctx);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(client);
        free(ctx);
        return err;
    }

    ESP_LOGI(TAG, "mqtt client for %s", cfg->broker_uri);
    *out = ctx;
    return ESP_OK;
}

esp_err_t e32p_mqtt_start(e32p_mqtt_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    return esp_mqtt_client_start(h->client);
}

esp_err_t e32p_mqtt_stop(e32p_mqtt_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    return esp_mqtt_client_stop(h->client);
}

esp_err_t e32p_mqtt_deinit(e32p_mqtt_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    esp_mqtt_client_destroy(h->client);   /* stops the client too */
    free(h);
    return ESP_OK;
}

/* ---- Operations ----------------------------------------------------------- */

esp_err_t e32p_mqtt_publish(e32p_mqtt_handle_t h, const char *topic, const void *data,
                            int len, int qos, bool retain, int *out_msg_id)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(topic != NULL, TAG);
    E32P_CHECK_ARG(qos >= 0 && qos <= 2, TAG);

    int id = esp_mqtt_client_publish(h->client, topic, (const char *)data, len, qos, retain ? 1 : 0);
    if (out_msg_id) {
        *out_msg_id = id;
    }
    return (id < 0) ? ESP_FAIL : ESP_OK;
}

esp_err_t e32p_mqtt_subscribe(e32p_mqtt_handle_t h, const char *topic, int qos, int *out_msg_id)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(topic != NULL, TAG);
    E32P_CHECK_ARG(qos >= 0 && qos <= 2, TAG);

    int id = esp_mqtt_client_subscribe_single(h->client, topic, qos);
    if (out_msg_id) {
        *out_msg_id = id;
    }
    return (id < 0) ? ESP_FAIL : ESP_OK;
}

esp_err_t e32p_mqtt_unsubscribe(e32p_mqtt_handle_t h, const char *topic, int *out_msg_id)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(topic != NULL, TAG);

    int id = esp_mqtt_client_unsubscribe(h->client, topic);
    if (out_msg_id) {
        *out_msg_id = id;
    }
    return (id < 0) ? ESP_FAIL : ESP_OK;
}

bool e32p_mqtt_is_connected(e32p_mqtt_handle_t h)
{
    return (h != NULL) && h->connected;
}
