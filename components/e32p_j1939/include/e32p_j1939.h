#pragma once
/**
 * @file e32p_j1939.h
 * @brief SAE J1939 protocol/data layer over e32p_twai (CAN 2.0B, 29-bit IDs).
 *
 * This layer is the 29-bit CAN ID <-> J1939 field codec plus single-frame
 * (<=8 byte) send/receive. The CAN bus configuration (pins, bitrate, mode)
 * stays in e32p_twai: the application inits and starts TWAI, then inits this
 * node with its source address. The ID codec is pure and hardware-independent.
 *
 * 29-bit ID layout: Priority(3) EDP(1) DP(1) PDU-Format(8) PDU-Specific(8)
 * Source-Address(8). The PGN (Parameter Group Number, 18 bits) and the
 * destination address depend on the PDU format:
 *   - PDU1 (PF <  240): destination-specific. PS = destination address,
 *     which is NOT part of the PGN.
 *   - PDU2 (PF >= 240): broadcast. PS = group extension, which IS part of
 *     the PGN; the destination is implicitly global (0xFF).
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Well-known addresses (J1939-81). */
#define E32P_J1939_ADDR_GLOBAL       0xFFu  /*!< Broadcast destination.        */
#define E32P_J1939_ADDR_NULL         0xFEu  /*!< Null / cannot-claim address.  */
#define E32P_J1939_PRIORITY_DEFAULT  6u     /*!< Typical default TX priority.  */

/* Well-known PGNs (used by the TP and ACL layers). */
#define E32P_J1939_PGN_TP_CM         60416u /*!< 0xEC00 transport conn. mgmt.  */
#define E32P_J1939_PGN_TP_DT         60160u /*!< 0xEB00 transport data.        */
#define E32P_J1939_PGN_REQUEST       59904u /*!< 0xEA00 request PGN.           */
#define E32P_J1939_PGN_ADDR_CLAIMED  60928u /*!< 0xEE00 address claimed.       */

/** @brief A single-frame (<=8 byte) J1939 message, host-side view. */
typedef struct {
    uint8_t  priority;   /*!< 0..7. On send, 0xFF => use configured default.    */
    uint32_t pgn;        /*!< 18-bit Parameter Group Number.                    */
    uint8_t  src_addr;   /*!< Source address (filled on receive).               */
    uint8_t  dst_addr;   /*!< Destination; only meaningful for PDU1. 0xFF=global.*/
    uint8_t  len;        /*!< Data length, 0..8.                                */
    uint8_t  data[8];
} e32p_j1939_message_t;

/** @brief Node configuration. */
typedef struct {
    uint8_t src_addr;          /*!< This node's source address (0..253).        */
    uint8_t default_priority;  /*!< Default TX priority (typically 6).          */
    bool    self_reception;    /*!< Request self-reception of sent frames. Set
                                    true only for single-board loopback with
                                    TWAI in NO_ACK self-test mode; false on a
                                    real bus (a node must not receive its own). */
} e32p_j1939_config_t;

/* --- Pure ID codec (no hardware; the heart of the unit tests) ------------- */

/**
 * @brief Compose a 29-bit CAN ID from J1939 fields.
 * @param[in] priority 0..7 (masked to 3 bits).
 * @param[in] pgn      18-bit PGN.
 * @param[in] src      Source address.
 * @param[in] dst      Destination address; used only for PDU1 (PF < 240),
 *                     ignored for PDU2 where PS comes from the PGN.
 * @return 29-bit identifier.
 */
uint32_t e32p_j1939_id_encode(uint8_t priority, uint32_t pgn,
                              uint8_t src, uint8_t dst);

/**
 * @brief Decode a 29-bit CAN ID into J1939 fields. Any out-pointer may be NULL.
 *        For PDU2 (broadcast) @p dst is set to E32P_J1939_ADDR_GLOBAL.
 */
void e32p_j1939_id_decode(uint32_t id, uint8_t *priority, uint32_t *pgn,
                          uint8_t *src, uint8_t *dst);

/** @brief True if @p pgn is PDU1 (destination-specific); false if PDU2 (broadcast). */
bool e32p_j1939_pgn_is_pdu1(uint32_t pgn);

/* --- Node lifecycle + single-frame transfer ------------------------------- */

/**
 * @brief Bind this J1939 node to a source address. Requires e32p_twai to be
 *        already initialized and started.
 * @return ESP_OK, or ESP_ERR_INVALID_ARG for a NULL cfg or an out-of-range SA.
 */
esp_err_t e32p_j1939_init(const e32p_j1939_config_t *cfg);
esp_err_t e32p_j1939_deinit(void);

/**
 * @brief Send a single J1939 frame. The source address is the configured node
 *        SA; the extended ID is composed from priority (default if 0xFF), pgn,
 *        and dst. @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE
 *        (not initialized), or a driver error.
 */
esp_err_t e32p_j1939_send(const e32p_j1939_message_t *msg, uint32_t timeout_ms);

/**
 * @brief Receive and decode the next J1939 frame.
 * @return ESP_OK, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_STATE, or a driver error.
 */
esp_err_t e32p_j1939_receive(e32p_j1939_message_t *msg, uint32_t timeout_ms);

/**
 * @brief Convenience: broadcast (or address to @p dst) a Request for @p
 *        requested_pgn (PGN 59904, 3-byte payload). @return as e32p_j1939_send.
 */
esp_err_t e32p_j1939_request(uint32_t requested_pgn, uint8_t dst, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
