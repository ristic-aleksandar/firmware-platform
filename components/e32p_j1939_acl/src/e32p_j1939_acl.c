/**
 * @file e32p_j1939_acl.c
 * @brief e32p_j1939_acl implementation — J1939-81 NAME codec + claim engine.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "e32p_j1939_acl.h"

static const char *TAG = "e32p_j1939_acl";

#define ACL_PRIORITY  6u
#define ACL_TC_MS     250u    /* contention window after a claim */
#define ACL_SA_MIN    128u    /* self-configurable address range (J1939) */
#define ACL_SA_MAX    247u

/* --- NAME codec (bit layout per J1939-81) -------------------------------- */

uint64_t e32p_j1939_name_encode(const e32p_j1939_name_t *n)
{
    if (n == NULL) {
        return 0;
    }
    uint64_t v = 0;
    v |= (uint64_t)(n->identity_number & 0x1FFFFFu);            /* bits 0..20  */
    v |= (uint64_t)(n->manufacturer_code & 0x7FFu) << 21;       /* bits 21..31 */
    v |= (uint64_t)(n->ecu_instance & 0x7u) << 32;             /* bits 32..34 */
    v |= (uint64_t)(n->function_instance & 0x1Fu) << 35;       /* bits 35..39 */
    v |= (uint64_t)(n->function & 0xFFu) << 40;                /* bits 40..47 */
    /* bit 48 reserved = 0 */
    v |= (uint64_t)(n->vehicle_system & 0x7Fu) << 49;          /* bits 49..55 */
    v |= (uint64_t)(n->vehicle_system_instance & 0xFu) << 56;  /* bits 56..59 */
    v |= (uint64_t)(n->industry_group & 0x7u) << 60;          /* bits 60..62 */
    v |= (uint64_t)(n->arbitrary_address_capable ? 1u : 0u) << 63;
    return v;
}

void e32p_j1939_name_decode(uint64_t v, e32p_j1939_name_t *out)
{
    if (out == NULL) {
        return;
    }
    out->identity_number          = (uint32_t)(v & 0x1FFFFFu);
    out->manufacturer_code        = (uint16_t)((v >> 21) & 0x7FFu);
    out->ecu_instance             = (uint8_t)((v >> 32) & 0x7u);
    out->function_instance        = (uint8_t)((v >> 35) & 0x1Fu);
    out->function                 = (uint8_t)((v >> 40) & 0xFFu);
    out->vehicle_system           = (uint8_t)((v >> 49) & 0x7Fu);
    out->vehicle_system_instance  = (uint8_t)((v >> 56) & 0xFu);
    out->industry_group           = (uint8_t)((v >> 60) & 0x7u);
    out->arbitrary_address_capable = ((v >> 63) & 0x1u) != 0;
}

/* --- Engine -------------------------------------------------------------- */

typedef enum { ACL_IDLE, ACL_CLAIMING, ACL_CLAIMED, ACL_CANNOT_CLAIM } acl_state_t;

struct e32p_j1939_acl_ctx {
    uint64_t              name;
    uint8_t               current_sa;
    acl_state_t           state;
    bool                  timer_armed;
    uint32_t              claim_start_ms;
    uint32_t              now_ms;
    e32p_j1939_acl_tx_fn  tx;
    e32p_j1939_acl_evt_fn on_event;
    void                 *user;
    uint8_t               seen[32];   /* bitmap of addresses others have claimed */
};

static void seen_set(struct e32p_j1939_acl_ctx *c, uint8_t sa)
{
    c->seen[sa >> 3] |= (uint8_t)(1u << (sa & 7u));
}
static bool seen_get(struct e32p_j1939_acl_ctx *c, uint8_t sa)
{
    return (c->seen[sa >> 3] >> (sa & 7u)) & 1u;
}

/* Emit an Address Claimed (or Cannot Claim when sa == null) carrying our NAME. */
static void emit_claim(struct e32p_j1939_acl_ctx *c, uint8_t sa)
{
    e32p_j1939_message_t m = {
        .priority = ACL_PRIORITY,
        .pgn      = E32P_J1939_PGN_ADDR_CLAIMED,
        .src_addr = sa,
        .dst_addr = E32P_J1939_ADDR_GLOBAL,
        .len      = 8,
    };
    for (int i = 0; i < 8; i++) {
        m.data[i] = (uint8_t)((c->name >> (8 * i)) & 0xFFu);
    }
    (void)c->tx(&m, c->user);
}

static void notify(struct e32p_j1939_acl_ctx *c, uint8_t sa, bool claimed)
{
    if (c->on_event) {
        c->on_event(sa, claimed, c->user);
    }
}

