#pragma once
/**
 * @file e32p_isotp.h
 * @brief ISO 15765-2 (ISO-TP) transport over classic CAN: segments and
 *        reassembles 1..4095 byte payloads using SF / FF / CF / FC.
 *
 * This is the **engine** — pure, and the whole protocol lives here. It makes no
 * TWAI call, starts no task, and reads no clock:
 *   - outgoing frames leave through an injected @ref e32p_isotp_tx_fn sink,
 *   - received frames are fed in with @ref e32p_isotp_on_frame,
 *   - time arrives via @ref e32p_isotp_poll, which services timers and paces
 *     consecutive frames.
 * That makes it fully unit-testable headless: two engines wired through an
 * in-process queue with a virtual clock, no bus. The binding to a real
 * controller is a separate thin layer (`e32p_isotp_twai.h`).
 *
 * **This is NOT SAE J1939-21 TP.** Do not confuse it with `e32p_j1939tp`:
 * different segmentation scheme, different addressing, different consumers.
 * ISO-TP is what OBD-II and UDS run on.
 *
 * A handle is driven from a single task (the caller serialises send / on_frame /
 * poll in one loop); it is not ISR- or multi-task-safe.
 *
 * **Concurrency:** one TX session and one RX session may run at the same time on
 * one channel. They are independent state machines on different CAN IDs, and
 * their flow-control frames travel in opposite directions, so they never
 * contend. Two concurrent TX sessions are not supported (@ref e32p_isotp_send
 * returns `ESP_ERR_INVALID_STATE`); a second FF during an active RX restarts the
 * reassembly.
 *
 * Out of scope in v1: extended and mixed addressing, CAN-FD.
 */
#include <stdint.h>
#include <stdbool.h>
#include "e32p_core.h"
#include "e32p_twai.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Kconfig-selected defaults ------------------------------------------- */
/* Fall back when a consumer's sdkconfig predates these symbols. */
#ifndef CONFIG_E32P_ISOTP_BITRATE_BPS
#  define CONFIG_E32P_ISOTP_BITRATE_BPS 250000
#endif
#ifndef CONFIG_E32P_ISOTP_MAX_MSG_BYTES
#  define CONFIG_E32P_ISOTP_MAX_MSG_BYTES 1024
#endif

/** @brief Bitrate an ISO-TP app should pass to e32p_twai_init(). A CAN bus is a
 *         single-bitrate domain: every node must match, sniffers included. */
#define E32P_ISOTP_DEFAULT_BITRATE  CONFIG_E32P_ISOTP_BITRATE_BPS

/** @brief Largest payload ISO 15765-2 can carry without the 32-bit escape. */
#define E32P_ISOTP_MAX_PAYLOAD      4095u

/** @brief Addressing mode. Extended and mixed addressing are out of scope (v1). */
typedef enum {
    E32P_ISOTP_ADDR_NORMAL_11,    /*!< Normal, 11-bit: raw tx_id / rx_id.        */
    E32P_ISOTP_ADDR_NORMAL_FIXED, /*!< Normal fixed, 29-bit: 0x18DA / 0x18DB.    */
} e32p_isotp_addr_mode_t;

#if defined(CONFIG_E32P_ISOTP_ADDRESSING_NORMAL_FIXED)
#  define E32P_ISOTP_DEFAULT_ADDR_MODE E32P_ISOTP_ADDR_NORMAL_FIXED
#else
#  define E32P_ISOTP_DEFAULT_ADDR_MODE E32P_ISOTP_ADDR_NORMAL_11
#endif

/** @brief FlowStatus values (ISO 15765-2). */
typedef enum {
    E32P_ISOTP_FS_CTS   = 0, /*!< Clear to send.                                */
    E32P_ISOTP_FS_WAIT  = 1, /*!< Wait; sender restarts N_Bs.                   */
    E32P_ISOTP_FS_OVFLW = 2, /*!< Receiver cannot hold the announced length.    */
} e32p_isotp_fs_t;

/** @brief Protocol Control Information frame types (high nibble of byte 0). */
typedef enum {
    E32P_ISOTP_PCI_SF  =  0, /*!< Single frame.        */
    E32P_ISOTP_PCI_FF  =  1, /*!< First frame.         */
    E32P_ISOTP_PCI_CF  =  2, /*!< Consecutive frame.   */
    E32P_ISOTP_PCI_FC  =  3, /*!< Flow control.        */
    E32P_ISOTP_PCI_BAD = -1, /*!< Not a valid PCI.     */
} e32p_isotp_pci_t;

