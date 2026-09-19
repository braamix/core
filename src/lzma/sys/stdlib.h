// What liblzma takes from <stdlib.h>, under names of its own (sys.cpp).
#pragma once

#include <stddef.h>

#define malloc lzma_sys_malloc
#define calloc lzma_sys_calloc
#define free   lzma_sys_free

void *lzma_sys_malloc(size_t n);
void *lzma_sys_calloc(size_t nmemb, size_t size);
void lzma_sys_free(void *p);
