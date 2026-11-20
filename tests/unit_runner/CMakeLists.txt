# Unity test runner for the platform. Builds the test/ suites of the listed
# components (TEST_COMPONENTS) and runs them on-target.
#
# Requirements/REQUIRES are unconditional, but the modules' SOURCES are gated by
# their CONFIG_E32P_*_ENABLE (see sdkconfig.defaults) so the code under test is
# actually compiled in.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# Components whose test/ directories should be built and run.
set(TEST_COMPONENTS
    e32p_gpio
    e32p_ledc
    e32p_event
    e32p_nvs
    e32p_sysinfo
    e32p_wdt
    e32p_uart
    e32p_i2c
    e32p_spi
    e32p_adc
    e32p_gptimer
    e32p_dac
    e32p_touch
    e32p_pcnt
    e32p_wifi
    e32p_espnow
    e32p_twai
    e32p_i2s
    e32p_ws2812
    e32p_j1939
    e32p_j1939tp
    e32p_isotp
    e32p_obd2
    e32p_uds
    e32p_j1939_acl
    e32p_bmp280
    e32p_tm1637
    e32p_pm
    e32p_mqtt
    e32p_modbus
    e32p_ota
    e32p_ble
    CACHE STRING "Component unit tests to build")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unit_runner)
