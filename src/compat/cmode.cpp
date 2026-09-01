#include "cmode.h"

#include "kernel/sysabi.h"

#define BRAAM_COMPAT_BUILDING 1
#include <sys/stat.h>

u32 fmode_flags(Str mode)
{
    if (mode.empty())
        return 0;

    u32 flags = 0;
    switch (mode[0]) {
    case 'r':
        flags = SYS_O_READ;
        break;
    case 'w':
        flags = SYS_O_WRITE | SYS_O_CREATE | SYS_O_TRUNC;
        break;
    case 'a':
        flags = SYS_O_WRITE | SYS_O_CREATE | SYS_O_APPEND;
        break;
    default:
        return 0;
    }

    for (usize i = 1; i < mode.size(); i++) {
        switch (mode[i]) {
        case '+':
            flags |= SYS_O_READ | SYS_O_WRITE;
            // "w+" and "a+" keep their truncation and their positioning.
            break;
        case 'x':
            flags |= SYS_O_EXCL;
            break;
        case 'b':
        case 't':
            break;
        default:
            return 0;
        }
    }
    return flags;
}

u32 stat_mode(u32 kind)
{
    if (kind == SYS_KIND_DIR)
        return S_IFDIR | 0755;
    if (kind == SYS_KIND_LINK)
        return S_IFLNK | 0777;
    return S_IFREG | 0644;
}

u64 stat_ino(Str path)
{
    u64 h = 0xcbf29ce484222325ull;

    for (usize i = 0; i < path.size(); i++) {
        h ^= u8(path[i]);
        h *= 0x100000001b3ull;
    }
    return h;
}
