# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES esp_driver_i2s -> the I2S std driver, used only in the .c.
set(srcs "")
if(CONFIG_E32P_I2S_ENABLE)
    list(APPEND srcs "src/e32p_i2s.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "esp_driver_i2s"
)
