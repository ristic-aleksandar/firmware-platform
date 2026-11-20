# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core      -> error helpers appear in our public header.
#   PRIV_REQUIRES e32p_isotp -> the bus glue (e32p_uds_isotp.c).
#   PRIV_REQUIRES e32p_ota, app_update -> the reflash adapter (e32p_uds_ota.c), built
#     only with CONFIG_E32P_OTA_ENABLE. Requiring them always costs build time, not
#     binary size: nothing of the OTA stack is linked unless the adapter is.
set(srcs "")
if(CONFIG_E32P_UDS_ENABLE)
    list(APPEND srcs "src/e32p_uds.c" "src/e32p_uds_security.c" "src/e32p_uds_transfer.c"
                     "src/e32p_uds_dtc.c" "src/e32p_uds_routine.c" "src/e32p_uds_isotp.c")
    if(CONFIG_E32P_OTA_ENABLE)
        list(APPEND srcs "src/e32p_uds_ota.c")
    endif()
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "e32p_isotp" "e32p_ota" "app_update" "esp_timer"
)
