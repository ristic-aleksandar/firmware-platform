# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core, e32p_twai -> both appear in our public header
#     (e32p_isotp.h includes e32p_twai.h for e32p_twai_message_t).
# The ENGINE emits frames through an injected sink and calls no driver at all.
# The LINK half (e32p_isotp_twai.c) does drive e32p_twai and needs esp_timer for
# its poll clock - private, since neither appears in a public header.
set(srcs "")
if(CONFIG_E32P_ISOTP_ENABLE)
    list(APPEND srcs "src/e32p_isotp.c" "src/e32p_isotp_twai.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core" "e32p_twai"
    PRIV_REQUIRES "esp_timer"
)
