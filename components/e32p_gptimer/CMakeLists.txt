# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES esp_driver_gptimer -> driver/gptimer.h, used only in the .c.
set(srcs "")
if(CONFIG_E32P_GPTIMER_ENABLE)
    list(APPEND srcs "src/e32p_gptimer.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_gptimer"
)
