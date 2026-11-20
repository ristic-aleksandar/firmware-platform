# Cherry-pick by conditional SRCS; requirements stay unconditional.
#   REQUIRES e32p_core            -> only e32p types are in the public header.
#   PRIV_REQUIRES (esp OTA stack) -> used only in the .c, so none of the esp_ota /
#                                    esp_https_ota API leaks to consumers. mbedtls gives
#                                    esp_crt_bundle.h (the CA bundle for https).
set(srcs "")
if(CONFIG_E32P_OTA_ENABLE)
    list(APPEND srcs "src/e32p_ota.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES "e32p_core"
    PRIV_REQUIRES "app_update" "esp_https_ota" "esp_http_client" "esp_partition" "esp_app_format" "mbedtls"
)
