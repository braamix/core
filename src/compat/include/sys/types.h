// <sys/types.h>. The typedefs the file headers below it need, and nothing
// else: there are no users, no groups and no devices here, so the three that
// name one are a width and a promise to keep quiet.
//
// time_t is <time.h>'s, not repeated here.
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;
typedef long long off_t; // 64-bit, as time_t is: a file outgrows a long
typedef unsigned int mode_t;
typedef unsigned long long ino_t;
typedef unsigned int dev_t;
typedef unsigned int nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int pid_t;
typedef long blksize_t;
typedef long long blkcnt_t;
