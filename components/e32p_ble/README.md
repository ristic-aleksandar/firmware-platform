# e32p_ble — BLE GATT peripheral (NimBLE)

Advertise a named BLE device with one custom service and a handful of
characteristics (read / write / notify), on top of Apache **NimBLE**. Connect to
it from a phone (nRF Connect / LightBlue) or another BLE central.

## What it does

- **Advertises** a device name + your service UUID (16- or 128-bit).
- **Serves a GATT service** with characteristics you declare — each read, write,
  and/or notify.
- **Write callback** fires when a central writes; **notify** pushes a value to a
  subscribed central; **connect/disconnect** callback tracks the link.
- Re-advertises automatically after a disconnect.

## Design

Split so the tricky logic is testable without a radio:

- **Pure** — `adv_build` (assembles the advertising AD-structure payload) and the
  UUID helpers. Always compiled, unit-tested byte-for-byte. The real advertising
  path uses `adv_build`, so the tested code is the shipped code.
- **Radio (NimBLE)** — `init` / `notify` / … compiled **only when
  `CONFIG_BT_NIMBLE_ENABLED`** is set. Without the BLE host the device functions
  return `ESP_ERR_NOT_SUPPORTED`, so the module still builds on radio-free targets
  (and stays out of `unit_runner`/the HIL agent, which don't enable Bluetooth).

Flat/singleton — one BLE controller per chip, so there's no handle.

## Enabling the radio

BLE needs the Bluetooth host built in. In `menuconfig`:
*Component config → Bluetooth → **[*] Bluetooth***, then
*Host → **NimBLE - BLE only***. In `sdkconfig.defaults`:

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_E32P_BLE_ENABLE=y
```

## API

```c
/* pure */
e32p_ble_uuid_t e32p_ble_uuid16(uint16_t uuid);
e32p_ble_uuid_t e32p_ble_uuid128(const uint8_t bytes[16]);
int             e32p_ble_adv_build(uint8_t *dst, size_t cap, uint8_t flags,
                                   const char *name, const e32p_ble_uuid_t *svc);

/* peripheral */
esp_err_t e32p_ble_init(const e32p_ble_config_t *cfg);
esp_err_t e32p_ble_deinit(void);
esp_err_t e32p_ble_start_adv(void);
esp_err_t e32p_ble_stop_adv(void);
esp_err_t e32p_ble_set_value(uint16_t idx, const void *data, uint16_t len);
esp_err_t e32p_ble_notify(uint16_t idx, const void *data, uint16_t len);
bool      e32p_ble_is_connected(void);
```

## Usage

```c
static void on_write(uint16_t idx, const uint8_t *data, uint16_t len, void *user)
{
    printf("central wrote %.*s to char %u\n", len, data, idx);
}

enum { CH_STATUS, CH_CMD };
/* Local, not static: the uuid helpers are function calls (init() reads it here). */
const e32p_ble_char_t chars[] = {
    { .uuid = e32p_ble_uuid16(0x2A56), .props = E32P_BLE_PROP_READ | E32P_BLE_PROP_NOTIFY },
    { .uuid = e32p_ble_uuid16(0x2A57), .props = E32P_BLE_PROP_WRITE },
};
e32p_ble_config_t cfg = {
    .device_name = "e32p-node", .service_uuid = e32p_ble_uuid16(0x00FF),
    .chars = chars, .n_chars = 2, .on_write = on_write,
};
e32p_ble_init(&cfg);                      /* advertises automatically */

/* later, push a reading to a subscribed central: */
uint8_t v = 42;
e32p_ble_notify(CH_STATUS, &v, 1);
```

> Callbacks run in the NimBLE host task — keep them short and non-blocking.
> Advertising names are limited by the 31-byte legacy packet; a long name + a
> 128-bit service UUID may not fit (`adv_build` returns -1 / `init` returns
> `ESP_ERR_INVALID_ARG`).

## Cherry-pick

```
CONFIG_E32P_BLE_ENABLE=y
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
```

Requires `e32p_core`; privately requires `bt` (NimBLE) when the host is enabled.
