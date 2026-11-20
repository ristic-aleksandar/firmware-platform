idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES e32p_twai e32p_isotp e32p_obd2 e32p_nvs e32p_core
                                esp_timer driver)
