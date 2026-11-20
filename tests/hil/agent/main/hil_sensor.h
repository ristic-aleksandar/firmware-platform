#pragma once
/**
 * @file hil_sensor.h
 * @brief Device-driver HIL tests (need the actual part wired):
 *        BMP280 over I2C and a TM1637 display over 2 GPIOs.
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_sensor_bmp280(char *detail, size_t n);
hil_verdict_t hil_sensor_tm1637(char *detail, size_t n);

#ifdef __cplusplus
}
#endif
