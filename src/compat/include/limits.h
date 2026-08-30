// A wrapper, not a replacement: clang ships a freestanding <limits.h> and its
// INT_MAX/LONG_MAX/CHAR_BIT are right by derivation. Only the POSIX names that
// a hosted <limits.h> would add are ours.
#pragma once

#if __has_include_next(<limits.h>)
#include_next <limits.h>
#else
#define CHAR_BIT  8
#define SCHAR_MAX 127
#define SCHAR_MIN (-128)
#define UCHAR_MAX 255
#define SHRT_MAX  32767
#define SHRT_MIN  (-32768)
#define USHRT_MAX 65535
#define INT_MAX   2147483647
#define INT_MIN   (-INT_MAX - 1)
#define UINT_MAX  4294967295U
#define LONG_MAX  2147483647L
#define LONG_MIN  (-LONG_MAX - 1L)
#define ULONG_MAX 4294967295UL
#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX 18446744073709551615ULL
#endif

// PATH_MAX is FS_BLOCK, FILE_BUF and the allocator's top small size class, so
// a char[PATH_MAX] is exactly one block. The number is not the constraint —
// the placement is: put one in a heap block, never in a coroutine frame.
#define PATH_MAX 512
#define NAME_MAX 255
#define ARG_MAX  4096
#define OPEN_MAX 256
#define IOV_MAX  16
#define LINE_MAX 2048

// clang's freestanding <limits.h> already says 1, which is a C-locale answer
// this system contradicts: everything here is UTF-8.
#undef MB_LEN_MAX
#define MB_LEN_MAX 4
