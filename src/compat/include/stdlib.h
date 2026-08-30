// <stdlib.h>. Group A throughout: nothing here blocks.
//
// exit() traps — a coroutine cannot exit through a return. Give proc_main a
// status instead. setenv/putenv/unsetenv are absent by design (doc/TODO.md).
#pragma once

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX     0x7fffffff

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t n);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *p, size_t n);
void *reallocarray(void *p, size_t nmemb, size_t size);
void free(void *p);

long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
double atof(const char *s);

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
// GNU argument order: cmp(a, b, arg). BSD's differs.
void qsort_r(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *, void *),
             void *arg);
void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *));

int abs(int v);
long labs(long v);
long long llabs(long long v);

char *getenv(const char *name);

__attribute__((noreturn)) void abort(void);
__attribute__((noreturn)) void exit(int status);

#ifdef __cplusplus
}
#endif
