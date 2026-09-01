// <fcntl.h>. The flag words, mapped onto sysabi.h's SYS_O_*; open and creat
// name their b_* (compat/cio.h).
#pragma once

#include <sys/cdefs.h>
#include <sys/types.h>

#define O_RDONLY 1  // SYS_O_READ
#define O_WRONLY 2  // SYS_O_WRITE
#define O_RDWR   3  // both
#define O_CREAT  4  // SYS_O_CREATE
#define O_TRUNC  8  // SYS_O_TRUNC
#define O_APPEND 16 // SYS_O_APPEND
#define O_EXCL   32 // SYS_O_EXCL

// Everything is UTF-8 and every read is bytes, so there is no text mode to
// leave. Accepted and ignored.
#define O_BINARY 0
#define O_TEXT   0

#ifndef BRAAM_COMPAT_BUILDING

// Declared so the diagnostic can name the fix. None of these is defined.
int open(const char *path, int flags, ...) BRAAM_BLOCKS("b_open(path, flags, mode)");
int creat(const char *path, mode_t mode) BRAAM_BLOCKS("b_creat(path, mode)");

// No O_NONBLOCK -- everything is a coroutine; no FD_CLOEXEC -- a spawn moves
// a descriptor rather than duplicating it (Concept.md §4.3).
int fcntl(int fd, int cmd, ...) BRAAM_ABSENT("nothing to set: see doc/TODO.md");

#endif // BRAAM_COMPAT_BUILDING
