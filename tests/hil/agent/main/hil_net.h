#pragma once
/**
 * @file hil_net.h
 * @brief Network HIL commands driven by the host harness: join Wi-Fi, run an
 *        MQTT broker round-trip, and pull an OTA image from the host's HTTP
 *        server. These need a live AP + the host acting as broker / file server,
 *        so the host gates them behind --wifi-ssid (see test_net_mqtt/test_net_ota).
 *
 * Each function emits its own `@RSP <VERB> …` line via hil_emit().
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief `@CMD WIFI <ssid> [pass]` — connect as a station; reply the IP. */
void hil_net_wifi(const char *ssid, const char *pass);

/** @brief `@CMD MQTT <broker_ip>` — connect, subscribe hil/mqtt/cmd, publish
 *         hil/mqtt/echo, and report a message received back from the host. */
void hil_net_mqtt(const char *broker_ip);

/** @brief `@CMD OTA <url>` — pull firmware from @p url; on success reboots into
 *         the new slot (the host waits for @EVT READY and re-checks the slot). */
void hil_net_ota(const char *url);

/** @brief `@CMD OTASLOT` — report the running OTA partition label. */
void hil_net_running_slot(void);

#ifdef __cplusplus
}
#endif
