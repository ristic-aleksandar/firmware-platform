# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES esp_driver_i2c + esp_driver_gpio -> both used only in the .c
#     (i2c_master.h and the GPIO SOC validation macros). ESP-IDF v5.x: I2C lives
#     in esp_driver_i2c (the new i2c_master driver), not the legacy "driver".
set(srcs "")
if(CONFIG_E32P_I2C_ENABLE)
    list(APPEND srcs "src/e32p_i2c.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_i2c" "esp_driver_gpio"
)
