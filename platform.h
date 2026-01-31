#ifndef __PLATFORM_H
#define __PLATFORM_H

#include <stddef.h>

//
// Platform detection macros
//
#if defined(_WIN32) || defined(_WIN64)
#   define SK_PLATFORM_WINDOWS
#elif defined(__linux__)
#   define SK_PLATFORM_LINUX
#elif defined(__APPLE__) || defined(__MACH__)
#   define SK_PLATFORM_MACOS
#endif

//
// Path constants
//
#ifdef SK_PLATFORM_WINDOWS
#   define SK_PATH_MAX      260  // _MAX_PATH on Windows
#   define SK_PATH_SEP      '\\'
#   define SK_PATH_SEP_STR  "\\"
#else
#   define SK_PATH_MAX      4096  // Standard PATH_MAX on Unix-like systems
#   define SK_PATH_SEP      '/'
#   define SK_PATH_SEP_STR  "/"
#endif

//
// Compiler attributes
//
#ifdef SK_PLATFORM_WINDOWS
#   define SK_UNUSED
#else
#   define SK_UNUSED __attribute__((unused))
#endif

//
// Platform-independent string functions
//
int sk_strcasecmp(const char* s1, const char* s2);
char* sk_strdup(const char* s);

//
// Platform-independent file path functions
//
char* sk_getcwd(char* buf, size_t size);

#endif // __PLATFORM_H
