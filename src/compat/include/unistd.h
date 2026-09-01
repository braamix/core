// <unistd.h>. The constants; every call names its b_* (compat/cio.h) or the
// proc/io.h call that answers it.
#pragma once

#include <stddef.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#ifndef BRAAM_COMPAT_BUILDING

// Declared so the diagnostic can name the fix. None of these is defined.
ssize_t read(int fd, void *buf, size_t n) BRAAM_BLOCKS("b_read(fd, buf, n)");
ssize_t write(int fd, const void *buf, size_t n) BRAAM_BLOCKS("b_write(fd, buf, n)");
int close(int fd) BRAAM_BLOCKS("b_close(fd)");
off_t lseek(int fd, off_t off, int whence) BRAAM_BLOCKS("b_lseek(fd, off, whence)");
int ftruncate(int fd, off_t n) BRAAM_BLOCKS("b_ftruncate(fd, n)");
int dup(int fd) BRAAM_BLOCKS("b_dup(fd)");
int isatty(int fd) BRAAM_BLOCKS("b_isatty(fd), or tty_of(fd) for the geometry");
int unlink(const char *path) BRAAM_BLOCKS("b_unlink(path)");
int rmdir(const char *path) BRAAM_BLOCKS("b_rmdir(path)");
int chdir(const char *path) BRAAM_BLOCKS("b_chdir(path)");
char *getcwd(char *buf, size_t n) BRAAM_BLOCKS("b_getcwd(buf, n)");
int access(const char *path, int mode) BRAAM_BLOCKS("b_access(path, mode)");
int symlink(const char *target, const char *path) BRAAM_BLOCKS("b_symlink(target, path)");
ssize_t readlink(const char *path, char *buf, size_t n) BRAAM_BLOCKS("b_readlink(path, buf, n)");

pid_t fork(void) BRAAM_ABSENT("co_await spawn(argv, io) (proc/io.h)");
int pipe(int fd[2]) BRAAM_ABSENT("co_await make_pipe() (proc/io.h)");
int execv(const char *path, char *const argv[]) BRAAM_ABSENT("co_await spawn(argv, io)");
int execvp(const char *file, char *const argv[]) BRAAM_ABSENT("co_await spawn(argv, io)");
int dup2(int from, int to) BRAAM_ABSENT("Spawn takes an fd0/fd1/fd2 triple (Concept.md §4.3)");
pid_t getpid(void) BRAAM_ABSENT("proc_pid() (proc/rt.h)");
unsigned sleep(unsigned s) BRAAM_ABSENT("co_await sleep_for(ms) (proc/io.h)");
void *sbrk(long n) BRAAM_ABSENT("heap_alloc(n) (kernel/alloc.h), or malloc");

#endif // BRAAM_COMPAT_BUILDING
