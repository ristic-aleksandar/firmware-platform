/**
 * @file e32p_uds.h
 * @brief ISO 14229-1 UDS server — pure, with time injected through poll().
 *
 * Requests arrive through e32p_uds_request(), time through e32p_uds_poll(), and
 * every message the server wants on the bus leaves through the injected
 * @ref e32p_uds_config_t::send callback. The server owns no transport, no task and
 * no clock, so every rule — the S3 session timeout, the security delay, the 0x78
 * responsePending cadence — is testable on the Unity runner with a virtual clock.
 *
 * A request can be answered later. A callback that cannot finish inside P2
 * returns @ref E32P_UDS_NRC_PENDING; the server then sends `7F <sid> 78` on time
 * and keeps repeating it from poll() until the application finishes the request
 * with e32p_uds_complete().
 *
 * Every service is optional: a NULL callback (or an empty table) answers it as
 * not supported. That is also how the component grows slice by slice.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Largest TransferData request accepted: SID, block counter and 1024 data
 *        bytes. Reported to the tester in the 0x74 response.
 */
#define E32P_UDS_MAX_BLOCK_LEN 1026u

/** @brief ISO 14229-2 default timing, used when the config leaves a field at 0. */
#define E32P_UDS_P2_DEFAULT_MS      50u
#define E32P_UDS_P2_STAR_DEFAULT_MS 5000u
#define E32P_UDS_S3_DEFAULT_MS      5000u

/**
 * @brief Longest a request may stay pending before the server gives up and answers
 *        0x10 generalReject, when the config leaves the field at 0. Far above any
 *        legitimate erase on these parts, but bounded: an application that never
 *        calls e32p_uds_complete() cannot keep a tester waiting forever.
 */
#define E32P_UDS_PENDING_MAX_DEFAULT_MS 60000u

/**
 * @brief How the tester addressed the request.
 * A functional request that hits "not supported" or "out of range" gets no answer
 * at all, so that a broadcast is not answered with an error by every ECU.
 */
typedef enum {
    E32P_UDS_ADDR_PHYSICAL = 0,   /**< sent to this ECU alone (0x7E1)    */
    E32P_UDS_ADDR_FUNCTIONAL,     /**< broadcast to every ECU (0x7DF)    */
} e32p_uds_addressing_t;

/** @brief Diagnostic sessions (0x10 sub-functions). */
typedef enum {
    E32P_UDS_SESSION_DEFAULT     = 0x01,
    E32P_UDS_SESSION_PROGRAMMING = 0x02,
    E32P_UDS_SESSION_EXTENDED    = 0x03,
} e32p_uds_session_t;

/** @brief Session masks for the DID and routine tables. */
#define E32P_UDS_IN_DEFAULT     0x01u
#define E32P_UDS_IN_PROGRAMMING 0x02u
#define E32P_UDS_IN_EXTENDED    0x04u
#define E32P_UDS_IN_ANY         (E32P_UDS_IN_DEFAULT | E32P_UDS_IN_PROGRAMMING | E32P_UDS_IN_EXTENDED)

/**
 * @brief Negative response codes, and the two values that are not negative.
 * Callbacks return one of these: @ref E32P_UDS_NRC_OK for success,
 * @ref E32P_UDS_NRC_PENDING to finish later through e32p_uds_complete(), and
 * anything else to answer with that negative response.
 */
typedef enum {
    E32P_UDS_NRC_OK                     = 0x00, /**< success, not an NRC            */
    E32P_UDS_NRC_GENERAL_REJECT         = 0x10,
    E32P_UDS_NRC_SERVICE_NOT_SUPPORTED  = 0x11,
    E32P_UDS_NRC_SUBFUNC_NOT_SUPPORTED  = 0x12,
    E32P_UDS_NRC_BAD_LENGTH             = 0x13, /**< incorrect length or format     */
    E32P_UDS_NRC_RESPONSE_TOO_LONG      = 0x14,
    E32P_UDS_NRC_BUSY_REPEAT            = 0x21,
    E32P_UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22,
    E32P_UDS_NRC_SEQUENCE_ERROR         = 0x24,
    E32P_UDS_NRC_OUT_OF_RANGE           = 0x31,
    E32P_UDS_NRC_SECURITY_DENIED        = 0x33,
    E32P_UDS_NRC_INVALID_KEY            = 0x35,
    E32P_UDS_NRC_EXCEEDED_ATTEMPTS      = 0x36,
    E32P_UDS_NRC_DELAY_NOT_EXPIRED      = 0x37,
    E32P_UDS_NRC_DOWNLOAD_NOT_ACCEPTED  = 0x70,
    E32P_UDS_NRC_TRANSFER_SUSPENDED     = 0x71,
    E32P_UDS_NRC_PROGRAMMING_FAILURE    = 0x72,
    E32P_UDS_NRC_WRONG_BLOCK_COUNTER    = 0x73,
    E32P_UDS_NRC_PENDING                = 0x78, /**< "finish later", see header doc */
    E32P_UDS_NRC_SUBFUNC_NOT_IN_SESSION = 0x7E,
    E32P_UDS_NRC_SERVICE_NOT_IN_SESSION = 0x7F,
} e32p_uds_nrc_t;

