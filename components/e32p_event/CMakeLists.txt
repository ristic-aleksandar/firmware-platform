# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core esp_event -> both appear in our PUBLIC header
#   (esp_event.h types are part of the API).
set(srcs "")
if(CONFIG_E32P_EVENT_ENABLE)
    list(APPEND srcs "src/e32p_event.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core" "esp_event"
)
