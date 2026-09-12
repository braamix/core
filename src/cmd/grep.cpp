#include "kernel/fmt.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"
#include "regex/regex.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    grep [-i] [-v] [-F] <pattern> [<file>...]\n"
    "Options:\n"
    "    -i    ignore case\n"
    "    -v    print the lines that do not match\n"
    "    -F    a plain string, not a regular expression\n";

} // namespace

namespace {

char fold_case(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

// -F: plain substring search, which is smaller than compiling a pattern that
// has nothing in it to compile.
bool contains(Str hay, Str needle, bool fold)
{
    if (needle.size() > hay.size())
        return false;
    for (usize i = 0; i + needle.size() <= hay.size(); i++) {
        usize j = 0;
        while (j < needle.size()) {
            char a = hay[i + j], b = needle[j];
            if (fold) {
                a = fold_case(a);
                b = fold_case(b);
            }
            if (a != b)
                break;
            j++;
        }
        if (j == needle.size())
            return true;
    }
    return false;
}

// POD, so it may live here: a coroutine frame has no room for it.
regex_t g_re;

// A line is a region, not a C string: REG_STARTEND means a NUL in the file is
// a byte like any other.
bool matches(Str line)
{
    regmatch_t m = { 0, regoff_t(line.size()) };

    return regexec(&g_re, line.empty() ? "" : line.data(), 1, &m, REG_STARTEND) == 0;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    bool invert = false, fold = false, plain = false;
    usize i = 1;
    for (; i < args.size(); i++) {
        if (args[i] == "-v")
            invert = true;
        else if (args[i] == "-i")
            fold = true;
        else if (args[i] == "-F")
            plain = true;
        else
            break;
    }
    if (i >= args.size())
        co_return co_await usage_error(USAGE);

    Str pattern = args[i];
    if (!plain) {
        // argv is views into a blob, so the pattern is copied to get its NUL.
        String pat;
        if (!pat.append(pattern) || !pat.push('\0'))
            co_return 1;
        int rc = regcomp(&g_re, pat.data(), REG_EXTENDED | REG_NOSUB | (fold ? REG_ICASE : 0));
        if (rc != 0) {
            char msg[64];
            usize n = regerror(rc, &g_re, msg, sizeof msg) - 1;
            Buf<128> b;
            b.put("grep: ").put(Str(msg, n)).put('\n');
            co_await write_all(SYS_STDERR, b.str());
            co_return 2;
        }
    }

    Input files(Args{ args.v.subspan(i + 1) }, SYS_STDIN, "grep");

    File in(files);
    String line;
    bool matched = false;
    i32 status   = 1;

    for (;;) {
        Result<bool> r = co_await in.getline(line);
        if (r.is_err()) {
            status = r.error() == Error::Cancelled ? 130 : 1;
            break;
        }
        if (!r.value()) {
            status = matched ? 0 : 1;
            break;
        }
        bool hit = plain ? contains(line.str(), pattern, fold) : matches(line.str());
        if (hit == invert)
            continue;

        matched = true;
        if (!line.push('\n'))
            break;
        if ((co_await File::stdout().write(line.str())).is_err())
            break;
    }

    // Not on ^C: the process is going, and a failed flush would eat the 130.
    if (status != 130 && (co_await File::stdout().flush()).is_err())
        status = 1;
    if (!plain)
        regfree(&g_re);
    co_return status;
}
