/**
 * @file host_testcase.h
 * @brief Host stand-in for ESP-IDF's TEST_CASE: the same shape, registering each
 *        case into a list that host_main.c runs with the real Unity.
 *
 * Force-included into a component's test file only (gcc -include), so the test
 * source stays exactly as it is on the target.
 */
#pragma once

#include "unity.h"

typedef struct host_case {
    const char       *name;
    void            (*fn)(void);
    int               line;
    struct host_case *next;
} host_case_t;

void host_register(host_case_t *c);

/** @brief ESP-IDF's helper: assert that an esp_err_t is ESP_OK. */
#define TEST_ESP_OK(rc) TEST_ASSERT_EQUAL_HEX32(0, (rc))

#define HC_CAT2(a, b) a##b
#define HC_CAT(a, b)  HC_CAT2(a, b)

#define TEST_CASE(name_, tag_)                                                        \
    static void HC_CAT(hc_fn_, __LINE__)(void);                                       \
    static host_case_t HC_CAT(hc_case_, __LINE__) = {                                 \
        name_, HC_CAT(hc_fn_, __LINE__), __LINE__, 0 };                               \
    __attribute__((constructor)) static void HC_CAT(hc_reg_, __LINE__)(void)          \
    {                                                                                 \
        host_register(&HC_CAT(hc_case_, __LINE__));                                   \
    }                                                                                 \
    static void HC_CAT(hc_fn_, __LINE__)(void)
