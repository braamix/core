// <strings.h> — the BSD half, which BSD's own <string.h> also pulls in.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void bzero(void *d, size_t n);
void bcopy(const void *s, void *d, size_t n);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *strcasestr(const char *h, const char *n);

#ifdef __cplusplus
}
#endif
