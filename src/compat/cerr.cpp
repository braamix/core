#include "cerr.h"

#include <errno.h>
#include <string.h>

extern "C" {
int errno;
}

int errno_of(Error e)
{
    switch (e) {
    case Error::Invalid:     return EINVAL;
    case Error::NoMemory:    return ENOMEM;
    case Error::NotFound:    return ENOENT;
    case Error::Exists:      return EEXIST;
    case Error::NotDir:      return ENOTDIR;
    case Error::IsDir:       return EISDIR;
    case Error::Perm:        return EACCES;
    case Error::Io:          return EIO;
    // Both mean "abandoned by a signal"; whether the process survives is not
    // expressible in errno.
    case Error::Cancelled:   return EINTR;
    case Error::Intr:        return EINTR;
    case Error::Again:       return EAGAIN;
    case Error::Unsupported: return ENOSYS;
    case Error::Closed:      return EPIPE;
    case Error::NotEmpty:    return ENOTEMPTY;
    case Error::Loop:        return ELOOP;
    }
    return EIO;
}

Error error_of(int e)
{
    switch (e) {
    case EINVAL:      return Error::Invalid;
    case ENOMEM:      return Error::NoMemory;
    case ENOENT:      return Error::NotFound;
    case EEXIST:      return Error::Exists;
    case ENOTDIR:     return Error::NotDir;
    case EISDIR:      return Error::IsDir;
    case EPERM:
    case EACCES:      return Error::Perm;
    case EINTR:       return Error::Intr;
    case EAGAIN:      return Error::Again;
    case ENOSYS:      return Error::Unsupported;
    case EPIPE:       return Error::Closed;
    case ENOTEMPTY:   return Error::NotEmpty;
    case ELOOP:       return Error::Loop;
    }
    return Error::Io;
}

int fail_with(Error e)
{
    errno = errno_of(e);
    return -1;
}

namespace {

struct Named {
    int e;
    const char *name;
};

// The POSIX name, mirroring error_name() in kernel/result.h. Every byte of
// English prose a Unix libc spends here stays unspent.
constexpr Named NAMES[] = {
    { EPERM, "EPERM" },           { ENOENT, "ENOENT" },     { ESRCH, "ESRCH" },
    { EINTR, "EINTR" },           { EIO, "EIO" },           { ENXIO, "ENXIO" },
    { E2BIG, "E2BIG" },           { ENOEXEC, "ENOEXEC" },   { EBADF, "EBADF" },
    { ECHILD, "ECHILD" },         { EAGAIN, "EAGAIN" },     { ENOMEM, "ENOMEM" },
    { EACCES, "EACCES" },         { EFAULT, "EFAULT" },     { EBUSY, "EBUSY" },
    { EEXIST, "EEXIST" },         { EXDEV, "EXDEV" },       { ENODEV, "ENODEV" },
    { ENOTDIR, "ENOTDIR" },       { EISDIR, "EISDIR" },     { EINVAL, "EINVAL" },
    { ENFILE, "ENFILE" },         { EMFILE, "EMFILE" },     { ENOTTY, "ENOTTY" },
    { EFBIG, "EFBIG" },           { ENOSPC, "ENOSPC" },     { ESPIPE, "ESPIPE" },
    { EROFS, "EROFS" },           { EMLINK, "EMLINK" },     { EPIPE, "EPIPE" },
    { EDOM, "EDOM" },             { ERANGE, "ERANGE" },     { ENOSYS, "ENOSYS" },
    { ENAMETOOLONG, "ENAMETOOLONG" },
    { ENOTEMPTY, "ENOTEMPTY" },   { ELOOP, "ELOOP" },       { EOVERFLOW, "EOVERFLOW" },
    { EILSEQ, "EILSEQ" },
};

} // namespace

extern "C" {

char *strerror(int e)
{
    for (const Named &n : NAMES)
        if (n.e == e)
            return const_cast<char *>(n.name);
    return const_cast<char *>("EIO");
}

int strerror_r(int e, char *buf, size_t n)
{
    const char *s = strerror(e);
    size_t len    = strlen(s);
    if (n == 0)
        return ERANGE;
    size_t fit = len < n - 1 ? len : n - 1;
    memcpy(buf, s, fit);
    buf[fit] = '\0';
    return len < n ? 0 : ERANGE;
}

} // extern "C"
