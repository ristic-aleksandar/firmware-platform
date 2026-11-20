#pragma once
/**
 * @file gw_source.h
 * @brief The gateway's field-data source: something that produces J1939-shaped
 *        signal samples. Two interchangeable backends implement it —
 *
 *          - sim   : a built-in simulator (no hardware; always available).
 *          - j1939 : a real CAN bus read over e32p_twai + e32p_j1939 (needs an
 *                    SN65HVD230-class transceiver).
 *
 * The rest of the gateway (normalize -> JSON -> MQTT) is written against this
 * interface, so swapping the physical bus in is a one-line change at startup.
 */
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One decoded signal, normalized to engineering units. */
typedef struct {
    uint32_t    pgn;    /*!< J1939 PGN the value came from. */
    const char *name;   /*!< Signal name, e.g. "engine_speed". */
    float       value;  /*!< Engineering value. */
    const char *unit;   /*!< "rpm", "degC", "kPa", "pct", ... */
} gw_sample_t;

/** @brief A field-data source backend. */
typedef struct {
    const char *kind;                                /*!< "sim" or "j1939". */
    esp_err_t (*start)(void);                        /*!< Bring the source up. */
    int       (*poll)(gw_sample_t *out, int max);    /*!< Fill up to @p max samples;
                                                          returns count, or <0 on error. */
} gw_source_t;

/** @brief The built-in simulator source (no hardware needed). */
const gw_source_t *gw_source_sim(void);

/** @brief The real J1939/CAN source (needs a transceiver on the CAN pins). */
const gw_source_t *gw_source_j1939(void);

#ifdef __cplusplus
}
#endif
