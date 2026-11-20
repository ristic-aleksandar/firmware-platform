# e32p_core — platform foundation. Always compiled; every module REQUIRES it.
idf_component_register(
    SRCS "src/e32p_core.c"
    INCLUDE_DIRS "include"
    # esp_err/esp_check/esp_log/soc are part of IDF's common requirements,
    # so no explicit REQUIRES are needed here.
)
