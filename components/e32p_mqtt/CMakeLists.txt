# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core   -> only e32p types are in the public header.
#   PRIV_REQUIRES mqtt   -> esp-mqtt (mqtt_client.h) is used only in the .c, so
#                           the esp-mqtt API does not leak to consumers.
set(srcs "")
if(CONFIG_E32P_MQTT_ENABLE)
    list(APPEND srcs "src/e32p_mqtt.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "mqtt" "esp_event"
)
