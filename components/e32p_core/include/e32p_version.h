#pragma once
/**
 * @file e32p_version.h
 * @brief Platform version (semantic).
 */
#define E32P_VERSION_MAJOR  0
#define E32P_VERSION_MINOR  1
#define E32P_VERSION_PATCH  0
#define E32P_VERSION_STR    "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the platform version string.
 * @return Pointer to a static "MAJOR.MINOR.PATCH" string.
 */
const char *e32p_version_string(void);

#ifdef __cplusplus
}
#endif
