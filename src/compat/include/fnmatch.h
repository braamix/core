// <fnmatch.h>. Not sh's glob_match: that one is braam_sh's and takes the
// expander's quoting mask, where fnmatch takes flags and a backslash.
#pragma once

#define FNM_NOMATCH     1
#define FNM_PATHNAME    0x01 // a '/' matches only a '/'
#define FNM_NOESCAPE    0x02 // '\' is an ordinary character
#define FNM_PERIOD      0x04 // a leading '.' matches only a literal one
#define FNM_CASEFOLD    0x08 // BSD's, and glibc's
#define FNM_LEADING_DIR 0x10 // a match may stop at a '/'

#ifdef __cplusplus
extern "C" {
#endif

// 0, or FNM_NOMATCH. Bytes, not runes: a '?' takes one byte.
int fnmatch(const char *pattern, const char *s, int flags);

#ifdef __cplusplus
}
#endif