/**
 * @brief Put one complete UDS message on the bus — a response, or a 0x78.
 * Called from e32p_uds_request(), e32p_uds_poll() or e32p_uds_complete(), in the
 * caller's context. Must not call back into the server. @p data is only valid
 * during the call: copy it before returning.
 * @return ESP_OK when the transport accepted the message
 */
typedef esp_err_t (*e32p_uds_send_fn)(const uint8_t *data, uint16_t len, void *user);

/* ---- 0x22 / 0x2E: data identifiers ------------------------------------------ */

/**
 * @brief One data identifier. Its preconditions live here, so a DID cannot be
 *        added without stating them.
 */
typedef struct {
    uint16_t did;             /**< e.g. 0xF190 (VIN)                              */
    uint16_t len;             /**< fixed length of its data record                */
    uint8_t  read_sessions;   /**< E32P_UDS_IN_* mask; 0 = cannot be read         */
    uint8_t  write_sessions;  /**< E32P_UDS_IN_* mask; 0 = cannot be written      */
    bool     write_secured;   /**< writing needs security access unlocked         */
} e32p_uds_did_t;

/**
 * @brief Fill @p out (@p len bytes, already zeroed) with the current value of
 *        @p did. Reads answer at once: PENDING is not supported here and is
 *        answered as 0x10 generalReject.
 */
typedef e32p_uds_nrc_t (*e32p_uds_did_read_fn)(uint16_t did, uint8_t *out, uint16_t len, void *user);

/** @brief Store @p len bytes for @p did. May return PENDING (e.g. an NVS write). */
typedef e32p_uds_nrc_t (*e32p_uds_did_write_fn)(uint16_t did, const uint8_t *data, uint16_t len,
                                                void *user);

/* ---- 0x27: security access --------------------------------------------------- */

/** @brief Seed and key length, in bytes. */
#define E32P_UDS_SECURITY_LEN          4u
/** @brief Wrong keys in a row that trigger the lockout; the last one answers 0x36. */
#define E32P_UDS_SECURITY_MAX_ATTEMPTS 3u
/**
 * @brief Lockout after too many wrong keys: seed requests answer 0x37 meanwhile.
 *        Neither the lockout nor the attempt count is cleared by a session change —
 *        otherwise switching sessions would reset a brute-force attempt.
 *
 * The same delay runs after a start (power-up or reset) when the last security access
 * failed — or when the server cannot tell, which is the case without an @c on_sec_load
 * hook (ISO 14229-1, SecurityAccess delay timer). It begins at the first time value
 * the server is given, because the clock restarts at every boot.
 */
#define E32P_UDS_SECURITY_DELAY_MS     10000u

/**
 * @brief Produce a fresh seed of @p len bytes for security @p level (1 = 0x27 01).
 *        Seeds answer at once: PENDING is not supported here and is answered 0x10.
 */
typedef e32p_uds_nrc_t (*e32p_uds_seed_fn)(uint8_t level, uint8_t *seed, uint8_t len, void *user);

/**
 * @brief Decide whether @p key answers @p seed. The algorithm and its secret live
 *        in the application — never in this component, and never in source.
 */
typedef bool (*e32p_uds_key_fn)(uint8_t level, const uint8_t *seed, const uint8_t *key, uint8_t len,
                                void *user);

/**
 * @brief Read the saved "the last security access failed" flag. Called once, from
 *        e32p_uds_init(), before the first request.
 * @param[in] user  the config's @c user pointer
 * @return true if the last security access before this start ended in a wrong key,
 *         or if the application cannot read the flag (answer true — the safe side).
 *         A MISSING flag may be answered false only when "missing" can only mean that
 *         no access has failed since the device's storage was initialised — true
 *         when wiping the storage also wipes the secret the keys come from. If a
 *         wipe could leave the secret in place, answer true for a missing flag too.
 */
typedef bool (*e32p_uds_sec_load_fn)(void *user);

