# TEMPLATE module. The if/else guard means a disabled module compiles nothing
# yet still registers as a component (so Kconfig switches resolve cleanly).
# When you copy this into components/e32p_<name>/, rename FOO/foo accordingly.
# TEMPLATE module CMakeLists.
#
# Cherry-pick works by CONDITIONAL SRCS, NOT by guarding idf_component_register.
# ESP-IDF expands REQUIRES/PRIV_REQUIRES to build the dependency graph BEFORE
# Kconfig is loaded, so they MUST be unconditional -- a `if(CONFIG_...)` around
# the register call silently drops all requirements. When the module is
# disabled, srcs is empty and the component compiles nothing.
#
#   REQUIRES: components whose headers appear in your PUBLIC header (include/).
#     e32p_core is always needed (error/log/version conventions).
#   Depending on ANOTHER e32p_* module? REQUIRES/PRIV_REQUIRES pulls it into the
#     build, but its SOURCE compiles only when its own CONFIG_..._ENABLE is set
#     (conditional SRCS). So ALSO add `select E32P_<DEP>_ENABLE` in your Kconfig,
#     or you get undefined-reference link errors (e.g. e32p_wifi selects
#     E32P_NVS_ENABLE + E32P_EVENT_ENABLE).
#   PRIV_REQUIRES: components used only inside src/ (.c). In ESP-IDF v5.x the
#     driver is split into granular components -- require the specific one:
#       GPIO -> esp_driver_gpio   I2C -> esp_driver_i2c   UART -> esp_driver_uart
#       SPI  -> esp_driver_spi    LEDC-> esp_driver_ledc  (ADC -> esp_adc)
#     Do NOT use the legacy umbrella "driver" component.
#     Also require components you include TRANSITIVELY: e.g. driver/ledc.h pulls
#     in driver/gpio.h, so an LEDC module needs esp_driver_ledc AND
#     esp_driver_gpio. If a header isn't found, add the component that owns it.
set(srcs "")
if(CONFIG_E32P_FOO_ENABLE)
    list(APPEND srcs "src/e32p_foo.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    # PRIV_REQUIRES "esp_driver_<periph>"   # unconditional; see note above
)
