// What the two derived scanners need in place of musl's stdio: the shgetc
// macros over api.h's Cur, the character classes, and an errno that is a field.
#pragma once

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

#include "api.h"

#define ERANGE 34
#define EINVAL 22

#define shcnt(f)      ((long long)((f)->p - (f)->start))
#define shlim(f, lim) ((void)0)
#define shgetc(f)     ((f)->p < (f)->end ? *(f)->p++ : ((f)->p++, 0))
#define shunget(f)    ((void)(f)->p--)

#define errno (f->err)

// Functions, not macros: the callers pass `c = shgetc(f)`, and a macro would
// evaluate it twice.
static inline int isdigit(int c)
{
	return (unsigned)c - '0' < 10u;
}

static inline int isspace(int c)
{
	return c == ' ' || (unsigned)c - '\t' < 5u;
}

#define ULONG_MAX 0xffffffffuL
