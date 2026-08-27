// /bin/basename and /bin/dirname: text in, text out, no store between them.
// Part of the system suite; test/run.mjs runs the cases in order and
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

    // Asking is stdout and 0; getting it wrong is stderr and 2.
    const base = "Usage:|    basename <path> [<suffix>]|" +
                 "    basename [-a] [-s <suffix>] <path>...|Options:|" +
                 "    -a    every operand is a path, none of them a suffix|" +
                 "    -s    strip this suffix, and imply -a";
    is("basename; echo $?", `${base}|0`);
    is("basename -h; echo $?", `${base}|0`);
    is("basename --help; echo $?", `${base}|0`);
    is("basename a b c; echo $?", `${base}|2`);
    is("basename -q x", `basename: illegal option -- q|${base}`);
    is("dirname; echo $?", "Usage:|    dirname <path>...|0");
    is("dirname -h; echo $?", "Usage:|    dirname <path>...|0");
    // A file named -h is still an operand when it is not the only word.
    is("dirname -h x", ".|.");
}
