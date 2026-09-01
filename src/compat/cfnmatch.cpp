// fnmatch. Backtracking on '*' rather than recursion, so a pattern of stars
// cannot reach the shadow stack.
#include <ctype.h>
#include <fnmatch.h>
#include <string.h>

namespace {

struct Named {
    const char *name;
    int (*fn)(int);
};

const Named CLASSES[] = {
    { "alnum", isalnum }, { "alpha", isalpha }, { "blank", isblank }, { "cntrl", iscntrl },
    { "digit", isdigit }, { "graph", isgraph }, { "lower", islower }, { "print", isprint },
    { "punct", ispunct }, { "space", isspace }, { "upper", isupper }, { "xdigit", isxdigit },
};

int fold(int c, int flags)
{
    return (flags & FNM_CASEFOLD) ? tolower(c) : c;
}

bool in_class(const char *name, unsigned long n, int c)
{
    for (const Named &k : CLASSES)
        if (strlen(k.name) == n && strncmp(k.name, name, n) == 0)
            return k.fn(c) != 0;
    return false;
}

// `p` is just past the '['. Answers the byte after the ']', or null when there
// is none, in which case the '[' was a literal.
const char *bracket(const char *p, int c, int flags, bool &ok)
{
    bool neg = false;
    if (*p == '!' || *p == '^') {
        neg = true;
        p++;
    }

    bool hit = false;
    for (bool first = true; *p && (*p != ']' || first); first = false) {
        if (p[0] == '[' && p[1] == ':') {
            const char *name = p + 2;
            const char *e    = name;
            while (*e && !(e[0] == ':' && e[1] == ']'))
                e++;
            if (*e) {
                if (in_class(name, (unsigned long)(e - name), c))
                    hit = true;
                p = e + 2;
                continue;
            }
        }

        int lo = (unsigned char)*p;
        if (lo == '\\' && !(flags & FNM_NOESCAPE) && p[1])
            lo = (unsigned char)*++p;
        p++;

        if (p[0] == '-' && p[1] && p[1] != ']') {
            p++;
            int hi = (unsigned char)*p;
            if (hi == '\\' && !(flags & FNM_NOESCAPE) && p[1])
                hi = (unsigned char)*++p;
            p++;
            if (fold(c, flags) >= fold(lo, flags) && fold(c, flags) <= fold(hi, flags))
                hit = true;
        } else if (fold(lo, flags) == fold(c, flags)) {
            hit = true;
        }
    }

    if (*p != ']')
        return nullptr;
    ok = hit != neg;
    return p + 1;
}

} // namespace

extern "C" int fnmatch(const char *pattern, const char *string, int flags)
{
    const char *p = pattern, *s = string;
    const char *star_p = nullptr, *star_s = nullptr;

    // A period that only a literal period may match.
    auto leading = [&](const char *at) {
        if (!(flags & FNM_PERIOD) || *at != '.')
            return false;
        return at == string || ((flags & FNM_PATHNAME) && at[-1] == '/');
    };

    for (;;) {
        if (*s == '\0') {
            while (*p == '*')
                p++;
            return *p == '\0' ? 0 : FNM_NOMATCH;
        }
        if ((flags & FNM_LEADING_DIR) && *p == '\0' && *s == '/')
            return 0;

        if (*p == '*') {
            if (leading(s))
                return FNM_NOMATCH;
            star_p = ++p;
            star_s = s;
            continue;
        }

        bool matched      = false;
        const char *after = p + 1;
        bool wild         = !leading(s) && !((flags & FNM_PATHNAME) && *s == '/');

        if (*p == '?') {
            matched = wild;
        } else if (*p == '[') {
            bool ok         = false;
            const char *end = bracket(p + 1, (unsigned char)*s, flags, ok);
            if (end) {
                matched = ok && wild;
                after   = end;
            } else {
                matched = *s == '[';
            }
        } else {
            const char *lit = p;
            if (*lit == '\\' && !(flags & FNM_NOESCAPE) && lit[1])
                lit++;
            matched = *lit != '\0' && fold((unsigned char)*lit, flags) ==
                                          fold((unsigned char)*s, flags);
            after   = lit + 1;
        }

        if (matched) {
            p = after;
            s++;
            continue;
        }
        if (!star_p)
            return FNM_NOMATCH;
        // A '*' does not cross a component boundary.
        if ((flags & FNM_PATHNAME) && *star_s == '/')
            return FNM_NOMATCH;
        s = ++star_s;
        p = star_p;
    }
}