/** @brief Opaque per-address-pair transport channel. */
typedef struct e32p_isotp_ctx *e32p_isotp_handle_t;

/** @brief Sink the engine calls to put one CAN frame on the wire. */
typedef esp_err_t (*e32p_isotp_tx_fn)(const e32p_twai_message_t *frame, void *user);

/** @brief A complete payload was reassembled. @p data is valid only for the
 *         duration of the call. */
typedef void (*e32p_isotp_rx_fn)(const uint8_t *data, uint16_t len, void *user);

/** @brief A TX transfer ended. @p status is ESP_OK, ESP_ERR_TIMEOUT (N_Bs/N_As),
 *         ESP_ERR_NO_MEM (peer sent FC.OVFLW), or ESP_FAIL (protocol error). */
typedef void (*e32p_isotp_txdone_fn)(esp_err_t status, void *user);

/** @brief Channel configuration. Zeroed fields take the documented defaults. */
typedef struct {
    e32p_isotp_addr_mode_t mode;

    /* NORMAL_11 addressing. */
    uint32_t tx_id;         /*!< ID we transmit on (e.g. 0x7E8).                */
    uint32_t rx_id;         /*!< ID we accept      (e.g. 0x7E0).                */

    /* NORMAL_FIXED addressing — IDs are composed from these instead. */
    uint8_t  source_addr;   /*!< N_SA.                                          */
    uint8_t  target_addr;   /*!< N_TA.                                          */
    bool     functional;    /*!< true -> 0x18DB.. (1-to-N), false -> 0x18DA..   */

    /* Flow control this channel ISSUES as a receiver. */
    uint8_t  rx_block_size; /*!< BS in our FC. 0 = send all CFs, no further FC.  */
    uint8_t  rx_stmin_ms;   /*!< STmin we request, 0..127 ms.                   */
    uint8_t  rx_wft_max;    /*!< Max FC.WAIT frames we emit. 0 = never WAIT.    */

    /* Framing. */
    bool     pad_tx;        /*!< Pad every transmitted frame to 8 bytes.        */
    uint8_t  pad_byte;      /*!< Padding value. 0 -> 0xAA.                      */
    uint16_t max_msg_bytes; /*!< 0 -> CONFIG_E32P_ISOTP_MAX_MSG_BYTES.          */
    bool     self_reception;/*!< Set .self on transmitted frames (NO_ACK loop). */

    /* Timers, milliseconds. 0 -> the ISO default shown. */
    uint16_t n_bs_ms;       /*!< Sender waits for FC.        Default 1000.      */
    uint16_t n_cr_ms;       /*!< Receiver waits for next CF. Default 1000.      */
    uint16_t n_as_ms;       /*!< TX confirmation budget.     Default 1000.      */

    e32p_isotp_tx_fn     tx;         /*!< Required: outgoing-frame sink.        */
    e32p_isotp_rx_fn     on_rx;      /*!< Required: completed-payload callback. */
    e32p_isotp_txdone_fn on_tx_done; /*!< Optional (NULL allowed).              */
    void                *user;       /*!< Passed back to every callback.        */
} e32p_isotp_config_t;

/**
 * @brief Fill @p cfg with Kconfig-selected addressing and ISO defaults for one
 *        address pair. In NORMAL_11 mode @p req_id / @p resp_id are used and
 *        @p sa / @p ta ignored; in NORMAL_FIXED it is the reverse.
 *
 * Kconfig picks the defaults; it does not replace per-instance config — every
 * field stays overridable afterwards.
 *
 * @param[out] cfg      Destination; fully overwritten.
 * @param[in]  resp_id  ID this channel transmits on (NORMAL_11).
 * @param[in]  req_id   ID this channel accepts     (NORMAL_11).
 * @param[in]  sa       N_SA (NORMAL_FIXED).
 * @param[in]  ta       N_TA (NORMAL_FIXED).
 * @param[in]  functional  Functional (1-to-N) addressing (NORMAL_FIXED).
 */
void e32p_isotp_config_defaults(e32p_isotp_config_t *cfg,
                                uint32_t resp_id, uint32_t req_id,
                                uint8_t sa, uint8_t ta, bool functional);

/**
 * @brief Create a channel. Allocates two buffers of @c max_msg_bytes.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (NULL cfg/tx/on_rx/out, or a bad size),
 *         or ESP_ERR_NO_MEM.
 */
