// The port kit's worked example: the includes and the idioms are C's, and the
// only Braam-shaped thing is proc_main and the one write.
#include "proc/io.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace {

int by_str(const void *a, const void *b)
{
    return strcmp(*static_cast<const char *const *>(a), *static_cast<const char *const *>(b));
}

} // namespace

Task<i32> proc_main(Args)
{
    const char *words[] = { "pear", "apple", "fig", "cherry" };
    qsort(words, 4, sizeof(words[0]), by_str);

    char line[PATH_MAX];
    line[0] = '\0';
    for (const char *w : words) {
        strlcat(line, w, sizeof(line));
        strlcat(line, " ", sizeof(line));
    }

    // strtol saturates and says so, which none of the ports' own copies did.
    errno       = 0;
    long big    = strtol("99999999999", nullptr, 0);
    bool ranged = errno == ERANGE && big == LONG_MAX;

    char *dup = strdup(line);
    for (char *p = dup; *p; p++)
        *p = char(toupper(*p));

    co_await write_all(SYS_STDOUT, Str(dup, strlen(dup)));
    co_await write_all(SYS_STDOUT, ranged ? "\nERANGE ok\n" : "\nERANGE MISSING\n");
    free(dup);
    co_return 0;
}
