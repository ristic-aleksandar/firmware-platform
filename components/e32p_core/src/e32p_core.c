/**
 * @file e32p_core.c
 * @brief e32p_core implementation (currently: version accessor).
 */
#include "e32p_version.h"

const char *e32p_version_string(void)
{
    return E32P_VERSION_STR;
}
