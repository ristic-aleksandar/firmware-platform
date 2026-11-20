# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> error helpers appear in our public header.
#   PRIV_REQUIRES e32p_isotp -> only the transport glue uses it (slice 2.3).
#   PRIV_REQUIRES e32p_nvs -> the DTC store persists codes across a reset.
set(srcs "")
if(CONFIG_E32P_OBD2_ENABLE)
    list(APPEND srcs "src/e32p_obd2.c" "src/e32p_obd2_dtc.c" "src/e32p_obd2_isotp.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "e32p_isotp" "e32p_nvs"
)
