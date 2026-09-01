// Group B: streams, descriptors and directories. A C signature cannot block
// here, so each keeps C's name, its arguments and its error convention, and
// gains a b_ prefix and an awaitable return.
//
//     if ((c = fgetc(f)) == EOF)   ->   if ((c = co_await b_fgetc(f)) == EOF)
//
// Every failure is C's own -- -1, 0, EOF or null -- with errno set through
// compat/cerr.h. An end of input is not a failure and never reaches errno.
//
// This header does not include <stdio.h>: a port may have a printf of its own
// (doc/Compat.md §4).
#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kernel/vec.h"
#include "proc/file.h"
#include "proc/io.h"

#ifndef EOF
#define EOF (-1)
#endif

#ifndef _IOFBF
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#endif

typedef struct CompatFile FILE;

// A File, plus the two bytes stdio owes a caller: one of pushback, and one for
// b_fgetc and b_fputc to read and write through without a buffer of their own.
struct CompatFile {
    explicit CompatFile(File &&g) : own(move(g)), at(&own), owned(true) {}
    explicit CompatFile(File &g) : own(File::of(0, FileMode::Read)), at(&g) {}

    File own;
    File *at;
    int back   = EOF; // b_ungetc's byte
    char one   = 0;
    bool owned = false;
};

// ------------------------------------------------------------------ streams

// Built on first use, as File::stdin() and its two siblings are.
FILE *b_stdin();
FILE *b_stdout();
FILE *b_stderr();

#ifndef BRAAM_COMPAT_BUILDING
#define stdin  b_stdin()
#define stdout b_stdout()
#define stderr b_stderr()
#endif

// Null with errno set when the open failed or the mode string was not one.
Task<FILE *> b_fopen(const char *path, const char *mode);

// b_fclose closes the descriptor, as POSIX's fdopen has it.
Task<FILE *> b_fdopen(int fd, const char *mode);

// The same FILE, reopened: a caller holding `stdout` still holds it.
Task<FILE *> b_freopen(const char *path, const char *mode, FILE *f);

// Flushes, closes what this FILE opened, frees it. 0, or EOF.
Task<int> b_fclose(FILE *f);

Task<int> b_fflush(FILE *f);

// One byte, or EOF at end of input and on error -- b_ferror tells them apart.
// An awaiter and not a Task: a byte already in the buffer must not cost a
// coroutine frame.
struct BFgetc : FileRead {
    CompatFile *cf = nullptr;

    bool await_ready();
    int await_resume() const;
};

BFgetc b_fgetc(FILE *f);

// One byte, encoded as itself: File::put is a rune.
struct BFputc : FileWrite {
    CompatFile *cf = nullptr;

    int await_resume() const;
};

BFputc b_fputc(int c, FILE *f);

// One byte of pushback, which is all C promises. The byte, or EOF.
int b_ungetc(int c, FILE *f);

// Up to n-1 bytes, stopping after a newline, which is kept. `s`, or null at
// end of input with nothing read.
Task<char *> b_fgets(char *s, int n, FILE *f);

Task<int> b_fputs(const char *s, FILE *f);
Task<int> b_puts(const char *s);

// Whole items, as C counts them. Short only at end of input or on error.
Task<size_t> b_fread(void *p, size_t size, size_t n, FILE *f);
Task<size_t> b_fwrite(const void *p, size_t size, size_t n, FILE *f);

// Clears the end-of-input indicator and the pushback, as C requires.
Task<int> b_fseek(FILE *f, long off, int whence);
Task<long> b_ftell(FILE *f);
Task<void> b_rewind(FILE *f);

Task<int> b_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
Task<int> b_fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
Task<int> b_vfprintf(FILE *f, const char *fmt, va_list ap);

// "who: ERRNAME" on stderr.
Task<void> b_perror(const char *s);

int b_feof(FILE *f);
int b_ferror(FILE *f);
void b_clearerr(FILE *f);
int b_fileno(FILE *f);

// _IOFBF, _IOLBF or _IONBF, and a buffer size the stream asks the heap for.
// 0, or -1.
int b_setvbuf(FILE *f, int mode, size_t size);

// -------------------------------------------------------------- descriptors

// `flags` is <fcntl.h>'s O_*. The descriptor, or -1.
Task<int> b_open(const char *path, int flags, mode_t mode = 0666);
Task<int> b_creat(const char *path, mode_t mode = 0666);
Task<int> b_close(int fd);

// As many bytes as are there; 0 at end of input.
Task<ssize_t> b_read(int fd, void *buf, size_t n);

// All of them or none: a short write is not representable here.
Task<ssize_t> b_write(int fd, const void *buf, size_t n);

Task<off_t> b_lseek(int fd, off_t off, int whence);
Task<int> b_ftruncate(int fd, off_t n);
Task<int> b_dup(int fd);

// Whether the descriptor is the console. tty_of (proc/io.h) also answers the
// geometry, which this drops.
Task<int> b_isatty(int fd);

Task<int> b_unlink(const char *path);
// b_unlink, both of them: the store has one removal, and it refuses a
// directory with children.
Task<int> b_rmdir(const char *path);
Task<int> b_mkdir(const char *path, mode_t mode = 0777);
Task<int> b_remove(const char *path);
Task<int> b_rename(const char *from, const char *to);
Task<int> b_chdir(const char *path);
Task<char *> b_getcwd(char *buf, size_t n);

// Existence, whatever `mode` asks: there are no file permissions.
Task<int> b_access(const char *path, int mode);

Task<int> b_symlink(const char *target, const char *path);
Task<ssize_t> b_readlink(const char *path, char *buf, size_t n);

// ---------------------------------------------------------- stat and dirent

Task<int> b_stat(const char *path, struct stat *st);
Task<int> b_lstat(const char *path, struct stat *st);

// st_ino is 0 and st_mtime is 0: a descriptor has no name to hash and the
// store stamps a path, not an open file.
Task<int> b_fstat(int fd, struct stat *st);

// The whole listing, taken in one syscall; the walk after it does not block.
struct CompatDir {
    Vec<DirEntry> ents;
    usize at = 0;
    struct dirent cur;
};

typedef struct CompatDir DIR;

Task<DIR *> b_opendir(const char *path);

struct dirent *b_readdir(DIR *d);
int b_closedir(DIR *d);
void b_rewinddir(DIR *d);
long b_telldir(DIR *d);
void b_seekdir(DIR *d, long at);
