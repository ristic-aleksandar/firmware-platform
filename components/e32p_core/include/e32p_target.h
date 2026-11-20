#pragma once
/**
 * @file e32p_target.h
 * @brief Portability seam. Centralizes target-conditional facts so leaf modules
 *        never scatter #ifdef CONFIG_IDF_TARGET_* . Modules gate chip features on
 *        SOC_* capability macros (soc/soc_caps.h) and `depends on SOC_*` in Kconfig.
 */
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if CONFIG_IDF_TARGET_ESP32
#  define E32P_TARGET_NAME "esp32"
#else
#  define E32P_TARGET_NAME CONFIG_IDF_TARGET   /* string set by IDF for other targets */
#endif
