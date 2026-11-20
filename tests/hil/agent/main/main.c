/**
 * @file main.c
 * @brief HIL test agent entry point. Installs the control UART, loads the role
 *        from NVS, and runs the command loop. One binary for both boards; the
 *        role (DUT / COMPANION) is provisioned once via `@CMD SETROLE`.
 */
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "e32p_nvs.h"
#include "hil_pins.h"
#include "hil_agent.h"
#include "hil_can.h"
#include "hil_bus.h"
#include "hil_wireless.h"
#include "hil_modbus.h"

#if CONFIG_E32P_BENCH_FORBIDDEN
#error "HIL agent is bench-only (UDS_TEST_SECRET in hil_diag.c, plain-HTTP OTA); " \
       "this build is marked as a product (E32P_PRODUCT_BUILD, Secure Boot or Flash Encryption)"
#endif

#define HIL_UART     UART_NUM_0
#define HIL_LINE_MAX 256

void app_main(void)
{
    /* Park the CAN TX pin recessive first. It is undriven from chip reset until
       e32p_twai_init() configures it, so anything that biases the line — a logic
       analyzer probe does exactly this (WIRING.md §12.4) — can hold the bus dominant
       until the first CAN test. Set the level before the direction, so enabling the
       output never drives the reset value (0 = dominant). */
    ESP_ERROR_CHECK(gpio_set_level(HIL_TWAI_TX, 1));
    ESP_ERROR_CHECK(gpio_set_direction(HIL_TWAI_TX, GPIO_MODE_OUTPUT));

    hil_out_init();   /* before anything logs or emits: one lock for both paths */

    ESP_ERROR_CHECK(e32p_nvs_init());

    /* Install the driver on the console UART so we can read command lines.
       Logs still go out on the same UART; protocol lines carry the `@` prefix
       so the PC harness can separate them. */
    ESP_ERROR_CHECK(uart_driver_install(HIL_UART, 1024, 0, 0, NULL, 0));

    hil_agent_init();   /* loads role from NVS, emits "@EVT READY role=..." */

    /* A COMPANION board runs the autonomous peers (CAN/J1939, UART echo,
       ESP-NOW). These use independent peripherals (TWAI, UART1, radio) so they
       coexist. The DUT leaves them idle and drives each per-test on command. */
    if (hil_agent_is_companion()) {
        hil_can_start_companion();
        hil_bus_start_companion();
        hil_wireless_start_companion();
        hil_modbus_start_companion();
    }

    char line[HIL_LINE_MAX];
    size_t idx = 0;
    for (;;) {
        uint8_t ch;
        int r = uart_read_bytes(HIL_UART, &ch, 1, portMAX_DELAY);
        if (r != 1) {
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            if (idx > 0) {
                line[idx] = '\0';
                hil_agent_dispatch(line);
                idx = 0;
            }
        } else if (idx < sizeof(line) - 1) {
            line[idx++] = (char)ch;
        } else {
            idx = 0;   /* overflow — drop the oversized line */
        }
    }
}
