// <string.h> for a port. Group A: every one of these is pure computation.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *stpcpy(char *d, const char *s);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcoll(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strchrnul(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
size_t strspn(const char *s, const char *set);
size_t strcspn(const char *s, const char *set);
char *strpbrk(const char *s, const char *set);
char *strtok(char *s, const char *sep);
char *strtok_r(char *s, const char *sep, char **save);
char *strsep(char **sp, const char *sep);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
size_t strlcpy(char *d, const char *s, size_t n);
size_t strlcat(char *d, const char *s, size_t n);

// The POSIX name, not the number: doc/Compat.md §3.
char *strerror(int e);
int strerror_r(int e, char *buf, size_t n);

#ifdef __cplusplus
}
#endif

#include <strings.h>
