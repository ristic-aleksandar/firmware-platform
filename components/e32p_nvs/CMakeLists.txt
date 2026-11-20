# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core            -> its headers appear in our PUBLIC header.
#   PRIV_REQUIRES nvs_flash       -> nvs_flash.h / nvs.h used only in the .c, so
#     nvs stays out of our public API (consumers need not require it).
set(srcs "")
if(CONFIG_E32P_NVS_ENABLE)
    list(APPEND srcs "src/e32p_nvs.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "nvs_flash"
)
