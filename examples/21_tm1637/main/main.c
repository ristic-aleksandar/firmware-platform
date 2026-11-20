/**
 * @file main.c
 * @brief 21_tm1637 — drive a TM1637 4-digit 7-segment display.
 *
 * Shows a fixed number, sweeps brightness on "8888", then runs a mm:ss clock
 * with the colon lit. Writes are one-way (no read-back), so if the display stays
 * blank, check VCC and the CLK/DIO wiring.
 *
 * Wiring: CLK=GPIO23, DIO=GPIO19, VCC=3V3 (or 5V), GND=GND.
 * (CLK/DIO on the SPI pins matches the HIL bench so one wired display serves both.)
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_tm1637.h"

static const char *TAG = "tm1637_demo";

void app_main(void)
{
    e32p_tm1637_config_t cfg = { .clk_pin = 23, .dio_pin = 19, .brightness = 4 };
    e32p_tm1637_handle_t disp = NULL;
    ESP_ERROR_CHECK(e32p_tm1637_init(&cfg, &disp));
    ESP_LOGI(TAG, "TM1637 on CLK=23 DIO=19; running demo.");

    /* 1) A fixed number. */
    e32p_tm1637_show_int(disp, 1234, false, false);
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* 2) Brightness sweep on all-eights (every segment lit). */
    const uint8_t eights[E32P_TM1637_DIGITS] = { 0x7F, 0x7F, 0x7F, 0x7F };
    e32p_tm1637_set_segments(disp, eights, E32P_TM1637_DIGITS, 0);
    for (uint8_t b = 0; b <= E32P_TM1637_MAX_BRIGHT; b++) {
        e32p_tm1637_set_brightness(disp, b);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /* 3) A ticking mm:ss clock with the colon on. */
    ESP_LOGI(TAG, "clock ticking (mm:ss).");
    int mm = 11, ss = 58;
    while (1) {
        e32p_tm1637_show_int(disp, mm * 100 + ss, true, true);
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (++ss >= 60) {
            ss = 0;
            if (++mm >= 100) {
                mm = 0;
            }
        }
    }
}
