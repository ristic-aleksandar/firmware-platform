# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES esp_driver_dac -> driver/dac_oneshot.h, used only in the .c.
set(srcs "")
if(CONFIG_E32P_DAC_ENABLE)
    list(APPEND srcs "src/e32p_dac.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_dac"
)
