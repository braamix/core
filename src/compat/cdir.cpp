// Group B's directory half. Sys::List answers with the whole listing, so the
// syscall is b_opendir's and the walk after it does not block.
#define BRAAM_COMPAT_BUILDING 1

#include <errno.h>
#include <string.h>

#include "cerr.h"
#include "cio.h"
#include "cmode.h"
#include "kernel/alloc.h"

Task<DIR *> b_opendir(const char *path)
{
    if (!path) {
        errno = EINVAL;
        co_return nullptr;
    }

    Str p                   = Str(path, strlen(path));
    Result<Vec<DirEntry>> r = Err(Error::NoMemory);
    if (Task<Result<Vec<DirEntry>>> t = list_dir(p))
        r = co_await t;
    if (r.is_err()) {
        fail_with(r.error());
        co_return nullptr;
    }

    DIR *d = heap_new<CompatDir>();
    if (!d) {
        errno = ENOMEM;
        co_return nullptr;
    }
    d->ents = move(r.value());
    co_return d;
}

struct dirent *b_readdir(DIR *d)
{
    if (!d || d->at >= d->ents.size())
        return nullptr;

    const DirEntry &e = d->ents[d->at++];
    Str n             = e.name.str();
    usize k           = n.size() < NAME_MAX ? n.size() : NAME_MAX;

    memcpy(d->cur.d_name, n.data(), k);
    d->cur.d_name[k] = '\0';
    d->cur.d_ino     = ino_t(stat_ino(n));
    d->cur.d_type    = e.kind == SYS_KIND_DIR ? DT_DIR : e.kind == SYS_KIND_LINK ? DT_LNK : DT_REG;
    return &d->cur;
}

int b_closedir(DIR *d)
{
    if (!d)
        return fail_with(Error::Invalid);
    heap_delete(d);
    return 0;
}

void b_rewinddir(DIR *d)
{
    if (d)
        d->at = 0;
}

long b_telldir(DIR *d)
{
    return d ? long(d->at) : -1;
}

void b_seekdir(DIR *d, long at)
{
    if (d && at >= 0 && usize(at) <= d->ents.size())
        d->at = usize(at);
}
