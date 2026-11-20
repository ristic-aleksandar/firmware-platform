# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core, esp_hw_support -> esp_sleep.h is in the public header.
#   PRIV_REQUIRES esp_driver_gpio      -> rtc_io / gpio wake helpers (in the .c).
set(srcs "")
if(CONFIG_E32P_PM_ENABLE)
    list(APPEND srcs "src/e32p_pm.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core" "esp_hw_support"
    PRIV_REQUIRES "esp_driver_gpio"
)
