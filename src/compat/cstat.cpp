// Group B's stat half. There are no permissions, no owners and no links here,
// so most of what a struct stat carries is a constant; cmode.cpp holds the two
// fields that are a decision.
#define BRAAM_COMPAT_BUILDING 1

#include <string.h>

#include "cerr.h"
#include "cio.h"
#include "cmode.h"

namespace {

void fill(struct stat *st, const FileInfo &fi, Str path)
{
    st->st_mode    = stat_mode(fi.kind);
    st->st_size    = off_t(fi.size);
    st->st_mtime   = time_t(fi.mtime / 1000);
    st->st_ino     = path.empty() ? 0 : ino_t(stat_ino(path));
    st->st_dev     = 1;
    st->st_nlink   = 1;
    st->st_uid     = 0;
    st->st_gid     = 0;
    st->st_atime   = st->st_mtime;
    st->st_ctime   = st->st_mtime;
    st->st_blksize = 512; // FS_BLOCK
    st->st_blocks  = blkcnt_t((fi.size + 511) / 512);
}

Task<int> stat_at(const char *path, struct stat *st, bool follow)
{
    if (!path || !st)
        co_return fail_with(Error::Invalid);

    Str p              = Str(path, strlen(path));
    Result<FileInfo> r = Err(Error::NoMemory);
    if (Task<Result<FileInfo>> t = stat_of(p, follow))
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error());

    fill(st, r.value(), p);
    co_return 0;
}

} // namespace

Task<int> b_stat(const char *path, struct stat *st)
{
    if (Task<int> t = stat_at(path, st, true))
        co_return co_await t;
    co_return fail_with(Error::NoMemory);
}

Task<int> b_lstat(const char *path, struct stat *st)
{
    if (Task<int> t = stat_at(path, st, false))
        co_return co_await t;
    co_return fail_with(Error::NoMemory);
}

Task<int> b_fstat(int fd, struct stat *st)
{
    if (fd < 0 || !st)
        co_return fail_with(Error::Invalid);

    Result<FileInfo> r = Err(Error::NoMemory);
    if (Task<Result<FileInfo>> t = stat_fd(u32(fd)))
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error());

    fill(st, r.value(), Str());
    co_return 0;
}
