// The fixed-width types the vendored sources name. Ours rather than clang's:
// nothing from the compiler's headers is included anywhere in this tree.
#pragma once

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long long uintmax_t;
typedef long long intmax_t;
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int uintptr_t;
typedef int intptr_t;

#define UINT32_MAX 0xffffffffu
#define UINT64_MAX 0xffffffffffffffffull
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define INT64_MIN  (-9223372036854775807ll - 1)
#define INT64_MAX  9223372036854775807ll
