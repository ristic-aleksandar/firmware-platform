/**
 * @file main.c
 * @brief 07_adc_read — reads an ADC1 channel and logs raw + calibrated mV.
 *
 * Proves e32p_adc. On a bare board GPIO34 floats (noisy mid-scale); jumper the
 * pin to 3V3 for ~full-scale or to GND for ~0 to watch it track.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "e32p_adc.h"

#define ADC_UNIT_NUM   1
#define ADC_CHAN       6   /* ADC1_CH6 = GPIO34 */

static const char *TAG = "adc_read";

void app_main(void)
{
    e32p_adc_handle_t adc = NULL;
    ESP_ERROR_CHECK(e32p_adc_init(ADC_UNIT_NUM, &adc));
    ESP_ERROR_CHECK(e32p_adc_config_channel(adc, ADC_CHAN, E32P_ADC_ATTEN_12DB));
    ESP_LOGI(TAG, "reading ADC%d ch%d (GPIO34) - jumper pin to 3V3 or GND to see it swing",
             ADC_UNIT_NUM, ADC_CHAN);

    while (1) {
        int raw = 0;
        int mv = 0;
        ESP_ERROR_CHECK(e32p_adc_read_raw(adc, ADC_CHAN, &raw));
        if (e32p_adc_read_voltage(adc, ADC_CHAN, &mv) == ESP_OK) {
            ESP_LOGI(TAG, "raw=%4d  mV=%4d", raw, mv);
        } else {
            ESP_LOGI(TAG, "raw=%4d  (no calibrated mV)", raw);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
