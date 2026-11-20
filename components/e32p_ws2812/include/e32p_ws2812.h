#pragma once
/**
 * @file e32p_ws2812.h
 * @brief WS2812 / NeoPixel addressable-LED driver over the RMT peripheral.
 *
 * Colors are staged in an internal per-pixel buffer, then @ref e32p_ws2812_refresh
 * clocks the whole strip out in one RMT transaction (bytes-encoded GRB stream
 * followed by the WS2812 reset/latch pulse). A single RMT TX channel and a
 * composite (bytes + copy) encoder back each handle.
 */
#include <stdint.h>
#include "e32p_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque strip handle. */
typedef struct e32p_ws2812_ctx *e32p_ws2812_handle_t;

/**
 * @brief Create a WS2812 strip driver.
 *
 * @param[in]  gpio     Data GPIO wired to the strip's DIN.
 * @param[in]  num_leds Number of pixels on the strip (>= 1).
 * @param[out] out      Receives the strip handle on success.
 * @return ESP_OK; ESP_ERR_INVALID_ARG on bad args; ESP_ERR_NO_MEM on allocation
 *         failure; or an RMT driver error.
 */
esp_err_t e32p_ws2812_init(int gpio, int num_leds, e32p_ws2812_handle_t *out);

/**
 * @brief Release the strip: disable/delete the RMT channel and encoder, free buffers.
 * @param[in] h Strip handle (NULL is a no-op).
 * @return ESP_OK.
 */
esp_err_t e32p_ws2812_deinit(e32p_ws2812_handle_t h);

/**
 * @brief Stage one pixel's color in the buffer (does not touch the strip).
 * @param[in] h     Strip handle.
 * @param[in] index Pixel index, 0 .. num_leds-1.
 * @param[in] r,g,b 8-bit color channels.
 * @return ESP_OK; ESP_ERR_INVALID_ARG on bad handle or out-of-range index.
 */
esp_err_t e32p_ws2812_set_pixel(e32p_ws2812_handle_t h, int index,
                                uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Stage all pixels to off (does not touch the strip until refresh).
 * @param[in] h Strip handle.
 * @return ESP_OK; ESP_ERR_INVALID_ARG on bad handle.
 */
esp_err_t e32p_ws2812_clear(e32p_ws2812_handle_t h);

/**
 * @brief Clock the staged buffer out to the strip and wait for the latch.
 * @param[in] h Strip handle.
 * @return ESP_OK; ESP_ERR_INVALID_ARG on bad handle; or an RMT driver error.
 */
esp_err_t e32p_ws2812_refresh(e32p_ws2812_handle_t h);

#ifdef __cplusplus
}
#endif