/**
 * @brief Save the "the last security access failed" flag. Called when the value changes
 *        or the saved value is unknown (a true from on_sec_load counts as unknown):
 *        with true after a wrong key, with false after a right key. The server runs
 *        it inside the request, so keep it short (one NVS write). The server
 *        treats a save that reports failure as making the saved value unknown (the
 *        write may have landed anyway), so the next outcome saves again.
 *        Both @c on_sec_load and @c on_sec_store must be set, or neither.
 * @param[in] failed  the new value
 * @param[in] user    the config's @c user pointer
 * @return true if the value was saved
 */
typedef bool (*e32p_uds_sec_store_fn)(bool failed, void *user);

/* ---- 0x19 / 0x14: trouble codes ---------------------------------------------- */

/** @brief DTC format identifiers, as reported in the `19 01` answer. */
#define E32P_UDS_DTC_FORMAT_ISO15031 0x00u   /**< SAE J2012-DA / ISO 15031-6: OBD-style codes */
#define E32P_UDS_DTC_FORMAT_ISO14229 0x01u   /**< ISO 14229-1 native format                   */

/**
 * @brief Where the trouble codes live. An interface, so that the same store can
 *        also answer OBD-II — one ECU, two views — without either component
 *        depending on the other.
 *
 * The provider only lists codes. The server applies ISO 14229-1's matching rule
 * itself — a code matches when its status AND the request's mask AND
 * @ref availability_mask is non-zero — so every application gets it right.
 */
typedef struct {
    uint8_t availability_mask;   /**< status bits this ECU reports: first byte of every 0x19 answer */
    uint8_t format;              /**< E32P_UDS_DTC_FORMAT_*, reported in the 19 01 answer          */
    /** @brief How many codes the store holds right now. */
    uint16_t (*count)(void *user);
    /** @brief The @p index-th code (3 bytes, in @p dtc) and its status byte; false past the end. */
    bool (*at)(uint16_t index, uint32_t *dtc, uint8_t *status, void *user);
    /** @brief Clear @p group — the server only asks for 0xFFFFFF, all. NULL: 0x14 not supported. */
    e32p_uds_nrc_t (*clear)(uint32_t group, void *user);
} e32p_uds_dtc_provider_t;

/* ---- 0x31: routines ---------------------------------------------------------- */

/** @brief One routine and where it may run. */
typedef struct {
    uint16_t rid;        /**< e.g. 0xFF00 (erase memory)                     */
    uint8_t  sessions;   /**< E32P_UDS_IN_* mask                             */
    bool     secured;    /**< needs security access unlocked                 */
} e32p_uds_routine_t;

/**
 * @brief Run routine control @p control (0x01 start, 0x03 results) for @p rid.
 * @param[in]  option      routine option record from the request; empty for control 0x03
 *                         (the server answers 0x13 otherwise)
 * @param[in]  option_len  bytes in @p option
 * @param[out] status      routine status record to return to the tester
 * @param[in]  status_cap  capacity of @p status
 * @param[out] status_len  bytes written to @p status
 * @return OK, PENDING (finish with e32p_uds_complete()), or an NRC
 */
typedef e32p_uds_nrc_t (*e32p_uds_routine_fn)(uint16_t rid, uint8_t control,
                                              const uint8_t *option, uint16_t option_len,
                                              uint8_t *status, uint16_t status_cap,
                                              uint16_t *status_len, void *user);

/* ---- 0x34 / 0x36 / 0x37: download -------------------------------------------- */

/**
 * @brief Where downloaded bytes go. The server owns the block counter and the
 *        byte accounting; the sink only stores. Any step may return PENDING.
 *
 * The download services run in the programming session only; 0x34 needs security.
 * 0x34 accepts uncompressed, unencrypted data (data format 0x00) starting at
 * address 0, with 1 to 4 address and size bytes. 0x36 blocks carry at most
 * E32P_UDS_MAX_BLOCK_LEN bytes including the SID and counter; a repeat of the last
 * block is acknowledged without being written again. 0x37 accepts and ignores an
 * optional parameter record.
 */
typedef struct {
    uint32_t capacity;                                              /**< largest image, bytes      */
    e32p_uds_nrc_t (*begin)(uint32_t size, void *user);             /**< 0x34: ready to receive?   */
    e32p_uds_nrc_t (*write)(const uint8_t *data, uint16_t len, void *user); /**< 0x36 payload      */
    e32p_uds_nrc_t (*end)(void *user);                              /**< 0x37: validate the image  */
    void (*abort)(void *user);                                      /**< transfer abandoned        */
} e32p_uds_sink_t;

/* ---- 0x11: reset ------------------------------------------------------------- */

/**
 * @brief Reset of type @p reset_type (0x01 hard) was accepted. Called *after* the
 *        positive response went to the send callback — but the transport may still
 *        be holding it, so restart only once it has left the wire.
 */
typedef void (*e32p_uds_reset_fn)(uint8_t reset_type, void *user);

