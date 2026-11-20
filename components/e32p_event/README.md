# e32p_event — default event loop facade

Singleton wrapper over the ESP-IDF default event loop. `init` is idempotent and
adopts an existing loop (Wi-Fi creates one), so any module can call it safely.

## Enable

```
CONFIG_E32P_EVENT_ENABLE=y
```
Then `REQUIRES e32p_event` in your component.

## API

| Function | Purpose |
|---|---|
| `e32p_event_init` / `e32p_event_deinit` | Create / delete the default loop (idempotent init). |
| `e32p_event_is_running` | Query loop state. |
| `e32p_event_handler_register` / `_unregister` | Attach / detach a handler. |
| `e32p_event_post` | Post an event with an optional payload. |

## Example

```c
ESP_EVENT_DEFINE_BASE(APP_EVENTS);
enum { APP_EVENT_READY };

static void on_ready(void *arg, esp_event_base_t base, int32_t id, void *data) { /* ... */ }

ESP_ERROR_CHECK(e32p_event_init());
ESP_ERROR_CHECK(e32p_event_handler_register(APP_EVENTS, APP_EVENT_READY, on_ready, NULL));
ESP_ERROR_CHECK(e32p_event_post(APP_EVENTS, APP_EVENT_READY, NULL, 0, portMAX_DELAY));
```

## Notes

- Handlers run on the **event task**, not the poster's task — keep them short and
  hand heavy work to your own task.
- This wraps only the **default** loop; custom loops are out of scope for the
  thin facade. Wi-Fi/IP events dispatch here once `e32p_event_init` has run.
