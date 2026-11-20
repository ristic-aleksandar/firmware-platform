# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core        -> public header includes e32p_core.h.
#   PRIV_REQUIRES e32p_gpio   -> pin config/level, used only in the .c.
#   PRIV_REQUIRES esp_rom     -> esp_rom_delay_us for the bit-bang timing.
set(srcs "")
if(CONFIG_E32P_TM1637_ENABLE)
    list(APPEND srcs "src/e32p_tm1637.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "e32p_gpio" "esp_rom"
)
