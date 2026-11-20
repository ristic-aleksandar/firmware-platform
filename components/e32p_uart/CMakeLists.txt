# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES esp_driver_uart + esp_driver_gpio -> both used only in the .c
#     (driver/uart.h pulls in driver/gpio.h; and we validate pins with the GPIO
#     SOC macros). ESP-IDF v5.x: UART lives in esp_driver_uart, not "driver".
set(srcs "")
if(CONFIG_E32P_UART_ENABLE)
    list(APPEND srcs "src/e32p_uart.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_uart" "esp_driver_gpio"
)
