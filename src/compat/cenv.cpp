// getenv's other half: the one line that needs braam_proc. There is no setenv
// (doc/TODO.md), so an interned answer can never go stale.
#include "cenv.h"

#include "proc/rt.h"

#include <stdlib.h>
#include <string.h>

extern "C" char *getenv(const char *name)
{
    if (!name)
        return nullptr;
    Str n = Str(name, strlen(name));
    return env_intern(n, proc_env(n));
}
