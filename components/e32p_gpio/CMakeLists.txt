# Cherry-pick by CONDITIONAL SRCS, never by guarding idf_component_register:
# ESP-IDF expands REQUIRES/PRIV_REQUIRES before Kconfig is loaded, so those must
# be unconditional. When CONFIG_E32P_GPIO_ENABLE is off, srcs is empty and this
# component compiles nothing (a config-only interface).
#
#   REQUIRES e32p_core      -> its headers appear in our PUBLIC header.
#   PRIV_REQUIRES esp_driver_gpio -> driver/gpio.h is used only in the .c; in
#     ESP-IDF v5.x GPIO lives in esp_driver_gpio, not the legacy "driver".
set(srcs "")
if(CONFIG_E32P_GPIO_ENABLE)
    list(APPEND srcs "src/e32p_gpio.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_gpio"
)
