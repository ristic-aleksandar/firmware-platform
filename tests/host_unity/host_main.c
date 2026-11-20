/**
 * @file host_main.c
 * @brief Runs every TEST_CASE registered through host_testcase.h with the real
 *        Unity, in source order — constructor order differs between toolchains.
 */
#include <stdio.h>
#include <stdlib.h>
#include "unity.h"
#include "host_testcase.h"

#define MAX_CASES 512

static host_case_t *g_cases[MAX_CASES];
static int          g_count;

void host_register(host_case_t *c)
{
    if (g_count < MAX_CASES) {
        g_cases[g_count++] = c;
    }
}

void setUp(void) {}
void tearDown(void) {}

static int by_line(const void *a, const void *b)
{
    return (*(host_case_t *const *)a)->line - (*(host_case_t *const *)b)->line;
}

int main(void)
{
    qsort(g_cases, (size_t)g_count, sizeof(g_cases[0]), by_line);
    UnityBegin("host");
    for (int i = 0; i < g_count; i++) {
        UnityDefaultTestRun(g_cases[i]->fn, g_cases[i]->name, g_cases[i]->line);
    }
    return UnityEnd();
}
