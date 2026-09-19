// What zstd takes from <stdlib.h>, under names of its own (sys.cpp).
#pragma once

#include <stddef.h>

#define malloc zstd_sys_malloc
#define calloc zstd_sys_calloc
#define free   zstd_sys_free

void *zstd_sys_malloc(size_t n);
void *zstd_sys_calloc(size_t nmemb, size_t size);
void zstd_sys_free(void *p);
