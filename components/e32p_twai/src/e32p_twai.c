/**
 * @file e32p_twai.c
 * @brief e32p_twai implementation — CAN 2.0 (TWAI) controller facade (singleton).
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "e32p_twai.h"

/* Queue depths come from Kconfig, but a project whose sdkconfig was generated
   before these symbols existed will not carry them and IDF does not always
   regenerate on a component Kconfig change. Fall back to the Kconfig default so
   a stale config still builds and behaves identically. */
#ifndef CONFIG_E32P_TWAI_RX_QUEUE_LEN
#  define E32P_TWAI_RX_QUEUE_LEN 5
#else
#  define E32P_TWAI_RX_QUEUE_LEN CONFIG_E32P_TWAI_RX_QUEUE_LEN
#endif
#ifndef CONFIG_E32P_TWAI_TX_QUEUE_LEN
#  define E32P_TWAI_TX_QUEUE_LEN 5
#else
#  define E32P_TWAI_TX_QUEUE_LEN CONFIG_E32P_TWAI_TX_QUEUE_LEN
#endif

static const char *TAG = "e32p_twai";
static bool s_installed = false;

static twai_mode_t map_mode(e32p_twai_mode_t m)
{
    switch (m) {
    case E32P_TWAI_MODE_NO_ACK:      return TWAI_MODE_NO_ACK;
    case E32P_TWAI_MODE_LISTEN_ONLY: return TWAI_MODE_LISTEN_ONLY;
    case E32P_TWAI_MODE_NORMAL:
    default:                         return TWAI_MODE_NORMAL;
    }
}

static esp_err_t map_timing(uint32_t bitrate, twai_timing_config_t *out)
{
    switch (bitrate) {
    case 125000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS(); *out = t; return ESP_OK; }
    case 250000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS(); *out = t; return ESP_OK; }
    case 500000:  { twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS(); *out = t; return ESP_OK; }
    case 1000000: { twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();   *out = t; return ESP_OK; }
    default:      return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t e32p_twai_init(const e32p_twai_config_t *cfg)
{
    E32P_CHECK_ARG(cfg != NULL, TAG);
    E32P_CHECK_ARG(cfg->tx_gpio >= 0 && cfg->rx_gpio >= 0, TAG);
    E32P_RETURN_ON_FALSE(!s_installed, ESP_ERR_INVALID_STATE, TAG, "already installed");

    twai_timing_config_t t_config;
    E32P_RETURN_ON_ERR(map_timing(cfg->bitrate_bps, &t_config), TAG);

    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(cfg->tx_gpio, cfg->rx_gpio, map_mode(cfg->mode));
    /* The DEFAULT macro hardcodes 5 for both queues. Override from Kconfig so a
       high-bitrate or bursty user (ISO-TP) can size them without patching. */
    g_config.rx_queue_len = E32P_TWAI_RX_QUEUE_LEN;
    g_config.tx_queue_len = E32P_TWAI_TX_QUEUE_LEN;
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    /* twai_driver_install() makes the TX pin a plain GPIO output and prints a log
       line before it connects the TWAI controller to the pin (IDF 5.3.1,
       twai.c:298-299). In that gap the pin drives its output register bit, which
       is 0 after reset: CAN dominant. With INFO logs on a 115200 console that held
       the bus dominant for ~8 ms at every bring-up. Preset the bit to 1. */
    E32P_RETURN_ON_ERR(gpio_set_level((gpio_num_t)cfg->tx_gpio, 1u), TAG);
    E32P_RETURN_ON_ERR(twai_driver_install(&g_config, &t_config, &f_config), TAG);
    s_installed = true;
    ESP_LOGI(TAG, "twai installed tx=%d rx=%d bitrate=%lu mode=%d", cfg->tx_gpio, cfg->rx_gpio,
             (unsigned long)cfg->bitrate_bps, (int)cfg->mode);
    return ESP_OK;
}

esp_err_t e32p_twai_deinit(void)
{
    if (!s_installed) {
        return ESP_OK;
    }
    twai_stop();                 /* ignore error if not started */
    E32P_RETURN_ON_ERR(twai_driver_uninstall(), TAG);
    s_installed = false;
    return ESP_OK;
}

esp_err_t e32p_twai_start(void)
{
    E32P_RETURN_ON_FALSE(s_installed, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_twai_init first");
    return twai_start();
}

esp_err_t e32p_twai_stop(void)
{
    E32P_RETURN_ON_FALSE(s_installed, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_twai_init first");
    return twai_stop();
}

esp_err_t e32p_twai_transmit(const e32p_twai_message_t *msg, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(msg != NULL, TAG);
    E32P_CHECK_ARG(msg->len <= 8, TAG);
    E32P_RETURN_ON_FALSE(s_installed, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_twai_init first");

    twai_message_t tx = {0};
    tx.identifier       = msg->id;
    tx.data_length_code = msg->len;
    tx.flags            = (msg->extended ? TWAI_MSG_FLAG_EXTD : 0) |
                          (msg->self ? TWAI_MSG_FLAG_SELF : 0);
    memcpy(tx.data, msg->data, msg->len);
    return twai_transmit(&tx, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t e32p_twai_receive(e32p_twai_message_t *msg, uint32_t timeout_ms)
{
    E32P_CHECK_ARG(msg != NULL, TAG);
    E32P_RETURN_ON_FALSE(s_installed, ESP_ERR_E32P_NOT_INIT, TAG, "call e32p_twai_init first");

    twai_message_t rx = {0};
    esp_err_t err = twai_receive(&rx, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        return err;
    }
    memset(msg, 0, sizeof(*msg));
    msg->id       = rx.identifier;
    msg->extended = (rx.flags & TWAI_MSG_FLAG_EXTD) != 0;
    msg->len      = (rx.data_length_code > 8) ? 8 : rx.data_length_code;
    memcpy(msg->data, rx.data, msg->len);
    return ESP_OK;
}
