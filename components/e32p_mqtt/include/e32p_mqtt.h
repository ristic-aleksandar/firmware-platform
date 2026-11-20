#pragma once
/**
 * @file e32p_mqtt.h
 * @brief MQTT v3.1.1 client facade over ESP-IDF's esp-mqtt (mqtt_client).
 *
 * Wraps client lifecycle, publish/subscribe, and connection state behind a small
 * opaque handle, and surfaces a compact event enum through one user callback.
 * The MQTT topic-filter matching (the +/# wildcard rules) is exposed as a pure
 * function so it can be unit-tested headless — the network path needs a broker.
 *
 * The public header deliberately does NOT include mqtt_client.h: callers depend
 * only on e32p types, so the module can be cherry-picked without leaking the
 * esp-mqtt API.
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Connection / message events delivered to the user callback. */
typedef enum {
    E32P_MQTT_EVENT_CONNECTED = 0,  /*!< Session established with the broker. */
    E32P_MQTT_EVENT_DISCONNECTED,   /*!< Transport/session lost. */
    E32P_MQTT_EVENT_SUBSCRIBED,     /*!< Broker acked a SUBSCRIBE. */
    E32P_MQTT_EVENT_UNSUBSCRIBED,   /*!< Broker acked an UNSUBSCRIBE. */
    E32P_MQTT_EVENT_PUBLISHED,      /*!< Broker acked a QoS>0 PUBLISH. */
    E32P_MQTT_EVENT_DATA,           /*!< An inbound message (topic + payload). */
    E32P_MQTT_EVENT_ERROR,          /*!< Transport/protocol error. */
    E32P_MQTT_EVENT_OTHER,          /*!< Any other esp-mqtt event. */
} e32p_mqtt_event_type_t;

/** @brief One event passed to the user callback. Pointers are only valid for
 *         the duration of the callback (esp-mqtt owns the buffers). */
typedef struct {
    e32p_mqtt_event_type_t type;
    const char *topic;      /*!< DATA: topic (NOT null-terminated; use topic_len). */
    int         topic_len;
    const char *data;       /*!< DATA: payload (NOT null-terminated; use data_len). */
    int         data_len;
    int         msg_id;     /*!< SUBSCRIBED/PUBLISHED/etc: the message id. */
    bool        retain;     /*!< DATA: the broker flagged the message as retained (a stored
                                 message replayed at subscribe, not a new one).
                                 For a message split over several DATA events, every
                                 fragment carries the same flag; false for every non-DATA event. */
} e32p_mqtt_event_t;

/** @brief User event callback. Runs in the esp-mqtt task context. */
typedef void (*e32p_mqtt_event_cb_t)(const e32p_mqtt_event_t *ev, void *user);

/** @brief Client configuration. Strings are borrowed — keep them alive while the
 *         client exists (esp-mqtt copies them at init). */
typedef struct {
    const char *broker_uri;    /*!< e.g. "mqtt://host:1883" or "mqtts://host:8883". */
    const char *client_id;     /*!< NULL -> esp-mqtt auto-generates one. */
    const char *username;      /*!< Optional. */
    const char *password;      /*!< Optional. */
    int         keepalive_sec; /*!< 0 -> esp-mqtt default (120 s). */
    e32p_mqtt_event_cb_t cb;   /*!< Optional event callback. */
    void       *user;          /*!< Passed back to @p cb. */
} e32p_mqtt_config_t;

/** @brief Opaque per-client handle. */
typedef struct e32p_mqtt_ctx *e32p_mqtt_handle_t;

/* ---- Pure helpers (no client; unit-testable) ------------------------------ */

/**
 * @brief Test an MQTT topic filter against a concrete topic name (MQTT 3.1.1 §4.7).
 *
 * Supports the `+` single-level and `#` multi-level wildcards, including that a
 * trailing `#` also matches the parent level, and that a filter beginning with a
 * wildcard does not match a topic beginning with `$`.
 *
 * @param[in] filter  Subscription filter (may contain `+` / `#`).
 * @param[in] topic   Concrete topic name (no wildcards).
 * @return true if @p filter matches @p topic. false if either is NULL.
 */
bool e32p_mqtt_topic_matches(const char *filter, const char *topic);

/** @brief Human-readable name for an event type (never NULL). */
const char *e32p_mqtt_event_type_str(e32p_mqtt_event_type_t type);

/* ---- Lifecycle ------------------------------------------------------------ */

/**
 * @brief Create a client (does not connect yet).
 * @param[in]  cfg  Configuration (cfg and cfg->broker_uri must be non-NULL).
 * @param[out] out  Receives the handle.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, or ESP_FAIL.
 */
esp_err_t e32p_mqtt_init(const e32p_mqtt_config_t *cfg, e32p_mqtt_handle_t *out);

/** @brief Start the client task and begin connecting. */
esp_err_t e32p_mqtt_start(e32p_mqtt_handle_t h);

/** @brief Stop the client (disconnect). */
esp_err_t e32p_mqtt_stop(e32p_mqtt_handle_t h);

/** @brief Stop and destroy the client (NULL is a no-op). */
esp_err_t e32p_mqtt_deinit(e32p_mqtt_handle_t h);

/* ---- Operations ----------------------------------------------------------- */

/**
 * @brief Publish a message.
 * @param[in]  h           Valid handle.
 * @param[in]  topic       Topic name (must not be NULL).
 * @param[in]  data        Payload (may be NULL if @p len is 0).
 * @param[in]  len         Payload length; if <= 0 and @p data is a C string,
 *                         esp-mqtt uses strlen(data).
 * @param[in]  qos         0, 1, or 2.
 * @param[in]  retain      Set the broker retain flag.
 * @param[out] out_msg_id  Receives the message id (may be NULL).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_FAIL (enqueue failed).
 */
esp_err_t e32p_mqtt_publish(e32p_mqtt_handle_t h, const char *topic, const void *data,
                            int len, int qos, bool retain, int *out_msg_id);

/**
 * @brief Subscribe to a topic filter.
 * @param[out] out_msg_id  Receives the message id (may be NULL).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_FAIL.
 */
esp_err_t e32p_mqtt_subscribe(e32p_mqtt_handle_t h, const char *topic, int qos, int *out_msg_id);

/**
 * @brief Unsubscribe from a topic filter.
 * @param[out] out_msg_id  Receives the message id (may be NULL).
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_FAIL.
 */
esp_err_t e32p_mqtt_unsubscribe(e32p_mqtt_handle_t h, const char *topic, int *out_msg_id);

/** @brief True if the client currently has a live broker session. */
bool e32p_mqtt_is_connected(e32p_mqtt_handle_t h);

#ifdef __cplusplus
}
#endif
