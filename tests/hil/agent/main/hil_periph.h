#pragma once
/**
 * @file hil_periph.h
 * @brief GPIO / analog / timer HIL tests (single board, no wiring).
 */
#include "hil_agent.h"

#ifdef __cplusplus
extern "C" {
#endif

hil_verdict_t hil_periph_gpio(char *detail, size_t n);
hil_verdict_t hil_periph_ledc(char *detail, size_t n);
hil_verdict_t hil_periph_adc(char *detail, size_t n);
hil_verdict_t hil_periph_dac(char *detail, size_t n);
hil_verdict_t hil_periph_touch(char *detail, size_t n);
hil_verdict_t hil_periph_pcnt(char *detail, size_t n);
hil_verdict_t hil_periph_gptimer(char *detail, size_t n);

#ifdef __cplusplus
}
#endif
