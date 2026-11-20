# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES: components used only in the .c (esp_system is a common
#     requirement, so it need not be listed). If a header isn't found at build,
#     add the component that owns it here.
set(srcs "")
if(CONFIG_E32P_SYSINFO_ENABLE)
    list(APPEND srcs "src/e32p_sysinfo.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_hw_support" "esp_app_format" "spi_flash" "esp_timer"
)
