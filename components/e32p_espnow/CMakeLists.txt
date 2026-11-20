# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES e32p_wifi (brings up the radio) + esp_wifi (esp_now lives in the
#     esp_wifi component). Used only in the .c.
# Kconfig `select E32P_WIFI_ENABLE` forces the e32p_wifi source to compile.
set(srcs "")
if(CONFIG_E32P_ESPNOW_ENABLE)
    list(APPEND srcs "src/e32p_espnow.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "e32p_wifi" "esp_wifi"
)
