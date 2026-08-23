// The two derived scanners' surface, in C and C++ both. cvt.h is their private
// side; this is what ftoa.cpp includes.
#pragma once

// A cursor over a bounded string, in place of musl's string pseudo-FILE. p may
// run one past end and is never dereferenced there.
typedef struct {
	const unsigned char *p, *start, *end;
	int err; // 0, 34 (ERANGE) or 22 (EINVAL)
} Cur;

// fmt_fp's sink: truncating, so a caller sizes the buffer from the precision it
// asked for and reads the wanted length back out of n.
typedef struct {
	char *buf;
	unsigned long cap, n;
} Sink;

#ifdef __cplusplus
extern "C" {
#endif

double __floatscan(Cur *, int prec, int pok);
int fmt_fp(Sink *, double, int w, int p, int fl, int t);

#ifdef __cplusplus
}
#endif