esp_err_t e32p_isotp_init(const e32p_isotp_config_t *cfg, e32p_isotp_handle_t *out);

/** @brief Destroy a channel and free its buffers. NULL is a no-op. */
esp_err_t e32p_isotp_deinit(e32p_isotp_handle_t h);

/**
 * @brief Begin sending 1..max_msg_bytes. The payload is copied. Non-blocking:
 *        progress comes from poll()/on_frame(), the result from on_tx_done.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (NULL data, len 0 or > max),
 *         or ESP_ERR_INVALID_STATE (a TX session is already active).
 */
esp_err_t e32p_isotp_send(e32p_isotp_handle_t h, const uint8_t *data, uint16_t len);

/**
 * @brief Feed one received CAN frame. Frames whose ID is not this channel's rx
 *        ID are ignored (ESP_OK, no effect).
 * @return ESP_OK, or ESP_ERR_INVALID_ARG for a NULL argument.
 *
 * @note Session timers are stamped from the most recent @ref e32p_isotp_poll
 *       timestamp. Call poll() regularly and in the same loop as on_frame(), so
 *       time is current when a frame arrives.
 */
esp_err_t e32p_isotp_on_frame(e32p_isotp_handle_t h, const e32p_twai_message_t *frame);

/**
 * @brief Service timers and pace outgoing consecutive frames. Call with a
 *        monotonic millisecond timestamp.
 *
 * Emits **as many CFs as the peer's BlockSize, its STmin and the sink currently
 * allow** — never one per invocation. A sink returning non-OK is treated as
 * back-pressure and the remainder resumes on the next call. STmin=0 throughput
 * is therefore bound by the sink, not by how often poll() runs.
 *
 * Sub-millisecond STmin (raw 0xF1..0xF9) is rounded **up** to 1 ms: sending
 * slower than the peer asked is always spec-legal.
 */
void e32p_isotp_poll(e32p_isotp_handle_t h, uint32_t now_ms);

/** @brief True while a TX session is in progress. */
bool e32p_isotp_tx_busy(e32p_isotp_handle_t h);
/** @brief True while an inbound payload is being reassembled. */
bool e32p_isotp_rx_busy(e32p_isotp_handle_t h);
/** @brief The CAN ID this channel accepts — used by the link layer to route. */
uint32_t e32p_isotp_rx_id(e32p_isotp_handle_t h);
/** @brief True if this channel's IDs are 29-bit. */
bool e32p_isotp_is_extended(e32p_isotp_handle_t h);

/* --- Pure PCI helpers: exposed for tests and for tooling ------------------ */

/** @brief Classify a frame's PCI. @p dlc is the payload length 0..8. */
e32p_isotp_pci_t e32p_isotp_pci_type(const uint8_t *d, uint8_t dlc);
/** @brief SingleFrame data length. @return ESP_OK or ESP_ERR_INVALID_ARG. */
esp_err_t e32p_isotp_pci_sf_len(const uint8_t *d, uint8_t dlc, uint8_t *out_len);
/** @brief FirstFrame 12-bit length. FF_DL 0 (32-bit escape) is rejected. */
esp_err_t e32p_isotp_pci_ff_len(const uint8_t *d, uint8_t dlc, uint16_t *out_len);
/** @brief ConsecutiveFrame sequence number 0..15. */
esp_err_t e32p_isotp_pci_cf_sn(const uint8_t *d, uint8_t dlc, uint8_t *out_sn);
/** @brief FlowControl fields. @p stmin_raw is the wire byte, not milliseconds. */
esp_err_t e32p_isotp_pci_fc(const uint8_t *d, uint8_t dlc, e32p_isotp_fs_t *fs,
                            uint8_t *bs, uint8_t *stmin_raw);

/**
 * @brief Decode a raw STmin byte to microseconds.
 *        0x00..0x7F -> 0..127 ms; 0xF1..0xF9 -> 100..900 us; all reserved
 *        values -> 127 ms, as ISO 15765-2 requires.
 */
uint32_t e32p_isotp_stmin_us(uint8_t raw);

/**
 * @brief Compose a normal-fixed 29-bit ID: 0x18DA{TA}{SA} physical,
 *        0x18DB{TA}{SA} functional. PF 0xDA/0xDB is PGN 55808/56064, which
 *        SAE J1939-73 reserves for exactly this — so the two stacks coexist.
 */
uint32_t e32p_isotp_id_normal_fixed(uint8_t sa, uint8_t ta, bool functional);

#ifdef __cplusplus
}
#endif