/** @brief Server configuration. Tables must outlive the handle. */
typedef struct {
    e32p_uds_send_fn               send;            /**< required                          */
    void                          *user;            /**< passed to every callback          */

    const e32p_uds_did_t          *dids;            /**< 0x22 / 0x2E table, or NULL        */
    uint8_t                        did_count;
    e32p_uds_did_read_fn           on_did_read;
    e32p_uds_did_write_fn          on_did_write;    /**< NULL: no DID is writable          */

    e32p_uds_seed_fn               on_seed;         /**< NULL: 0x27 not supported          */
    e32p_uds_key_fn                on_key;
    e32p_uds_sec_load_fn           on_sec_load;     /**< NULL: the security delay is armed at every start */
    e32p_uds_sec_store_fn          on_sec_store;    /**< NULL: the flag is not saved       */

    const e32p_uds_dtc_provider_t *dtc;             /**< NULL: 0x19 / 0x14 not supported   */

    const e32p_uds_routine_t      *routines;        /**< 0x31 table, or NULL               */
    uint8_t                        routine_count;
    e32p_uds_routine_fn            on_routine;

    const e32p_uds_sink_t         *sink;            /**< NULL: 0x34 / 0x36 / 0x37 not supported */

    e32p_uds_reset_fn              on_reset;        /**< NULL: 0x11 not supported          */

    uint16_t                       p2_ms;           /**< 0 -> E32P_UDS_P2_DEFAULT_MS       */
    uint16_t                       p2_star_ms;      /**< 0 -> E32P_UDS_P2_STAR_DEFAULT_MS  */
    uint16_t                       s3_ms;           /**< 0 -> E32P_UDS_S3_DEFAULT_MS       */
    uint32_t                       pending_max_ms;  /**< 0 -> E32P_UDS_PENDING_MAX_DEFAULT_MS */
} e32p_uds_config_t;

/** @brief Opaque server instance. */
typedef struct e32p_uds_ctx *e32p_uds_handle_t;

/**
 * @brief Create a server, in the default session with security locked.
 * @param[in]  cfg  configuration, copied
 * @param[out] out  handle
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or ESP_ERR_NO_MEM
 */
esp_err_t e32p_uds_init(const e32p_uds_config_t *cfg, e32p_uds_handle_t *out);

/** @brief Destroy a server, abandoning any transfer in progress. NULL is a no-op. */
esp_err_t e32p_uds_deinit(e32p_uds_handle_t h);

/**
 * @brief Handle one reassembled request. Whatever it answers goes out through the
 *        send callback, possibly later (see the header doc).
 * @param[in] h           server
 * @param[in] addressing  how the tester addressed it
 * @param[in] req         request bytes, starting at the service id
 * @param[in] len         length of @p req
 * @param[in] now_ms      current time, the same clock poll() is given
 * @return ESP_OK when the request was handled — a negative response is still a
 *         response — or ESP_ERR_INVALID_ARG on bad arguments
 */
esp_err_t e32p_uds_request(e32p_uds_handle_t h, e32p_uds_addressing_t addressing,
                           const uint8_t *req, uint16_t len, uint32_t now_ms);

/**
 * @brief Advance time: the S3 session timeout, the security delay and the 0x78
 *        repeats run from here. Call it at least every few milliseconds.
 */
esp_err_t e32p_uds_poll(e32p_uds_handle_t h, uint32_t now_ms);

/**
 * @brief Finish the request a callback answered with PENDING.
 *
 * The final answer is always sent — even when the tester asked for no positive
 * response — because it has already been told to wait (ISO 14229-1). While a
 * request is pending, the server answers every other supported service 0x21 (a silent
 * `3E 80` excepted; a service it does not offer stays 0x11, silent on a functional
 * request) and the S3 timer is held; it restarts when the answer goes out.
 *
 * @param[in] nrc     @ref E32P_UDS_NRC_OK, or the negative response to send
 * @param[in] data    bytes the response carries after its echoed part (a routine
 *                    status record, for instance); NULL when there are none
 * @param[in] len     length of @p data
 * @param[in] now_ms  current time
 * @return ESP_OK; ESP_ERR_INVALID_STATE when nothing is pending (or the request was
 *         already abandoned at the pending cap); ESP_ERR_INVALID_SIZE when @p data
 *         would not fit, in which case 0x14 is sent instead
 */
esp_err_t e32p_uds_complete(e32p_uds_handle_t h, e32p_uds_nrc_t nrc,
                            const uint8_t *data, uint16_t len, uint32_t now_ms);

/** @brief The active session. */
e32p_uds_session_t e32p_uds_session(e32p_uds_handle_t h);

#ifdef __cplusplus
}
#endif
