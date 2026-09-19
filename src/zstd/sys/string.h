// What zstd takes from <string.h>: three builtins, and memcmp under a name of
// its own (sys.cpp).
#pragma once

#include <stddef.h>

#define memcpy  __builtin_memcpy
#define memmove __builtin_memmove
#define memset  __builtin_memset

#define memcmp zstd_sys_memcmp

int zstd_sys_memcmp(const void *a, const void *b, size_t n);
