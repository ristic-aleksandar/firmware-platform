# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core -> in our public header.
#   PRIV_REQUIRES (all used only in the .c):
#     e32p_nvs, e32p_event -> platform deps brought up by e32p_wifi_init
#     esp_wifi, esp_netif   -> the Wi-Fi driver + network interface
set(srcs "")
if(CONFIG_E32P_WIFI_ENABLE)
    list(APPEND srcs "src/e32p_wifi.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "e32p_nvs" "e32p_event" "esp_wifi" "esp_netif"
)
