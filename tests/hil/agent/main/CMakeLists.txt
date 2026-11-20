idf_component_register(
    SRCS "hil_diag.c" "main.c" "hil_agent.c" "hil_can.c" "hil_sys.c" "hil_periph.c"
         "hil_bus.c" "hil_wireless.c" "hil_sensor.c" "hil_pm.c" "hil_mqtt.c" "hil_modbus.c" "hil_ota.c" "hil_net.c" "hil_ble.c"
    INCLUDE_DIRS "."
    REQUIRES e32p_core e32p_sysinfo e32p_nvs e32p_event e32p_wdt
             e32p_gpio e32p_ledc e32p_adc e32p_dac e32p_touch e32p_pcnt e32p_gptimer
             e32p_uart e32p_i2c e32p_spi e32p_i2s e32p_ws2812
             e32p_wifi e32p_espnow
             e32p_twai e32p_j1939 e32p_j1939tp e32p_j1939_acl e32p_isotp e32p_obd2 e32p_uds
             e32p_bmp280 e32p_tm1637 e32p_pm e32p_mqtt e32p_modbus e32p_ota e32p_ble
             esp_driver_uart esp_timer driver mbedtls)
