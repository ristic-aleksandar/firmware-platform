# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES esp_driver_pcnt + esp_driver_gpio -> pulse_cnt.h + the GPIO SOC
#     validation macros, used only in the .c.
set(srcs "")
if(CONFIG_E32P_PCNT_ENABLE)
    list(APPEND srcs "src/e32p_pcnt.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_pcnt" "esp_driver_gpio"
)
