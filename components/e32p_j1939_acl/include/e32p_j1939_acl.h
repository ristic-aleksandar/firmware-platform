#pragma once
/**
 * @file e32p_j1939_acl.h
 * @brief SAE J1939-81 Address Claiming over e32p_j1939.
 *
 * A node claims a source address on the bus and defends it by NAME arbitration
 * (numerically lower NAME wins the same address). It answers Requests for
 * "Address Claimed", and on losing either relocates (if arbitrary-address-
 * capable) or falls back to the null address and reports "Cannot Claim". The
 * claimed address is what e32p_j1939 then uses as the node's source address.
 *
 * Like e32p_j1939tp, the engine is decoupled from the bus and the clock:
 *   - outgoing frames leave through an injected @ref e32p_j1939_acl_tx_fn sink;
 *   - feed received Address-Claimed (PGN 60928) / Request (PGN 59904) frames to
 *     @ref e32p_j1939_acl_on_frame;
 *   - call @ref e32p_j1939_acl_poll periodically with a millisecond clock to run
 *     the 250 ms contention timer.
 * This makes it fully unit-testable headless (two engines contend over an
 * in-process bus).
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"
#include "e32p_j1939.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The eight fields of a 64-bit J1939 NAME (J1939-81). */
typedef struct {
    bool     arbitrary_address_capable; /*!< may relocate on a lost claim */
    uint8_t  industry_group;            /*!< 0..7   */
    uint8_t  vehicle_system_instance;   /*!< 0..15  */
    uint8_t  vehicle_system;            /*!< 0..127 */
    uint8_t  function;                  /*!< 0..255 */
    uint8_t  function_instance;         /*!< 0..31  */
    uint8_t  ecu_instance;              /*!< 0..7   */
    uint16_t manufacturer_code;         /*!< 0..2047 (11 bits) */
    uint32_t identity_number;           /*!< 0..2097151 (21 bits) */
} e32p_j1939_name_t;

/** @brief Pack the fields into the 64-bit NAME (also the arbitration key). */
uint64_t e32p_j1939_name_encode(const e32p_j1939_name_t *name);
/** @brief Unpack a 64-bit NAME into its fields. */
void     e32p_j1939_name_decode(uint64_t value, e32p_j1939_name_t *out);

/** @brief Opaque address-claiming engine handle. */
typedef struct e32p_j1939_acl_ctx *e32p_j1939_acl_handle_t;

/** @brief Sink the engine calls to put one J1939 frame on the wire. The frame's
 *         @c src_addr is set explicitly (it changes across a claim), so the sink
 *         must honour it rather than substitute a fixed node address. */
typedef esp_err_t (*e32p_j1939_acl_tx_fn)(const e32p_j1939_message_t *frame, void *user);

/** @brief Notified when the claim result changes: @p claimed true with the won
 *         address, or false when the node cannot claim (@p sa == 0xFE null). */
typedef void (*e32p_j1939_acl_evt_fn)(uint8_t sa, bool claimed, void *user);

/** @brief Engine configuration. */
typedef struct {
    uint64_t             name;         /*!< this node's 64-bit NAME */
    uint8_t              preferred_sa; /*!< address to claim first (0..253) */
    e32p_j1939_acl_tx_fn tx;           /*!< required frame sink */
    e32p_j1939_acl_evt_fn on_event;    /*!< optional result callback */
    void                *user;         /*!< passed back to callbacks */
} e32p_j1939_acl_config_t;

esp_err_t e32p_j1939_acl_init(const e32p_j1939_acl_config_t *cfg, e32p_j1939_acl_handle_t *out);
esp_err_t e32p_j1939_acl_deinit(e32p_j1939_acl_handle_t h);

/** @brief Begin claiming: sends Address Claimed for @c preferred_sa. */
esp_err_t e32p_j1939_acl_start(e32p_j1939_acl_handle_t h);

/** @brief Feed a received frame. Address-Claimed (60928) and Request (59904)
 *         drive the engine; other PGNs are ignored. */
esp_err_t e32p_j1939_acl_on_frame(e32p_j1939_acl_handle_t h, const e32p_j1939_message_t *frame);

/** @brief Run the contention timer. Call periodically with a monotonic ms clock. */
void e32p_j1939_acl_poll(e32p_j1939_acl_handle_t h, uint32_t now_ms);

/** @brief The address currently in use: 0..253 while claiming/claimed, or 0xFE
 *         (null) when the node cannot claim. */
uint8_t e32p_j1939_acl_current_sa(e32p_j1939_acl_handle_t h);
/** @brief True once the address is claimed (contention window elapsed). */
bool    e32p_j1939_acl_is_claimed(e32p_j1939_acl_handle_t h);

#ifdef __cplusplus
}
#endif
