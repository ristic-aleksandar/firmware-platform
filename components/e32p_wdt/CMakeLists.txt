# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   esp_task_wdt.h lives in esp_system, which is a common requirement, so no
#   extra PRIV_REQUIRES is needed. If the header isn't found, add esp_system.
set(srcs "")
if(CONFIG_E32P_WDT_ENABLE)
    list(APPEND srcs "src/e32p_wdt.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
)
