// What liblzma takes from <string.h>: three builtins, and three under names of
// its own (sys.cpp).
#pragma once

#include <stddef.h>

#define memcpy  __builtin_memcpy
#define memmove __builtin_memmove
#define memset  __builtin_memset

#define memcmp lzma_sys_memcmp
#define memchr lzma_sys_memchr
#define strlen lzma_sys_strlen

int lzma_sys_memcmp(const void *a, const void *b, size_t n);
void *lzma_sys_memchr(const void *s, int c, size_t n);
size_t lzma_sys_strlen(const char *s);
