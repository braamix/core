// /bin/basename and /bin/dirname: text in, text out, no store between them.
// Part of the smoke suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { has, is } = shows(14000);

export function check() {
    // The POSIX pair, and the trailing slashes each has to trim first.
    is("basename /usr/lib/libz.so", "libz.so");
    is("basename /usr/lib/", "lib");
    is("basename ///", "/");
    is("basename foo", "foo");
    is("basename a/b//", "b");

    // Nothing is opened: a path that is not there answers the same.
    is("basename /nope/gone.txt", "gone.txt");

    // The suffix operand, and the name that is only the suffix.
    is("basename f.c .c", "f");
    is("basename .c .c", ".c");
    is("basename libz.so .a", "libz.so");

    // -a takes every operand as a path, and -s implies it.
    is("basename -a /a/b /c/d", "b|d");
    is("basename -s .c a.c b.c", "a|b");

    // An empty word is an empty answer, which needs a capture to see.
    is("echo [$(basename '')]", "[]");

    is("dirname /usr/lib/libz.so", "/usr/lib");
    is("dirname /usr/lib/", "/usr");
    is("dirname a//b", "a");
    is("dirname foo", ".");
    is("dirname /", "/");
    is("dirname /a", "/");
    is("dirname /a /b/c", "/|/b");
    is("echo [$(dirname '')]", "[.]");

    // Usage on stderr, and the status that says so. The second line of
    // basename's is a continuation, so only the first is compared.
    has("basename", "usage: basename <path> [<suffix>]");
    is("basename a b c; echo $?", "usage: basename <path> [<suffix>]|" +
                                  "       basename [-a] [-s <suffix>] <path>...|2");
    is("basename -q x", "basename: illegal option -- q|" +
                        "usage: basename <path> [<suffix>]|" +
                        "       basename [-a] [-s <suffix>] <path>...");
    is("dirname; echo $?", "usage: dirname <path>...|2");
}
