# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core, e32p_i2c -> both appear in our public header
#     (e32p_bmp280.h includes e32p_i2c.h for the bus/device handle types).
set(srcs "")
if(CONFIG_E32P_BMP280_ENABLE)
    list(APPEND srcs "src/e32p_bmp280.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core" "e32p_i2c"
)
