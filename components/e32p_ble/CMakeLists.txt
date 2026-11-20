# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core   -> only e32p types are in the public header.
#   PRIV_REQUIRES bt     -> NimBLE headers. Required unconditionally: component
#     REQUIRES are resolved before CONFIG_* is reliably available, so this can't
#     be gated on CONFIG_BT_NIMBLE_ENABLED. It's harmless when Bluetooth is off
#     (the bt component then compiles to nothing), and the .c only #includes the
#     NimBLE headers under CONFIG_BT_NIMBLE_ENABLED anyway.
set(srcs "")
if(CONFIG_E32P_BLE_ENABLE)
    list(APPEND srcs "src/e32p_ble.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "bt"
)
