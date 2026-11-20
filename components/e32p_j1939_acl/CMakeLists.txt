# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core, e32p_j1939 -> both appear in our public header
#     (e32p_j1939_acl.h includes e32p_j1939.h for the message type + PGN macros).
# The engine emits frames through an injected sink, so it does not call the
# j1939/twai send path itself — no PRIV_REQUIRES needed.
set(srcs "")
if(CONFIG_E32P_J1939_ACL_ENABLE)
    list(APPEND srcs "src/e32p_j1939_acl.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core" "e32p_j1939"
)
