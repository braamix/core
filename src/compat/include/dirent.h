// <dirent.h>. Sys::List answers with the whole listing, so b_opendir performs
// the syscall and the walk after it does not block.
#pragma once

#include <limits.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_REG     8
#define DT_DIR     4
#define DT_LNK     10

#ifdef __cplusplus
extern "C" {
#endif

// A listing holds no "." or "..".
struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[NAME_MAX + 1];
};

#ifdef __cplusplus
}
#endif

typedef struct CompatDir DIR;

#ifndef BRAAM_COMPAT_BUILDING

// Declared so the diagnostic can name the fix. None of these is defined.
DIR *opendir(const char *path) BRAAM_BLOCKS("b_opendir(path)");
struct dirent *readdir(DIR *d) BRAAM_RENAMED("b_readdir(d)");
int closedir(DIR *d) BRAAM_RENAMED("b_closedir(d)");
void rewinddir(DIR *d) BRAAM_RENAMED("b_rewinddir(d)");
long telldir(DIR *d) BRAAM_RENAMED("b_telldir(d)");
void seekdir(DIR *d, long at) BRAAM_RENAMED("b_seekdir(d, at)");
int scandir(const char *path, struct dirent ***out, int (*sel)(const struct dirent *),
            int (*cmp)(const struct dirent **, const struct dirent **))
    BRAAM_ABSENT("b_opendir and a loop: the listing is already in hand");

#endif // BRAAM_COMPAT_BUILDING
