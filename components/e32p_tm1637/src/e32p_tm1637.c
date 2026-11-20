/**
 * @file e32p_tm1637.c
 * @brief e32p_tm1637 implementation — pure font/formatting + bit-banged 2-wire.
 *
 * The TM1637 uses an I2C-like frame (start/stop + per-byte ACK) with no device
 * address and LSB-first data, so it is bit-banged over two open-drain GPIOs.
 * Timing is generous (~5 µs/edge, ~100 kHz); the part tolerates far faster.
 */
#include <stdlib.h>
#include <string.h>
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "e32p_gpio.h"
#include "e32p_tm1637.h"

static const char *TAG = "e32p_tm1637";

/* TM1637 command bytes. */
#define CMD_DATA_WRITE  0x40  /* write display data, auto-increment address */
#define CMD_ADDR_BASE   0xC0  /* address command, | position (0..5) */
#define CMD_DISPLAY_ON  0x88  /* display on, | brightness (0..7) */

#define BIT_DELAY_US    5

/* 7-segment font, 0-9 then A-F (bit0=a … bit6=g). */
static const uint8_t DIGIT_SEG[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71,
};

struct e32p_tm1637_ctx {
    int     clk_pin;
    int     dio_pin;
    uint8_t brightness;
};

/* ---- Pure functions ------------------------------------------------------- */

uint8_t e32p_tm1637_encode_digit(uint8_t digit)
{
    return (digit < 16) ? DIGIT_SEG[digit] : 0x00;
}

esp_err_t e32p_tm1637_format_int(int value, bool leading_zeros, bool colon, uint8_t out[E32P_TM1637_DIGITS])
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    bool neg = value < 0;
    unsigned mag = neg ? (unsigned)(-value) : (unsigned)value;

    uint8_t seg[E32P_TM1637_DIGITS] = { 0, 0, 0, 0 };   /* blank */
    int idx = E32P_TM1637_DIGITS - 1;
    do {
        seg[idx--] = DIGIT_SEG[mag % 10u];
        mag /= 10u;
    } while (mag != 0 && idx >= 0);

    if (mag != 0) {
        return ESP_ERR_INVALID_ARG;   /* more digits than positions */
    }
    if (neg) {
        if (idx < 0) {
            return ESP_ERR_INVALID_ARG;   /* no room for the sign */
        }
        seg[idx--] = E32P_TM1637_SEG_MINUS;
    }
    if (leading_zeros) {
        while (idx >= 0) {
            seg[idx--] = DIGIT_SEG[0];
        }
    }
    if (colon) {
        seg[1] |= E32P_TM1637_SEG_COLON;
    }
    memcpy(out, seg, sizeof(seg));
    return ESP_OK;
}

/* ---- Bit-bang primitives -------------------------------------------------- */

static inline void bit_delay(void) { esp_rom_delay_us(BIT_DELAY_US); }

static inline void clk(struct e32p_tm1637_ctx *h, uint32_t v) { e32p_gpio_set_level(h->clk_pin, v); }
static inline void dio(struct e32p_tm1637_ctx *h, uint32_t v) { e32p_gpio_set_level(h->dio_pin, v); }

static void tm_start(struct e32p_tm1637_ctx *h)
{
    clk(h, 1); dio(h, 1); bit_delay();
    dio(h, 0); bit_delay();   /* DIO falls while CLK high */
}

static void tm_stop(struct e32p_tm1637_ctx *h)
{
    clk(h, 0); bit_delay();
    dio(h, 0); bit_delay();
    clk(h, 1); bit_delay();
    dio(h, 1); bit_delay();   /* DIO rises while CLK high */
}

/* Write one byte, LSB first, then read the ACK bit.
 * Returns true if the TM1637 acknowledged (pulled DIO low), false if not — a
 * missing/unpowered display leaves DIO high (pull-up), so a NAK means "absent". */
static bool tm_write_byte(struct e32p_tm1637_ctx *h, uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        clk(h, 0); bit_delay();
        dio(h, (b >> i) & 0x01); bit_delay();
        clk(h, 1); bit_delay();
    }
    /* ACK slot: drop CLK, release DIO (open-drain high = float on the pull-up),
       let the chip pull it low, then sample while CLK is still low. */
    clk(h, 0); dio(h, 1); bit_delay();
    int level = 1;
    e32p_gpio_get_level(h->dio_pin, &level);
    clk(h, 1); bit_delay();
    clk(h, 0); bit_delay();
    return level == 0;   /* DIO low = ACK (chip present) */
}

/* Returns true if the display-control byte was acknowledged. */
static bool tm_send_display_ctrl(struct e32p_tm1637_ctx *h)
{
    tm_start(h);
    bool ack = tm_write_byte(h, CMD_DISPLAY_ON | (h->brightness & 0x07));
    tm_stop(h);
    return ack;
}

