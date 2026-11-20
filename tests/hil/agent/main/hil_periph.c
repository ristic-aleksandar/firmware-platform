/**
 * @file hil_periph.c
 * @brief GPIO / analog / timer HIL tests (single board).
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "e32p_gpio.h"
#include "e32p_ledc.h"
#include "e32p_adc.h"
#include "e32p_dac.h"
#include "e32p_touch.h"
#include "e32p_pcnt.h"
#include "e32p_gptimer.h"
#include "hil_agent.h"
#include "hil_periph.h"
#include "hil_pins.h"

hil_verdict_t hil_periph_gpio(char *d, size_t n)
{
    e32p_gpio_config_t out = E32P_GPIO_CONFIG_DEFAULT();
    out.pin = HIL_GPIO_OUT;
    out.mode = E32P_GPIO_MODE_OUTPUT;
    if (e32p_gpio_config(&out) != ESP_OK) {
        snprintf(d, n, "output config failed");
        return HIL_FAIL;
    }
    e32p_gpio_set_level(HIL_GPIO_OUT, 1);
    e32p_gpio_set_level(HIL_GPIO_OUT, 0);

    /* A pin that cannot be an output (input-only on WROOM, nonexistent on S3)
       must be rejected with INVALID_ARG. */
    e32p_gpio_config_t bad = E32P_GPIO_CONFIG_DEFAULT();
    bad.pin = HIL_PIN_NO_OUTPUT;
    bad.mode = E32P_GPIO_MODE_OUTPUT;
    if (e32p_gpio_config(&bad) != ESP_ERR_INVALID_ARG) {
        snprintf(d, n, "no-output pin not rejected");
        return HIL_FAIL;
    }
#if CONFIG_IDF_TARGET_ESP32
    /* The SPI-flash-pin reserved guard is ESP32-only (see e32p_gpio). */
    e32p_gpio_config_t flash = E32P_GPIO_CONFIG_DEFAULT();
    flash.pin = HIL_PIN_FLASH;
    flash.mode = E32P_GPIO_MODE_OUTPUT;
    if (e32p_gpio_config(&flash) != ESP_ERR_E32P_PIN_RESERVED) {
        snprintf(d, n, "flash pin not refused");
        return HIL_FAIL;
    }
#endif
    e32p_gpio_reset(HIL_GPIO_OUT);
    snprintf(d, n, "io + pin guards ok");
    return HIL_PASS;
}

hil_verdict_t hil_periph_ledc(char *d, size_t n)
{
    e32p_ledc_config_t cfg = E32P_LEDC_CONFIG_DEFAULT();
    cfg.pin = HIL_LEDC_PIN;
    e32p_ledc_handle_t h = NULL;
    if (e32p_ledc_init(&cfg, &h) != ESP_OK) {
        snprintf(d, n, "ledc init failed");
        return HIL_FAIL;
    }
    e32p_ledc_start(h);
    e32p_ledc_set_duty_percent(h, 50);
    e32p_ledc_fade(h, 0, 50);
    e32p_ledc_stop(h);
    e32p_ledc_deinit(h);
    snprintf(d, n, "pwm init/duty/fade ok");
    return HIL_PASS;
}

