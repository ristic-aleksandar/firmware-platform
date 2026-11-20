# e32p_mqtt — MQTT v3.1.1 client

A small facade over ESP-IDF's [esp-mqtt](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/api-reference/protocols/mqtt.html):
connect, publish, subscribe, and receive — behind an opaque handle with one event
callback and a compact event enum.

## What it does

- **Lifecycle:** `init` → `start` → `stop` → `deinit`, plus `is_connected`.
- **Messaging:** `publish` (QoS/retain), `subscribe`, `unsubscribe`, each
  returning the broker message id.
- **Events:** one callback receives a compact `e32p_mqtt_event_t`
  (connected / data / published / …) instead of raw esp-mqtt event ids. A data
  event carries `retain`: true when the broker flagged the message as retained (a
  stored message replayed at subscribe), so a command handler can ignore it.
- **Topic matching:** a pure `e32p_mqtt_topic_matches()` implementing the MQTT
  `+` / `#` wildcard rules — useful for routing inbound messages yourself.

## Design

The awkward, spec-defined part — **topic-filter matching** — is a pure function,
so it is fully unit-tested headless (the `+`/`#` rules, a trailing `#` matching
the parent, and the "a wildcard filter never matches a `$` topic" rule). The
network path (`start`/`publish`/`subscribe`) wraps esp-mqtt and needs Wi-Fi and a
broker, so it is exercised by the `23_mqtt` demo rather than in CI. The public
header hides `mqtt_client.h`, so cherry-picking the module doesn't leak the
esp-mqtt API.

## API

```c
/* pure */
bool        e32p_mqtt_topic_matches(const char *filter, const char *topic);
const char *e32p_mqtt_event_type_str(e32p_mqtt_event_type_t type);

/* lifecycle */
esp_err_t e32p_mqtt_init(const e32p_mqtt_config_t *cfg, e32p_mqtt_handle_t *out);
esp_err_t e32p_mqtt_start(e32p_mqtt_handle_t h);
esp_err_t e32p_mqtt_stop(e32p_mqtt_handle_t h);
esp_err_t e32p_mqtt_deinit(e32p_mqtt_handle_t h);

/* operations */
esp_err_t e32p_mqtt_publish(e32p_mqtt_handle_t h, const char *topic, const void *data,
                            int len, int qos, bool retain, int *out_msg_id);
esp_err_t e32p_mqtt_subscribe(e32p_mqtt_handle_t h, const char *topic, int qos, int *out_msg_id);
esp_err_t e32p_mqtt_unsubscribe(e32p_mqtt_handle_t h, const char *topic, int *out_msg_id);
bool      e32p_mqtt_is_connected(e32p_mqtt_handle_t h);
```

## Usage

```c
static void on_evt(const e32p_mqtt_event_t *ev, void *user)
{
    if (ev->type == E32P_MQTT_EVENT_CONNECTED) {
        e32p_mqtt_subscribe(user, "e32p/demo/cmd", 1, NULL);
    } else if (ev->type == E32P_MQTT_EVENT_DATA) {
        printf("%.*s -> %.*s\n", ev->topic_len, ev->topic, ev->data_len, ev->data);
    }
}

/* after Wi-Fi is up and has an IP: */
e32p_mqtt_config_t cfg = { .broker_uri = "mqtt://test.mosquitto.org:1883", .cb = on_evt };
e32p_mqtt_handle_t mqtt;
e32p_mqtt_init(&cfg, &mqtt);
cfg.user = mqtt;                 /* so the callback can subscribe */
e32p_mqtt_start(mqtt);
/* ... */
e32p_mqtt_publish(mqtt, "e32p/demo/status", "online", 0, 1, false, NULL);
```

> The event callback runs in the esp-mqtt task. Keep it short and don't block; do
> heavy work on your own task. Event `topic`/`data` pointers are valid only for
> the duration of the callback.

## Topic matching

`e32p_mqtt_topic_matches(filter, topic)` follows MQTT 3.1.1 §4.7:

| Filter | Topic | Match |
|--------|-------|-------|
| `sport/#` | `sport/tennis/player1` | ✅ |
| `sport/#` | `sport` | ✅ (`#` matches the parent) |
| `sport/+` | `sport/tennis` | ✅ |
| `sport/+` | `sport/tennis/player1` | ❌ (`+` is one level) |
| `+/+` | `/finance` | ✅ (empty levels count) |
| `#` | `$SYS/broker/x` | ❌ (wildcard can't start a `$` topic) |

## Cherry-pick

```
CONFIG_E32P_MQTT_ENABLE=y
```

Requires `e32p_core`; privately requires `mqtt` (esp-mqtt) and `esp_event`. The
application supplies connectivity — e.g. enable `e32p_wifi` and get an IP before
`e32p_mqtt_start()`.
