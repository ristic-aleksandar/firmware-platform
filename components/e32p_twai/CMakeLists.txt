# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES driver -> the TWAI driver (driver/twai.h) is in the legacy
#     "driver" component (not split into an esp_driver_*). Used only in the .c.
set(srcs "")
if(CONFIG_E32P_TWAI_ENABLE)
    list(APPEND srcs "src/e32p_twai.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "driver"
)
