# Cherry-pick by CONDITIONAL SRCS; REQUIRES/PRIV_REQUIRES stay unconditional
# (ESP-IDF expands them before Kconfig is loaded). When CONFIG_E32P_LEDC_ENABLE
# is off, srcs is empty and this component compiles nothing.
#
#   REQUIRES e32p_core        -> its headers appear in our PUBLIC header.
#   PRIV_REQUIRES esp_driver_ledc + esp_driver_gpio -> both used only in the .c.
#     driver/ledc.h itself #includes driver/gpio.h, and esp_driver_ledc keeps
#     esp_driver_gpio private, so we must require esp_driver_gpio too.
set(srcs "")
if(CONFIG_E32P_LEDC_ENABLE)
    list(APPEND srcs "src/e32p_ledc.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_ledc" "esp_driver_gpio"
)