/* ---- Device functions ----------------------------------------------------- */

esp_err_t e32p_tm1637_init(const e32p_tm1637_config_t *cfg, e32p_tm1637_handle_t *out)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(out != NULL, TAG);

    struct e32p_tm1637_ctx *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->clk_pin    = cfg->clk_pin;
    ctx->dio_pin    = cfg->dio_pin;
    ctx->brightness = (cfg->brightness > E32P_TM1637_MAX_BRIGHT) ? E32P_TM1637_MAX_BRIGHT : cfg->brightness;

    /* Open-drain + readable (for ACK), with weak pull-ups; modules add stronger ones. */
    const e32p_gpio_config_t clk_cfg = {
        .pin = cfg->clk_pin, .mode = E32P_GPIO_MODE_INPUT_OUTPUT_OD,
        .pull = E32P_GPIO_PULL_UP, .intr = E32P_GPIO_INTR_NONE };
    const e32p_gpio_config_t dio_cfg = {
        .pin = cfg->dio_pin, .mode = E32P_GPIO_MODE_INPUT_OUTPUT_OD,
        .pull = E32P_GPIO_PULL_UP, .intr = E32P_GPIO_INTR_NONE };

    esp_err_t err = e32p_gpio_config(&clk_cfg);
    if (err == ESP_OK) {
        err = e32p_gpio_config(&dio_cfg);
    }
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }
    clk(ctx, 1);
    dio(ctx, 1);   /* idle: both released high */

    /* Probe: a present TM1637 acknowledges the display-control byte. No ACK means
       nothing is on the wire (unwired / unpowered display) — fail so callers know. */
    if (!tm_send_display_ctrl(ctx)) {
        ESP_LOGE(TAG, "no TM1637 ACK on clk=%d dio=%d - display connected/powered?",
                 cfg->clk_pin, cfg->dio_pin);
        e32p_gpio_reset(ctx->clk_pin);
        e32p_gpio_reset(ctx->dio_pin);
        free(ctx);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "tm1637 init clk=%d dio=%d brightness=%u", cfg->clk_pin, cfg->dio_pin, ctx->brightness);
    *out = ctx;
    return ESP_OK;
}

esp_err_t e32p_tm1637_deinit(e32p_tm1637_handle_t h)
{
    if (h == NULL) {
        return ESP_OK;
    }
    e32p_gpio_reset(h->clk_pin);
    e32p_gpio_reset(h->dio_pin);
    free(h);
    return ESP_OK;
}

esp_err_t e32p_tm1637_set_brightness(e32p_tm1637_handle_t h, uint8_t level)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(level <= E32P_TM1637_MAX_BRIGHT, TAG);
    h->brightness = level;
    return tm_send_display_ctrl(h) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t e32p_tm1637_set_segments(e32p_tm1637_handle_t h, const uint8_t *seg, size_t len, uint8_t pos)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    E32P_CHECK_ARG(seg != NULL, TAG);
    E32P_CHECK_ARG(len > 0 && (size_t)pos + len <= E32P_TM1637_DIGITS, TAG);

    bool ack = true;   /* &= keeps every byte sent (no short-circuit) */

    /* Set auto-increment write mode. */
    tm_start(h);
    ack &= tm_write_byte(h, CMD_DATA_WRITE);
    tm_stop(h);

    /* Address command + the data bytes. */
    tm_start(h);
    ack &= tm_write_byte(h, CMD_ADDR_BASE | pos);
    for (size_t i = 0; i < len; i++) {
        ack &= tm_write_byte(h, seg[i]);
    }
    tm_stop(h);

    /* Re-assert display-on / brightness. */
    ack &= tm_send_display_ctrl(h);
    return ack ? ESP_OK : ESP_ERR_NOT_FOUND;   /* no ACK = display gone mid-run */
}

esp_err_t e32p_tm1637_show_int(e32p_tm1637_handle_t h, int value, bool leading_zeros, bool colon)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    uint8_t seg[E32P_TM1637_DIGITS];
    E32P_RETURN_ON_ERR(e32p_tm1637_format_int(value, leading_zeros, colon, seg), TAG);
    return e32p_tm1637_set_segments(h, seg, E32P_TM1637_DIGITS, 0);
}

esp_err_t e32p_tm1637_clear(e32p_tm1637_handle_t h)
{
    E32P_CHECK_ARG(h != NULL, TAG);
    const uint8_t blank[E32P_TM1637_DIGITS] = { 0, 0, 0, 0 };
    return e32p_tm1637_set_segments(h, blank, E32P_TM1637_DIGITS, 0);
}
