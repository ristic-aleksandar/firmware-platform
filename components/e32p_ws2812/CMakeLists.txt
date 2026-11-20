# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES esp_driver_rmt -> RMT TX + encoder API, used only in the .c.
set(srcs "")
if(CONFIG_E32P_WS2812_ENABLE)
    list(APPEND srcs "src/e32p_ws2812.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_rmt"
)
