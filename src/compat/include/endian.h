// A wrapper, not a replacement: clang 23 ships a freestanding <endian.h> that
// derives the order from __BYTE_ORDER__ and carries the whole htobe/letoh
// family. Clang 18 ships none, so the same names are derived here from the same
// predefines.
#pragma once

#if __has_include_next(<endian.h>)
#include_next <endian.h>
#else

#include <stdint.h>

#define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#define BIG_ENDIAN    __ORDER_BIG_ENDIAN__
#define PDP_ENDIAN    __ORDER_PDP_ENDIAN__
#define BYTE_ORDER    __BYTE_ORDER__

#define __LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#define __BIG_ENDIAN    __ORDER_BIG_ENDIAN__
#define __PDP_ENDIAN    __ORDER_PDP_ENDIAN__
#define __BYTE_ORDER    __BYTE_ORDER__

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htobe16(x) __builtin_bswap16((uint16_t)(x))
#define htobe32(x) __builtin_bswap32((uint32_t)(x))
#define htobe64(x) __builtin_bswap64((uint64_t)(x))
#define be16toh(x) __builtin_bswap16((uint16_t)(x))
#define be32toh(x) __builtin_bswap32((uint32_t)(x))
#define be64toh(x) __builtin_bswap64((uint64_t)(x))
#define htole16(x) ((uint16_t)(x))
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#define le16toh(x) ((uint16_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define le64toh(x) ((uint64_t)(x))
#else
#define htobe16(x) ((uint16_t)(x))
#define htobe32(x) ((uint32_t)(x))
#define htobe64(x) ((uint64_t)(x))
#define be16toh(x) ((uint16_t)(x))
#define be32toh(x) ((uint32_t)(x))
#define be64toh(x) ((uint64_t)(x))
#define htole16(x) __builtin_bswap16((uint16_t)(x))
#define htole32(x) __builtin_bswap32((uint32_t)(x))
#define htole64(x) __builtin_bswap64((uint64_t)(x))
#define le16toh(x) __builtin_bswap16((uint16_t)(x))
#define le32toh(x) __builtin_bswap32((uint32_t)(x))
#define le64toh(x) __builtin_bswap64((uint64_t)(x))
#endif

#endif
