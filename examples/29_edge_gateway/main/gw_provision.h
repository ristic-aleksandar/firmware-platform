#pragma once
/**
 * @file gw_provision.h
 * @brief First-boot BLE provisioning: advertise a GATT service, accept the
 *        network config from a phone, save it to NVS, and reboot into normal
 *        (Wi-Fi + MQTT) operation. Keeps Wi-Fi credentials out of the firmware
 *        image — they arrive over the air at commissioning time.
 */
#include "e32p_core.h"
#include "gw_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run provisioning. Advertises "e32p-gateway", stages fields written over
 *        GATT on top of @p current (so id/interval defaults survive), and on the
 *        "apply" write saves to NVS and reboots. Does not return.
 * @param[in] current  The current (mostly-empty) config, used as the staging base.
 */
void gw_provision_run(const gw_config_t *current);

#ifdef __cplusplus
}
#endif
