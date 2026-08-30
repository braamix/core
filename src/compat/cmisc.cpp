// The odds and ends, plus the two ways a port stops.
#include "kernel/host.h"

#include <stdlib.h>

extern "C" {

int abs(int v)
{
    return v < 0 ? -v : v;
}

long labs(long v)
{
    return v < 0 ? -v : v;
}

long long llabs(long long v)
{
    return v < 0 ? -v : v;
}

void abort(void)
{
    panic("compat: abort");
}

// A coroutine cannot exit through a return, so this cannot unwind: it traps.
// Return a status from proc_main instead.
void exit(int)
{
    panic("compat: exit — return a status from proc_main");
}

} // extern "C"
