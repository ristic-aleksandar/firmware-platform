#pragma once
/**
 * @file hil_pins.h
 * @brief Per-target pin map for the HIL agent — one place, one block per chip.
 *
 * The agent runs on a mix of boards (ESP32-WROOM-32, ESP32-S3, ...). Because two
 * boards are joined by wires / a bus, each board may use *different* GPIOs for
 * the same signal — so pins are resolved per `CONFIG_IDF_TARGET_*` here and used
 * as macros everywhere else. Adding a new ESP target = add one block below and
 * a per-board column in tests/hil/WIRING.md.
 *
 * S3 constraints honored below: GPIO22–25 do not exist; 26–37 are flash + 8 MB
 * PSRAM; 19/20 are native-USB; 0/3/45/46 are strapping. (The S3 has no DAC and a
 * different touch peripheral — those two tests are skipped on S3, see
 * hil_periph.c + sdkconfig.defaults.esp32s3.)
 */
#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32)

/* --- ESP32-WROOM-32 (the original bring-up board) --- */
#define HIL_TWAI_TX      21
#define HIL_TWAI_RX      22
#define HIL_UART1_TX     17
#define HIL_UART1_RX     16
#define HIL_UART_LOOP_TX 32   /* internal-loopback test: dedicated, unwired pins */
#define HIL_UART_LOOP_RX 33   /* so the two-board UART1 cross-wire can't interfere */
#define HIL_MODBUS_TX    25
#define HIL_MODBUS_RX    27
#define HIL_I2C_SDA      13   /* off the 21/22 TWAI pins so CAN + BMP280 coexist */
#define HIL_I2C_SCL      14
#define HIL_SPI_SCLK     18
#define HIL_SPI_MOSI     23
#define HIL_SPI_MISO     19
#define HIL_SPI_CS       5
#define HIL_I2S_BCLK     26
#define HIL_I2S_WS       32   /* off 25 (Modbus TX) and 22 (TWAI RX): I2S is single- */
#define HIL_I2S_DATA     33   /* board, so it borrows the unwired UART-loopback pins  */
#define HIL_TM1637_CLK   23   /* CLK+DIO share only the SPI-loopback pins (MOSI/MISO), */
#define HIL_TM1637_DIO   19   /* which assert no data - so a permanently-wired display */
                              /* can't disturb pcnt (GPIO18) or touch T0 (GPIO4).      */
#define HIL_WS2812_DIN   18
#define HIL_GPIO_OUT     2
#define HIL_LEDC_PIN     18
#define HIL_PCNT_PIN     18
#define HIL_PIN_NO_OUTPUT 34   /* input-only on WROOM -> output config rejected */
#define HIL_PIN_FLASH     7    /* SPI-flash pin (6-11) -> reserved (ESP32 only)  */

#elif defined(CONFIG_IDF_TARGET_ESP32S3)

/* --- ESP32-S3 (avoids 22-25, flash/PSRAM 26-37, USB 19/20, strapping 0/3/45/46) --- */
#define HIL_TWAI_TX      4
#define HIL_TWAI_RX      5
#define HIL_UART1_TX     17
#define HIL_UART1_RX     18
#define HIL_UART_LOOP_TX 38   /* internal-loopback test: dedicated, unwired pins */
#define HIL_UART_LOOP_RX 39   /* so the two-board UART1 cross-wire can't interfere */
#define HIL_MODBUS_TX    6
#define HIL_MODBUS_RX    7
#define HIL_I2C_SDA      8
#define HIL_I2C_SCL      9
#define HIL_SPI_SCLK     10
#define HIL_SPI_MOSI     11
#define HIL_SPI_MISO     12
#define HIL_SPI_CS       13
#define HIL_I2S_BCLK     14
#define HIL_I2S_WS       15
#define HIL_I2S_DATA     16
#define HIL_TM1637_CLK   1
#define HIL_TM1637_DIO   2
#define HIL_WS2812_DIN   48    /* DevKitC-1 onboard addressable LED */
#define HIL_GPIO_OUT     21
#define HIL_LEDC_PIN     21
#define HIL_PCNT_PIN     21
#define HIL_PIN_NO_OUTPUT 22   /* nonexistent on S3 -> config rejected (INVALID_ARG) */
/* HIL_PIN_FLASH: n/a — the SPI-flash-pin guard is ESP32-only; test skips it on S3 */

#else
#error "hil_pins.h: add a pin block for this target"
#endif

/* --- Bus-wide CAN bitrate (every target) ------------------------------------
 * One CAN bus is one bitrate: the J1939 tests, the diagnostics tests and the
 * COMPANION must all agree. Follows the ISO-TP Kconfig choice when e32p_isotp is
 * enabled, so a rate-sweep build moves everything together; otherwise it is the
 * 250 kbit J1939 bench rate. */
#if defined(CONFIG_E32P_ISOTP_BITRATE_BPS)
#define HIL_CAN_BITRATE CONFIG_E32P_ISOTP_BITRATE_BPS
#else
#define HIL_CAN_BITRATE 250000
#endif
