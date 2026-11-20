# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES driver -> the ESP32 touch sensor is the ONE peripheral still in
#     the legacy "driver" component (driver/touch_sensor.h); it was not split into
#     an esp_driver_* like the others. Used only in the .c.
set(srcs "")
if(CONFIG_E32P_TOUCH_ENABLE)
    list(APPEND srcs "src/e32p_touch.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "driver"
)
