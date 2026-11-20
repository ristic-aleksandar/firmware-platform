# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES e32p_twai -> used only in the .c (send/receive frames).
# Kconfig `select E32P_TWAI_ENABLE` guarantees the TWAI sources are built.
set(srcs "")
if(CONFIG_E32P_J1939_ENABLE)
    list(APPEND srcs "src/e32p_j1939.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "e32p_twai"
)