/* Pick the next free self-configurable address; false if none. */
static bool relocate(struct e32p_j1939_acl_ctx *c)
{
    for (uint32_t a = ACL_SA_MIN; a <= ACL_SA_MAX; a++) {
        if (!seen_get(c, (uint8_t)a)) {
            c->current_sa = (uint8_t)a;
            return true;
        }
    }
    return false;
}

esp_err_t e32p_j1939_acl_init(const e32p_j1939_acl_config_t *cfg, e32p_j1939_acl_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);
    E32P_CHECK_ARG(cfg->tx != NULL, TAG);
    E32P_CHECK_ARG(cfg->preferred_sa <= 253u, TAG);

    struct e32p_j1939_acl_ctx *c = calloc(1, sizeof(*c));
    ESP_RETURN_ON_FALSE(c != NULL, ESP_ERR_NO_MEM, TAG, "alloc failed");
    c->name = cfg->name;
    c->current_sa = cfg->preferred_sa;
    c->state = ACL_IDLE;
    c->tx = cfg->tx;
    c->on_event = cfg->on_event;
    c->user = cfg->user;
    *out = c;
    return ESP_OK;
}

esp_err_t e32p_j1939_acl_deinit(e32p_j1939_acl_handle_t h)
{
    free(h);
    return ESP_OK;
}

esp_err_t e32p_j1939_acl_start(e32p_j1939_acl_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    h->state = ACL_CLAIMING;
    h->timer_armed = false;
    emit_claim(h, h->current_sa);
    return ESP_OK;
}

static void lose_address(struct e32p_j1939_acl_ctx *h)
{
    e32p_j1939_name_t self;
    e32p_j1939_name_decode(h->name, &self);
    if (self.arbitrary_address_capable && relocate(h)) {
        h->state = ACL_CLAIMING;
        h->timer_armed = false;
        emit_claim(h, h->current_sa);          /* re-claim the new address */
    } else {
        h->current_sa = E32P_J1939_ADDR_NULL;  /* 254 */
        h->state = ACL_CANNOT_CLAIM;
        emit_claim(h, E32P_J1939_ADDR_NULL);   /* Cannot Claim Address */
        notify(h, E32P_J1939_ADDR_NULL, false);
    }
}

esp_err_t e32p_j1939_acl_on_frame(e32p_j1939_acl_handle_t h, const e32p_j1939_message_t *f)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(f != NULL, TAG);

    if (f->pgn == E32P_J1939_PGN_ADDR_CLAIMED) {
        if (f->len < 8) {
            return ESP_OK;
        }
        uint64_t rival = 0;
        for (int i = 0; i < 8; i++) {
            rival |= (uint64_t)f->data[i] << (8 * i);
        }
        if (rival == h->name) {
            return ESP_OK;   /* our own claim echoed back — ignore */
        }
        if (f->src_addr <= 253u) {
            seen_set(h, f->src_addr);
        }
        /* Contention only if the rival is claiming the address we want. */
        if ((h->state == ACL_CLAIMING || h->state == ACL_CLAIMED) &&
            f->src_addr == h->current_sa) {
            if (rival < h->name) {
                lose_address(h);               /* lower NAME wins — we yield */
            } else {
                emit_claim(h, h->current_sa);  /* we win — defend our address */
            }
        }
    } else if (f->pgn == E32P_J1939_PGN_REQUEST) {
        if (f->len < 3) {
            return ESP_OK;
        }
        uint32_t requested = (uint32_t)f->data[0] | ((uint32_t)f->data[1] << 8) |
                             ((uint32_t)f->data[2] << 16);
        if (requested == E32P_J1939_PGN_ADDR_CLAIMED) {
            emit_claim(h, h->current_sa);      /* SA (or null) carries the answer */
        }
    }
    return ESP_OK;
}

void e32p_j1939_acl_poll(e32p_j1939_acl_handle_t h, uint32_t now_ms)
{
    if (h == NULL) {
        return;
    }
    h->now_ms = now_ms;
    if (h->state != ACL_CLAIMING) {
        return;
    }
    if (!h->timer_armed) {
        h->timer_armed = true;
        h->claim_start_ms = now_ms;
    } else if (now_ms - h->claim_start_ms > ACL_TC_MS) {
        h->state = ACL_CLAIMED;
        notify(h, h->current_sa, true);
    }
}

uint8_t e32p_j1939_acl_current_sa(e32p_j1939_acl_handle_t h)
{
    return (h != NULL) ? h->current_sa : E32P_J1939_ADDR_NULL;
}

bool e32p_j1939_acl_is_claimed(e32p_j1939_acl_handle_t h)
{
    return (h != NULL) && (h->state == ACL_CLAIMED);
}
