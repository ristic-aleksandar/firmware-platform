/**
 * @file main.c
 * @brief 15_i2s_tone — streams a 440 Hz sine tone over I2S TX.
 *
 * Proves e32p_i2s. On a bare board this proves the TX/clock path (steady
 * throughput, no errors) — the ESP32 is the I2S master so it clocks data out
 * regardless. Wire a MAX98357A (DIN<-GPIO22, BCLK<-GPIO26, LRC<-GPIO25) to hear
 * the tone through a speaker.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include "e32p_i2s.h"

#define BCLK_GPIO   26
#define WS_GPIO     25
#define DOUT_GPIO   22
#define SAMPLE_RATE 16000
#define TONE_HZ     440
#define N_SAMPLES   1600           /* 100 ms of audio */
#define TWO_PI      6.28318530718f

static const char *TAG = "i2s_tone";
static int16_t s_tone[N_SAMPLES];

void app_main(void)
{
    for (int i = 0; i < N_SAMPLES; i++) {
        s_tone[i] = (int16_t)(12000.0f * sinf(TWO_PI * TONE_HZ * i / SAMPLE_RATE));
    }

    e32p_i2s_config_t cfg = { .port = 0, .dir = E32P_I2S_TX, .bclk_gpio = BCLK_GPIO,
                              .ws_gpio = WS_GPIO, .data_gpio = DOUT_GPIO,
                              .sample_rate = SAMPLE_RATE, .bits = 16, .stereo = false };
    e32p_i2s_handle_t i2s = NULL;
    ESP_ERROR_CHECK(e32p_i2s_init(&cfg, &i2s));
    ESP_LOGI(TAG, "streaming %d Hz on DOUT=%d - wire a MAX98357A (DIN<-%d BCLK<-%d LRC<-%d) to hear it",
             TONE_HZ, DOUT_GPIO, DOUT_GPIO, BCLK_GPIO, WS_GPIO);

    uint32_t bytes = 0;
    uint32_t last = 0;
    while (1) {
        size_t written = 0;
        ESP_ERROR_CHECK(e32p_i2s_write(i2s, s_tone, sizeof(s_tone), &written, 1000));
        bytes += written;

        const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last >= 1000) {
            ESP_LOGI(TAG, "streaming %d Hz tone, %lu KB/s", TONE_HZ, (unsigned long)(bytes / 1024));
            bytes = 0;
            last = now;
        }
    }
}
