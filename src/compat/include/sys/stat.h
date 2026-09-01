// <sys/stat.h>. The struct and the macros; the four that perform a syscall are
// unavailable and name their b_* (compat/cio.h).
#pragma once

#include <sys/cdefs.h>
#include <sys/types.h>
#include <time.h>

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFLNK  0120000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFIFO  0010000
#define S_IFSOCK 0140000

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)

// Nothing here is ever one of these.
#define S_ISCHR(m)  (((void)(m)), 0)
#define S_ISBLK(m)  (((void)(m)), 0)
#define S_ISFIFO(m) (((void)(m)), 0)
#define S_ISSOCK(m) (((void)(m)), 0)

#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000

#ifdef __cplusplus
extern "C" {
#endif

// st_mode is a kind plus a constant: 0755 a directory, 0777 a link, 0644 a
// file. S_ISDIR, S_ISLNK and S_ISREG are what it answers.
struct stat {
    mode_t st_mode;
    off_t st_size;
    time_t st_mtime;      // seconds; 0 where the store keeps none
    ino_t st_ino;         // a hash of the path
    dev_t st_dev;         // 1
    nlink_t st_nlink;     // 1
    uid_t st_uid;         // 0
    gid_t st_gid;         // 0
    time_t st_atime;      // st_mtime
    time_t st_ctime;      // st_mtime
    blksize_t st_blksize; // FS_BLOCK
    blkcnt_t st_blocks;   // st_size in 512-byte units, rounded up
};

#ifdef __cplusplus
}
#endif

#ifndef BRAAM_COMPAT_BUILDING

// Declared so the diagnostic can name the fix. None of these is defined.
int stat(const char *path, struct stat *st) BRAAM_BLOCKS("b_stat(path, st)");
int lstat(const char *path, struct stat *st) BRAAM_BLOCKS("b_lstat(path, st)");
int fstat(int fd, struct stat *st) BRAAM_BLOCKS("b_fstat(fd, st)");
int mkdir(const char *path, mode_t mode) BRAAM_BLOCKS("b_mkdir(path, mode)");

int chmod(const char *path, mode_t mode) BRAAM_ABSENT("no file permissions");
int fchmod(int fd, mode_t mode) BRAAM_ABSENT("no file permissions");
mode_t umask(mode_t mask) BRAAM_ABSENT("no file permissions");
int mkfifo(const char *path, mode_t mode) BRAAM_ABSENT("no FIFOs; make_pipe() (proc/io.h)");

#endif // BRAAM_COMPAT_BUILDING
