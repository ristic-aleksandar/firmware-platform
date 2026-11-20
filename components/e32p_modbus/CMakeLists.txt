# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core, e32p_uart -> both appear in the public header
#     (e32p_modbus.h includes e32p_uart.h for the parity/stop-bit enums).
set(srcs "")
if(CONFIG_E32P_MODBUS_ENABLE)
    list(APPEND srcs "src/e32p_modbus.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core" "e32p_uart"
)
