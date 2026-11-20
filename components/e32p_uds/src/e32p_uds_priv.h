/**
 * @file e32p_uds_priv.h
 * @brief Shared internals of e32p_uds — not part of the public API.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "e32p_uds.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Response buffer size; the largest answer is a long 0x19 DTC list. */
#define UDS_RESP_MAX 256u

#define UDS_SID_CLEAR_DTC       0x14u
#define UDS_SID_READ_DTC        0x19u
#define UDS_SID_SECURITY_ACCESS 0x27u
#define UDS_SID_ROUTINE_CONTROL 0x31u
#define UDS_SID_REQUEST_DOWNLOAD 0x34u
#define UDS_SID_TRANSFER_DATA   0x36u
#define UDS_SID_TRANSFER_EXIT   0x37u

/** @brief 0x27 state: unlocked or not, the outstanding seed, attempts, the lockout. */
typedef struct {
    bool     unlocked;                       /**< key accepted; cleared by any session change */
    bool     seed_issued;                    /**< a seed is outstanding; it answers one key  */
    uint8_t  seed[E32P_UDS_SECURITY_LEN];
    uint8_t  failed_attempts;                /**< wrong keys in a row; survives sessions     */
    bool     delay_active;                   /**< lockout running; survives sessions         */
    bool     delay_start_pending;            /**< delay armed at start; its clock starts at the
                                                  first time the server sees                 */
    uint32_t delay_start_ms;
    bool     flag_known;                     /**< the saved "last access failed" value is known */
    bool     flag_failed;                    /**< that value, as last read or saved         */
} uds_security_t;

/** @brief Where a download stands. */
typedef enum {
    UDS_XFER_IDLE = 0,          /**< no download                                */
    UDS_XFER_STARTING,          /**< 0x34 reached the sink, not yet accepted    */
    UDS_XFER_ACTIVE,            /**< accepted: blocks may come                  */
} uds_xfer_state_t;

/** @brief 0x34 / 0x36 / 0x37 state. */
typedef struct {
    uds_xfer_state_t state;
    uint32_t expected;          /**< bytes announced in 0x34                    */
    uint32_t received;          /**< bytes the sink has accepted                */
    uint8_t  next_counter;      /**< block sequence counter expected next       */
    bool     have_last;         /**< a block was accepted: it may be repeated   */
    uint8_t  last_counter;      /**< counter of the last accepted block         */
    uint8_t  block_counter;     /**< the block in flight                        */
    uint16_t block_len;
} uds_transfer_t;

struct e32p_uds_ctx;

/**
 * @brief Commits what a request changes, once its outcome is known: right after the
 *        handler when it answers at once, or at e32p_uds_complete() when it was
 *        pending. One place for every commit, so a failure never half-applies.
 */
typedef void (*uds_finish_t)(struct e32p_uds_ctx *c, e32p_uds_nrc_t outcome);

/** @brief The request a callback is still working on (0x78 sent). */
typedef struct {
    bool         active;
    uint8_t      sid;
    uint16_t     echo_len;      /**< positive-response prefix already in resp   */
    uint32_t     started_ms;    /**< for the pending cap                        */
    uint32_t     last_78_ms;    /**< when the last 0x78 went out                */
    uds_finish_t finish;
} uds_pending_t;

/** @brief Server instance. */
struct e32p_uds_ctx {
    e32p_uds_config_t  cfg;
    e32p_uds_session_t session;
    uint32_t           now_ms;            /**< time of the request being handled   */
    uint32_t           last_request_ms;   /**< S3 runs from here                   */
    uint8_t            reset_type;        /**< 0x11 accepted, reset after response */
    uds_finish_t       finish;            /**< set by the handler of this request  */
    uds_pending_t      pend;
    uds_security_t     sec;
    uds_transfer_t     xfer;
    uint8_t            resp[UDS_RESP_MAX];
};

/**
 * @brief A service handler: builds the positive response in c->resp and sets
 *        *resp_len, or returns the NRC. Runs only after the precondition chain.
 */
typedef e32p_uds_nrc_t (*uds_handler_t)(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                        uint16_t *resp_len);

/** @brief The E32P_UDS_IN_* bit for a session. */
uint8_t uds_session_mask(e32p_uds_session_t s);

/** @brief 0x19 ReadDTCInformation and 0x14 ClearDiagnosticInformation — e32p_uds_dtc.c. */
e32p_uds_nrc_t uds_read_dtc_info(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                 uint16_t *resp_len);
e32p_uds_nrc_t uds_clear_dtc(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                             uint16_t *resp_len);

/** @brief 0x31 RoutineControl — e32p_uds_routine.c. */
e32p_uds_nrc_t uds_routine_control(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                   uint16_t *resp_len);

/**
 * @brief Set the 0x27 state at start: ask the load hook whether the last access failed
 *        and arm the delay if it did — or if there is no hook to ask.
 */
void uds_security_init(struct e32p_uds_ctx *c);

/**
 * @brief Give the security delay its start time. Call it whenever c->now_ms was just
 *        set. A delay armed at start begins at the first time the server sees.
 */
void uds_security_time(struct e32p_uds_ctx *c);

/** @brief 0x27 SecurityAccess — e32p_uds_security.c. */
e32p_uds_nrc_t uds_security_access(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                   uint16_t *resp_len);

/**
 * @brief Lock again and forget any outstanding seed — on every session change and
 *        on reset. The attempt counter and the lockout delay are kept on purpose.
 */
void uds_security_lock(struct e32p_uds_ctx *c);

/** @brief 0x34 / 0x36 / 0x37 — e32p_uds_transfer.c. */
e32p_uds_nrc_t uds_request_download(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                    uint16_t *resp_len);
e32p_uds_nrc_t uds_transfer_data(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                 uint16_t *resp_len);
e32p_uds_nrc_t uds_transfer_exit(struct e32p_uds_ctx *c, const uint8_t *req, uint16_t len,
                                 uint16_t *resp_len);

/** @brief Abandon any download that reached the sink, telling it (abort). */
void uds_transfer_reset(struct e32p_uds_ctx *c);

#ifdef __cplusplus
}
#endif
