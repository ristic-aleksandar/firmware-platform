idf_component_register(SRCS "main.c" "gw_config.c" "gw_source.c" "gw_provision.c"
                       INCLUDE_DIRS ""
                       REQUIRES e32p_wifi e32p_mqtt e32p_ble e32p_nvs e32p_ota
                                e32p_twai e32p_j1939 e32p_tm1637 e32p_wdt
                                e32p_sysinfo e32p_gpio app_update esp_timer)
