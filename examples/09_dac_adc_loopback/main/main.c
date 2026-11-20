/**
 * @file main.c
 * @brief 09_dac_adc_loopback — drives GPIO25 with the DAC and reads it back with
 *        ADC1 on GPIO34. Needs a single jumper wire GPIO25 -> GPIO34.
 *
 * NOTE: GPIO25 is both DAC ch0 AND ADC2 ch8, but the DAC output and ADC2 input
 * contend for the same RTC pad through the oneshot drivers, so an *internal*
 * readback does not track. Reading the DAC back on an independent unit (ADC1,
 * GPIO34) works — hence the jumper. This also shows ADC1 working regardless of
 * Wi-Fi (unlike ADC2).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_dac.h"
#include "e32p_adc.h"

#define DAC_CH     0    /* GPIO25 */
#define ADC_UNIT   1    /* ADC1 (independent of the DAC's RTC pad) */
#define ADC_CH     6    /* ADC1_CH6 = GPIO34 */

static const char *TAG = "dac_adc";

void app_main(void)
{
    e32p_dac_handle_t dac = NULL;
    ESP_ERROR_CHECK(e32p_dac_init(DAC_CH, &dac));

    e32p_adc_handle_t adc = NULL;
    ESP_ERROR_CHECK(e32p_adc_init(ADC_UNIT, &adc));
    ESP_ERROR_CHECK(e32p_adc_config_channel(adc, ADC_CH, E32P_ADC_ATTEN_12DB));

    ESP_LOGI(TAG, "DAC ch%d (GPIO25) -> ADC1 ch%d (GPIO34) - jumper GPIO25->GPIO34 to see tracking",
             DAC_CH, ADC_CH);

    static const uint8_t codes[] = { 0, 64, 128, 192, 255 };
    while (1) {
        for (size_t i = 0; i < sizeof(codes); i++) {
            const uint8_t code = codes[i];
            ESP_ERROR_CHECK(e32p_dac_set_raw(dac, code));
            vTaskDelay(pdMS_TO_TICKS(50));   /* let the pin settle */

            int raw = 0;
            int mv = 0;
            ESP_ERROR_CHECK(e32p_adc_read_raw(adc, ADC_CH, &raw));
            e32p_adc_read_voltage(adc, ADC_CH, &mv);

            const unsigned dac_mv = (unsigned)((uint32_t)code * 3300u / 255u);
            ESP_LOGI(TAG, "dac=%3u (~%4u mV) -> adc raw=%4d mv=%4d",
                     (unsigned)code, dac_mv, raw, mv);
        }
        ESP_LOGI(TAG, "----");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
