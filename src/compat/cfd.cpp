// Group B's descriptor half: the POSIX calls over proc/io.h.
#define BRAAM_COMPAT_BUILDING 1

#include <errno.h>
#include <string.h>

#include "cerr.h"
#include "cio.h"

namespace {

Str path_of(const char *p)
{
    return Str(p, strlen(p));
}

// <fcntl.h>'s O_* are sysabi.h's SYS_O_* by construction; this rejects a bit
// that is neither.
bool open_flags(int flags, u32 &out)
{
    if (flags & ~int(SYS_O_ALL))
        return false;
    out = u32(flags) & SYS_O_ALL;
    return (out & (SYS_O_READ | SYS_O_WRITE)) != 0;
}

} // namespace

Task<int> b_open(const char *path, int flags, mode_t)
{
    u32 f = 0;
    if (!path || !open_flags(flags, f))
        co_return fail_with(Error::Invalid);

    Result<i32> r = Err(Error::NoMemory);
    if (Task<Result<i32>> t = open_at(path_of(path), f))
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error());
    co_return r.value();
}

Task<int> b_creat(const char *path, mode_t mode)
{
    if (Task<int> t = b_open(path, O_WRONLY | O_CREAT | O_TRUNC, mode))
        co_return co_await t;
    co_return fail_with(Error::NoMemory);
}

Task<int> b_close(int fd)
{
    if (fd < 0)
        co_return fail_with(Error::Invalid);
    if (Task<void> t = close_fd(u32(fd)))
        co_await t;
    co_return 0;
}

Task<ssize_t> b_read(int fd, void *buf, size_t n)
{
    if (fd < 0 || !buf)
        co_return fail_with(Error::Invalid);
    if (n == 0)
        co_return 0;

    Result<String> r = Err(Error::NoMemory);
    if (Task<Result<String>> t = read_some(u32(fd), u32(n)))
        r = co_await t;
    if (r.is_err()) {
        if (r.error() == Error::Closed)
            co_return 0;
        co_return fail_with(r.error());
    }

    Str got = r.value().str();
    usize k = got.size() < n ? got.size() : n;
    memcpy(buf, got.data(), k);
    co_return ssize_t(k);
}

Task<ssize_t> b_write(int fd, const void *buf, size_t n)
{
    if (fd < 0 || !buf)
        co_return fail_with(Error::Invalid);

    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = write_all(u32(fd), Str(static_cast<const char *>(buf), n)))
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error());
    co_return ssize_t(n);
}

Task<off_t> b_lseek(int fd, off_t off, int whence)
{
    Result<u64> r = Err(Error::NoMemory);
    if (Task<Result<u64>> t = seek_fd(u32(fd), i64(off), u32(whence)))
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error());
    co_return off_t(r.value());
}

Task<int> b_ftruncate(int fd, off_t n)
{
    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = truncate_fd(u32(fd), u64(n)))
        r = co_await t;
    co_return r.is_err() ? fail_with(r.error()) : 0;
}

Task<int> b_dup(int fd)
{
    Result<u32> r = Err(Error::NoMemory);
    if (Task<Result<u32>> t = dup_fd(u32(fd)))
        r = co_await t;
    co_return r.is_err() ? fail_with(r.error()) : int(r.value());
}

Task<int> b_isatty(int fd)
{
    Result<TtyInfo> r = Err(Error::NoMemory);
    if (Task<Result<TtyInfo>> t = tty_of(u32(fd)))
        r = co_await t;
    if (r.is_err()) {
        fail_with(r.error());
        co_return 0;
    }
    co_return r.value().console ? 1 : 0;
}

Task<int> b_unlink(const char *path)
{
    if (!path)
        co_return fail_with(Error::Invalid);
    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = remove_path(path_of(path), false))
        r = co_await t;
    co_return r.is_err() ? fail_with(r.error()) : 0;
}

Task<int> b_rmdir(const char *path)
{
    if (Task<int> t = b_unlink(path))
        co_return co_await t;
    co_return fail_with(Error::NoMemory);
}

Task<int> b_remove(const char *path)
{
    if (Task<int> t = b_unlink(path))
        co_return co_await t;
    co_return fail_with(Error::NoMemory);
}

Task<int> b_mkdir(const char *path, mode_t)
{
    if (!path)
        co_return fail_with(Error::Invalid);
    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = make_dir(path_of(path)))
        r = co_await t;
    co_return r.is_err() ? fail_with(r.error()) : 0;
}

Task<int> b_rename(const char *from, const char *to)
{
    if (!from || !to)
        co_return fail_with(Error::Invalid);
    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = rename_path(path_of(from), path_of(to)))
        r = co_await t;
    co_return r.is_err() ? fail_with(r.error()) : 0;
}

Task<int> b_chdir(const char *path)
{
    if (!path)
        co_return fail_with(Error::Invalid);
    Result<String> r = Err(Error::NoMemory);
    if (Task<Result<String>> t = cwd_set(path_of(path)))
        r = co_await t;
    co_return r.is_err() ? fail_with(r.error()) : 0;
}

Task<char *> b_getcwd(char *buf, size_t n)
{
    if (!buf || n == 0) {
        errno = EINVAL;
        co_return nullptr;
    }
    Result<String> r = Err(Error::NoMemory);
    if (Task<Result<String>> t = cwd_get())
        r = co_await t;
    if (r.is_err()) {
        fail_with(r.error());
        co_return nullptr;
    }

    Str s = r.value().str();
    if (s.size() + 1 > n) {
        errno = ERANGE;
        co_return nullptr;
    }
    memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    co_return buf;
}

Task<int> b_access(const char *path, int)
{
    if (!path)
        co_return fail_with(Error::Invalid);
    Result<FileInfo> r = Err(Error::NoMemory);
    if (Task<Result<FileInfo>> t = stat_of(path_of(path)))
        r = co_await t;
    co_return r.is_err() ? fail_with(r.error()) : 0;
}

Task<int> b_symlink(const char *target, const char *path)
{
    if (!target || !path)
        co_return fail_with(Error::Invalid);
    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = make_link(path_of(target), path_of(path)))
        r = co_await t;
    co_return r.is_err() ? fail_with(r.error()) : 0;
}

Task<ssize_t> b_readlink(const char *path, char *buf, size_t n)
{
    if (!path || !buf || n == 0)
        co_return fail_with(Error::Invalid);
    Result<String> r = Err(Error::NoMemory);
    if (Task<Result<String>> t = read_link(path_of(path)))
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error());

    // Not terminated, as readlink(2) does not.
    Str s   = r.value().str();
    usize k = s.size() < n ? s.size() : n;
    memcpy(buf, s.data(), k);
    co_return ssize_t(k);
}
