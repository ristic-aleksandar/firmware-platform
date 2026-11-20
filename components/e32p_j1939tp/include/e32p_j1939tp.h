#pragma once
/**
 * @file e32p_j1939tp.h
 * @brief SAE J1939-21 Transport Protocol over e32p_j1939: segments and
 *        reassembles 9..1785 byte messages via BAM (broadcast) and RTS/CTS
 *        (connection-mode), transmit and receive.
 *
 * The engine is decoupled from the bus and the clock so it is fully testable:
 *   - Outgoing frames go through an injected @ref e32p_j1939tp_tx_fn sink (the
 *     application wires it to e32p_j1939_send; a test can capture them or feed
 *     them into another engine for an in-process loopback).
 *   - Feed every received TP.CM (PGN 60416) / TP.DT (PGN 60160) frame to
 *     @ref e32p_j1939tp_on_frame.
 *   - Call @ref e32p_j1939tp_poll periodically with a millisecond clock to
 *     service timers and pace outgoing packets.
 *
 * A handle is driven from a single task (the application serialises send /
 * on_frame / poll in one loop); it is not ISR- or multi-task-safe.
 */
#include <stdint.h>
#include "e32p_core.h"
#include "e32p_j1939.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque transport engine handle. */
typedef struct e32p_j1939tp_ctx *e32p_j1939tp_handle_t;

/** @brief Sink the engine calls to put one J1939 frame on the wire. */
typedef esp_err_t (*e32p_j1939tp_tx_fn)(const e32p_j1939_message_t *frame, void *user);

/** @brief Delivered when a complete multi-packet message has been reassembled.
 *         @p data is valid only for the duration of the call. */
typedef void (*e32p_j1939tp_rx_fn)(uint32_t pgn, uint8_t src, uint8_t dst,
                                   const uint8_t *data, uint16_t len, void *user);

/** @brief A TX session finished: ESP_OK on success, ESP_ERR_TIMEOUT on a timer
 *         expiry, or ESP_FAIL on a received Conn_Abort. */
typedef void (*e32p_j1939tp_txdone_fn)(uint32_t pgn, uint8_t dst,
                                       esp_err_t status, void *user);

/** @brief Engine configuration. */
typedef struct {
    uint8_t                src_addr;    /*!< This node's source address.        */
    e32p_j1939tp_tx_fn     tx;          /*!< Required: outgoing-frame sink.      */
    e32p_j1939tp_rx_fn     on_rx;       /*!< Required: completed-message cb.     */
    e32p_j1939tp_txdone_fn on_tx_done;  /*!< Optional (NULL allowed).            */
    void                  *user;        /*!< Passed back to all callbacks.       */
} e32p_j1939tp_config_t;

/**
 * @brief Create a transport engine.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (NULL cfg/tx/on_rx or bad SA),
 *         or ESP_ERR_NO_MEM.
 */
esp_err_t e32p_j1939tp_init(const e32p_j1939tp_config_t *cfg, e32p_j1939tp_handle_t *out);
esp_err_t e32p_j1939tp_deinit(e32p_j1939tp_handle_t h);

/**
 * @brief Begin sending a 9..1785 byte message. @p dst == 0xFF (global) uses
 *        BAM; any other destination uses connection-mode RTS/CTS. The payload
 *        is copied. Non-blocking: progress is driven by poll()/on_frame() and
 *        the result is reported through on_tx_done.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (len<9 or >max, NULL data),
 *         or ESP_ERR_INVALID_STATE (a TX session is already active).
 */
esp_err_t e32p_j1939tp_send(e32p_j1939tp_handle_t h, uint32_t pgn, uint8_t priority,
                            uint8_t dst, const uint8_t *data, uint16_t len);

/**
 * @brief Feed a received frame. TP.CM (60416) and TP.DT (60160) frames drive
 *        the state machines; all other PGNs are ignored. @return ESP_OK, or
 *        ESP_ERR_INVALID_ARG for a NULL argument.
 *
 * @note Session timers are stamped with the most recent @ref e32p_j1939tp_poll
 *       timestamp. Call poll() regularly and in the same loop as on_frame() so
 *       that time is current when a frame arrives; a long unpolled gap followed
 *       by a frame can otherwise make a freshly-opened session appear to have
 *       already timed out.
 */
esp_err_t e32p_j1939tp_on_frame(e32p_j1939tp_handle_t h, const e32p_j1939_message_t *frame);

/**
 * @brief Service timers and pace outgoing TP.DT packets. Call periodically
 *        (e.g. every 10..50 ms) with a monotonic millisecond timestamp.
 */
void e32p_j1939tp_poll(e32p_j1939tp_handle_t h, uint32_t now_ms);

/** @brief True if a TX session is currently in progress. */
bool e32p_j1939tp_tx_busy(e32p_j1939tp_handle_t h);

#ifdef __cplusplus
}
#endif
