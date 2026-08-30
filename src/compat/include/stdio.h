// <stdio.h>. Only the buffer half is here: everything that touches a stream
// blocks, and a C signature cannot block on this system.
//
// The blocking names are declared `unavailable`, so the compiler names the
// replacement at the exact line rather than leaving a porter to find them.
// Include "compat/cio.h" for the b_* family.
#pragma once

#include <stdarg.h>
#include <stddef.h>

#define EOF          (-1)
#define BUFSIZ       512
#define FILENAME_MAX 512
#define SEEK_SET     0
#define SEEK_CUR     1
#define SEEK_END     2

#ifdef __cplusplus
extern "C" {
#endif

int snprintf(char *buf, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vsprintf(char *buf, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...) __attribute__((format(scanf, 2, 3)));
int vsscanf(const char *s, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#ifndef BRAAM_COMPAT_BUILDING

#define BRAAM_BLOCKS(what)                                                                    \
    __attribute__((unavailable("blocking: co_await " what " from compat/cio.h — "             \
                               "doc/Compat.md §4")))

// Declared so the diagnostic can name the fix. None of these is defined.
typedef struct CompatFile FILE;
extern FILE *stdin, *stdout, *stderr;

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
FILE *fopen(const char *path, const char *mode) BRAAM_BLOCKS("b_fopen(&f, path, mode)");
int fclose(FILE *f) BRAAM_BLOCKS("b_fclose(f)");
int fflush(FILE *f) BRAAM_BLOCKS("b_fflush(f)");
int fseek(FILE *f, long off, int whence) BRAAM_BLOCKS("b_fseek(f, off, whence)");
long ftell(FILE *f) BRAAM_BLOCKS("b_ftell(f)");
int printf(const char *fmt, ...) BRAAM_BLOCKS("b_printf(fmt, ...)");
int fprintf(FILE *f, const char *fmt, ...) BRAAM_BLOCKS("b_fprintf(f, fmt, ...)");
int remove(const char *path) BRAAM_BLOCKS("b_remove(path)");
int rename(const char *from, const char *to) BRAAM_BLOCKS("b_rename(from, to)");
void perror(const char *s) BRAAM_BLOCKS("b_perror(s)");

#endif // BRAAM_COMPAT_BUILDING