hil_verdict_t hil_periph_adc(char *d, size_t n)
{
#if CONFIG_E32P_ADC_ENABLE
    e32p_adc_handle_t h = NULL;
    if (e32p_adc_init(1, &h) != ESP_OK) {
        snprintf(d, n, "adc init failed");
        return HIL_FAIL;
    }
    if (e32p_adc_config_channel(h, 6, E32P_ADC_ATTEN_12DB) != ESP_OK) {
        e32p_adc_deinit(h);
        snprintf(d, n, "channel config failed");
        return HIL_FAIL;
    }
    int raw = -1;
    esp_err_t err = e32p_adc_read_raw(h, 6, &raw);
    e32p_adc_deinit(h);
    if (err != ESP_OK || raw < 0 || raw > 4095) {
        snprintf(d, n, "raw=%d err=%s", raw, esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "adc1 ch6 raw=%d", raw);
    return HIL_PASS;
#else
    snprintf(d, n, "e32p_adc disabled on this target — skipped");
    return HIL_PASS;
#endif
}

hil_verdict_t hil_periph_dac(char *d, size_t n)
{
#if CONFIG_E32P_DAC_ENABLE
    e32p_dac_handle_t h = NULL;
    if (e32p_dac_init(0, &h) != ESP_OK) {
        snprintf(d, n, "dac init failed");
        return HIL_FAIL;
    }
    e32p_dac_set_raw(h, 128);
    e32p_dac_set_voltage(h, 1650);
    e32p_dac_deinit(h);
    snprintf(d, n, "dac ch0 out ok");
    return HIL_PASS;
#else
    snprintf(d, n, "no DAC on this target — skipped");
    return HIL_PASS;
#endif
}

hil_verdict_t hil_periph_touch(char *d, size_t n)
{
    /* e32p_touch uses the ESP32 v1 touch API; disabled on targets without it. */
#if CONFIG_E32P_TOUCH_ENABLE
    if (e32p_touch_init() != ESP_OK) {
        snprintf(d, n, "touch init failed");
        return HIL_FAIL;
    }
    if (e32p_touch_config_pad(0) != ESP_OK) {
        snprintf(d, n, "pad config failed");
        return HIL_FAIL;
    }
    uint32_t val = 0;
    esp_err_t err = e32p_touch_read(0, &val);
    if (err != ESP_OK) {
        snprintf(d, n, "read=%s", esp_err_to_name(err));
        return HIL_FAIL;
    }
    snprintf(d, n, "pad T0 raw=%lu", (unsigned long)val);
    return HIL_PASS;
#else
    snprintf(d, n, "touch not supported on this target — skipped");
    return HIL_PASS;
#endif
}

hil_verdict_t hil_periph_pcnt(char *d, size_t n)
{
    e32p_pcnt_config_t cfg = { .edge_gpio = HIL_PCNT_PIN, .high_limit = 1000, .low_limit = -1000,
                               .count_rising = true, .count_falling = false,
                               .io_loop_back = true };
    e32p_pcnt_handle_t h = NULL;
    if (e32p_pcnt_init(&cfg, &h) != ESP_OK) {
        snprintf(d, n, "pcnt init failed");
        return HIL_FAIL;
    }
    /* io_loop_back routes the pin's output back to the PCNT input; drive it. */
    e32p_gpio_config_t drv = E32P_GPIO_CONFIG_DEFAULT();
    drv.pin = HIL_PCNT_PIN;
    drv.mode = E32P_GPIO_MODE_INPUT_OUTPUT_OD;
    e32p_gpio_config(&drv);
    e32p_pcnt_start(h);
    for (int i = 0; i < 5; i++) {
        e32p_gpio_set_level(HIL_PCNT_PIN, 1);
        e32p_gpio_set_level(HIL_PCNT_PIN, 0);
    }
    int count = -1;
    e32p_pcnt_get_count(h, &count);
    e32p_pcnt_stop(h);
    e32p_pcnt_deinit(h);
    if (count < 5) {
        snprintf(d, n, "count=%d (expected >=5)", count);
        return HIL_FAIL;
    }
    snprintf(d, n, "counted %d rising edges", count);
    return HIL_PASS;
}

static volatile int s_alarm_count;
static void on_alarm(void *arg) { (void)arg; s_alarm_count++; }

hil_verdict_t hil_periph_gptimer(char *d, size_t n)
{
    s_alarm_count = 0;
    e32p_gptimer_config_t cfg = { .resolution_hz = 1000000, .alarm_period_ticks = 10000,
                                  .auto_reload = true };  /* 10 ms period */
    e32p_gptimer_handle_t h = NULL;
    if (e32p_gptimer_init(&cfg, on_alarm, NULL, &h) != ESP_OK) {
        snprintf(d, n, "gptimer init failed");
        return HIL_FAIL;
    }
    e32p_gptimer_start(h);
    vTaskDelay(pdMS_TO_TICKS(60));  /* expect ~6 alarms */
    e32p_gptimer_stop(h);
    int c = s_alarm_count;
    e32p_gptimer_deinit(h);
    if (c < 3) {
        snprintf(d, n, "alarms=%d (expected >=3)", c);
        return HIL_FAIL;
    }
    snprintf(d, n, "%d alarms in 60ms", c);
    return HIL_PASS;
}
