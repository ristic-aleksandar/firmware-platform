#pragma once
/**
 * @file hil_mqtt.h
 * @brief MQTT HIL self-test (single board, no network): verifies topic-filter
 *        matching on-target and that the esp-mqtt client links/allocates. A real
 *        broker round-trip is the 23_mqtt demo (needs Wi-Fi + a broker).
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_mqtt_selftest(char *detail, size_t n);

#ifdef __cplusplus
}
#endif
