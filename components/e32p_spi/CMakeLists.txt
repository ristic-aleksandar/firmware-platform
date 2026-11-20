# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES esp_driver_spi + esp_driver_gpio -> both used only in the .c
#     (spi_master.h and the GPIO SOC validation macros). ESP-IDF v5.x: SPI master
#     lives in esp_driver_spi, not the legacy "driver".
set(srcs "")
if(CONFIG_E32P_SPI_ENABLE)
    list(APPEND srcs "src/e32p_spi.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_spi" "esp_driver_gpio"
)
