// <stdio.h>. Only the buffer half is here: everything that touches a stream
// blocks, and a C signature cannot block on this system.
//
// The blocking names are declared `unavailable`, so the compiler names the
// replacement at the exact line rather than leaving a porter to find them.
// Include "compat/cio.h" for the b_* family.
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <sys/cdefs.h>

#ifndef EOF
#define EOF (-1)
#endif
#define BUFSIZ       512
#define FILENAME_MAX 512
#define SEEK_SET     0
#define SEEK_CUR     1
#define SEEK_END     2

// b_setvbuf's mode, which is Buffering (proc/file.h) under C's names.
#ifndef _IOFBF
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#endif

#ifdef __cplusplus
extern "C" {
#endif

int snprintf(char *buf, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vsprintf(char *buf, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#ifndef BRAAM_COMPAT_BUILDING

// Declared so the diagnostic can name the fix. None of these is defined.
typedef struct CompatFile FILE;

// compat/cio.h makes these three macros over b_stdin() and its two siblings,
// which build on first use; this declaration is what the diagnostics name when
// it has not.
#ifndef stdin
extern FILE *stdin, *stdout, *stderr;
#endif

// Every conversion the ports used has a function of its own in kernel/text.h.
int sscanf(const char *s, const char *fmt, ...)
    BRAAM_ABSENT("scan with scan_i64/scan_u64/scan_token/scan_until (kernel/text.h) "
                 "or File::scan_* (proc/file.h)");
int vsscanf(const char *s, const char *fmt, va_list ap)
    BRAAM_ABSENT("scan with scan_i64/scan_u64/scan_token/scan_until (kernel/text.h) "
                 "or File::scan_* (proc/file.h)");

int fgetc(FILE *f) BRAAM_BLOCKS("b_fgetc(f)");
int getc(FILE *f) BRAAM_BLOCKS("b_fgetc(f)");
int getchar(void) BRAAM_BLOCKS("b_fgetc(stdin)");
char *fgets(char *s, int n, FILE *f) BRAAM_BLOCKS("b_fgets(s, n, f)");
int fputc(int c, FILE *f) BRAAM_BLOCKS("b_fputc(c, f)");
int putc(int c, FILE *f) BRAAM_BLOCKS("b_fputc(c, f)");
int putchar(int c) BRAAM_BLOCKS("b_fputc(c, stdout)");
int fputs(const char *s, FILE *f) BRAAM_BLOCKS("b_fputs(s, f)");
int puts(const char *s) BRAAM_BLOCKS("b_puts(s)");
size_t fread(void *p, size_t sz, size_t n, FILE *f) BRAAM_BLOCKS("b_fread(p, sz, n, f)");
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) BRAAM_BLOCKS("b_fwrite(p, sz, n, f)");
FILE *fopen(const char *path, const char *mode) BRAAM_BLOCKS("b_fopen(path, mode)");
FILE *fdopen(int fd, const char *mode) BRAAM_BLOCKS("b_fdopen(fd, mode)");
FILE *freopen(const char *path, const char *mode, FILE *f)
    BRAAM_BLOCKS("b_freopen(path, mode, f)");
int fclose(FILE *f) BRAAM_BLOCKS("b_fclose(f)");
int fflush(FILE *f) BRAAM_BLOCKS("b_fflush(f)");
int fseek(FILE *f, long off, int whence) BRAAM_BLOCKS("b_fseek(f, off, whence)");
int fseeko(FILE *f, long long off, int whence) BRAAM_BLOCKS("b_fseeko(f, off, whence)");
long ftell(FILE *f) BRAAM_BLOCKS("b_ftell(f)");
long long ftello(FILE *f) BRAAM_BLOCKS("b_ftello(f)");
void rewind(FILE *f) BRAAM_BLOCKS("b_rewind(f)");
int printf(const char *fmt, ...) BRAAM_BLOCKS("b_printf(fmt, ...)");
int fprintf(FILE *f, const char *fmt, ...) BRAAM_BLOCKS("b_fprintf(f, fmt, ...)");
int vfprintf(FILE *f, const char *fmt, va_list ap) BRAAM_BLOCKS("b_vfprintf(f, fmt, ap)");
int vprintf(const char *fmt, va_list ap) BRAAM_BLOCKS("b_vfprintf(stdout, fmt, ap)");
int remove(const char *path) BRAAM_BLOCKS("b_remove(path)");
int rename(const char *from, const char *to) BRAAM_BLOCKS("b_rename(from, to)");
void perror(const char *s) BRAAM_BLOCKS("b_perror(s)");

// The buffer answers these without a syscall, so they keep C's shape and only
// the name moves.
int ungetc(int c, FILE *f) BRAAM_RENAMED("b_ungetc(c, f)");
int feof(FILE *f) BRAAM_RENAMED("b_feof(f)");
int ferror(FILE *f) BRAAM_RENAMED("b_ferror(f)");
void clearerr(FILE *f) BRAAM_RENAMED("b_clearerr(f)");
int fileno(FILE *f) BRAAM_RENAMED("b_fileno(f)");
int setvbuf(FILE *f, char *buf, int mode, size_t size) BRAAM_RENAMED("b_setvbuf(f, mode, size)");
void setbuf(FILE *f, char *buf) BRAAM_RENAMED("b_setvbuf(f, mode, size)");

int fscanf(FILE *f, const char *fmt, ...)
    BRAAM_ABSENT("scan with File::scan_i64/scan_u64/scan_token/scan_until (proc/file.h)");
FILE *tmpfile(void) BRAAM_ABSENT("there is no temporary directory: name a file and b_unlink it");
char *tmpnam(char *s) BRAAM_ABSENT("there is no temporary directory");

#endif // BRAAM_COMPAT_BUILDING
