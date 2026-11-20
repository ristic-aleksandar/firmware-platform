# e32p_espnow — peer-to-peer messaging facade

Thin facade over ESP-NOW: connectionless peer-to-peer messaging over the Wi-Fi
radio — no router, no IP stack. The radio is brought up internally (STA mode,
unconnected).

## Enable

```
CONFIG_E32P_ESPNOW_ENABLE=y   # selects e32p_wifi (-> nvs, event) automatically
```
Then `REQUIRES e32p_espnow`.

## API

| Function | Purpose |
|---|---|
| `e32p_espnow_init` / `_deinit` | Start / stop ESP-NOW (registers a receive callback). |
| `e32p_espnow_add_peer` / `_remove_peer` | Register / drop a unicast peer. |
| `e32p_espnow_send` | Unicast to a registered peer (≤ 250 bytes). |
| `e32p_espnow_broadcast` | Send to everyone (auto-registers the broadcast peer). |
| `e32p_espnow_get_mac` | This device's STA MAC (the address peers target). |

## Example

```c
static void on_rx(const uint8_t mac[6], const uint8_t *data, int len) { /* ... */ }

ESP_ERROR_CHECK(e32p_espnow_init(on_rx));
e32p_espnow_broadcast("hello", 5);           /* to all listeners */

uint8_t peer[6] = { 0x0c,0xb8,0x15,0x11,0x22,0x33 };
e32p_espnow_add_peer(peer);
e32p_espnow_send(peer, "hi", 2);             /* to one peer */
```

## Notes / gotchas

| Item | Detail |
|---|---|
| No self-receive | A board does **not** receive its own broadcasts — you need a second board to see RX. |
| Callback context | `on_recv` runs on the Wi-Fi task. Keep it short; copy data out, don't block. |
| Payload limit | 250 bytes per message (`ESP_NOW_MAX_DATA_LEN`). |
| Same channel | Peers must be on the same Wi-Fi channel. With no AP, all boots default to channel 1. |
| Coexistence | Works alongside a Wi-Fi STA connection, but both must share the AP's channel. |
