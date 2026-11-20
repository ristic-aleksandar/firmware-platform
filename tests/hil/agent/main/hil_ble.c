/**
 * @file hil_ble.c
 * @brief BLE HIL self-test. No radio: runs the pure advertising-payload assembly
 *        and UUID helpers on-target (the agent enables e32p_ble but not the
 *        Bluetooth host). The connectable peripheral is the 27_ble demo.
 */
#include <string.h>
#include <stdio.h>
#include "e32p_ble.h"
#include "hil_agent.h"
#include "hil_ble.h"

hil_verdict_t hil_ble_selftest(char *d, size_t n)
{
    e32p_ble_uuid_t u = e32p_ble_uuid16(0x180F);
    if (u.len != 2 || u.bytes[0] != 0x0F || u.bytes[1] != 0x18) {
        snprintf(d, n, "uuid16 wrong");
        return HIL_FAIL;
    }

    const uint8_t expect[] = {
        0x02, 0x01, 0x06, 0x03, 0x03, 0xFF, 0x00, 0x05, 0x09, 'e', '3', '2', 'p',
    };
    uint8_t buf[31];
    e32p_ble_uuid_t svc = e32p_ble_uuid16(0x00FF);
    int len = e32p_ble_adv_build(buf, sizeof(buf), E32P_BLE_ADV_FLAGS_DEFAULT, "e32p", &svc);
    if (len != (int)sizeof(expect) || memcmp(buf, expect, (size_t)len) != 0) {
        snprintf(d, n, "adv payload wrong (len=%d)", len);
        return HIL_FAIL;
    }

    if (e32p_ble_adv_build(buf, sizeof(buf), 0x06, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123", NULL) != -1) {
        snprintf(d, n, "oversized payload not rejected");
        return HIL_FAIL;
    }

    snprintf(d, n, "uuid + adv payload (%d bytes) ok", len);
    return HIL_PASS;
}
