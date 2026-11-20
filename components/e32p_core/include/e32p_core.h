#pragma once
/**
 * @file e32p_core.h
 * @brief ESP32 Platform (e32p) core: umbrella header for shared conventions.
 *
 * Include this from any platform module to pull in the error, version, and
 * target-portability conventions. Modules add their own driver includes.
 */
#include "esp_log.h"     /* guarantee ESP_LOGx to every module that includes e32p_core */
#include "e32p_err.h"
#include "e32p_version.h"
#include "e32p_target.h"
