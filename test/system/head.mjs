// /bin/head: the first lines or bytes of each file, with the ==> <== header
// several files get. The in-wasm suite cannot run a program, so this is the
// whole of the coverage. Part of the system suite; test/run.mjs runs the cases
// in order and doc/Testing.md has the rules they run by.

import { counts, shows } from "./harness.mjs";

const { at, is, line } = shows(14130);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    head [-qv] [-n <count> | -c <count>] [<file>...]|" +
    "Options:|" +
    "    -n    how many lines; ten without it|" +
    "    -c    how many bytes instead|" +
    "    -q    never print the ==> <== header|" +
    "    -v    always print it";

export function check() {
    line("mkdir /home/hd");
    line("cd /home/hd");
    line("seq 1 20 > n");
    line("echo one > a; echo two >> a");
    line("echo three > b");
    line("echo -n frag > f");

    // Ten without -n, and the count is however it is spelled.
    is("head n", "1|2|3|4|5|6|7|8|9|10");
    is("head -n 3 n", "1|2|3");
    is("head -n3 n", "1|2|3");
    is("head -3 n", "1|2|3");    // BSD's obsolete form
    is("head -n 1K n | wc -l", counts(20)); // truncate's units, and past the file
    is("cat n | head -n 2", "1|2");
    is("head -n 30 a", "one|two"); // past the end is the whole file

    // Bytes instead, and only as many as were asked for.
    is("head -c 3 a", "one");
    is("head -c 1 a", "o");
    is("head -c 1K a | wc -c", counts(8));

    // A final fragment with no newline is a line, and gains none: the prompt
    // comes back on the same row, which is what `echo -n` does too.
    is("head -n 1 f", "frag");

    // One file is bare; several are headed, with a blank line between.
    is("head -n 1 a", "one");
    is("head -n 1 a b", "==> a <==|one||==> b <==|three");
    is("head -q -n 1 a b", "one|three");   // -q drops them
    is("head -v -n 1 a", "==> a <==|one"); // -v forces one over a single file
    is("head -qv -n 1 a", "==> a <==|one");
    is("head -vq -n 1 a b", "one|three"); // the last of the two wins

    // Stdin is never headed, even under -v, since there is no name to print.
    is("echo hi | head -v", "hi");

    // The count applies to each file rather than to the concatenation.
    is("head -n 1 a a", "==> a <==|one||==> a <==|one");

    // A file that will not open is a diagnostic and a status, and the rest are
    // still printed.
    is("head -n 1 nope b", "head: nope: not found|==> b <==|three");
    is("head -n 1 nope > /dev/null 2>&1; echo $?", "1");
    is("head -n 1 b > /dev/null; echo $?", "0");

    // Far past the depth a coroutine per line died at: 65,536 lines through
    // one read loop, which is why this program no longer holds a File.
    line("seq 1 65536 > big");
    is("head -n 65536 big | wc -l", counts(65536));
    line("rm big");

    // Every way of asking for a count that is not one.
    is("head -n 0 a 2>&1 | head -n 1", "head: illegal line count: 0");
    is("head -n x a 2>&1 | head -n 1", "head: illegal line count: x");
    is("head -n +5 a 2>&1 | head -n 1", "head: illegal line count: +5");
    is("head -c 0 a 2>&1 | head -n 1", "head: illegal byte count: 0");
    is("head -n 1 -c 2 a 2>&1 | head -n 1", "head: can't combine line and byte counts");
    is("head -n a 2>&1 | head -n 1", "head: illegal line count: a");
    is("head -z a 2>&1 | head -n 1", "head: bad option: z");
    // The obsolete form is a leading run only, as BSD's obsolete() is.
    is("head -q -5 a 2>&1 | head -n 1", "head: bad option: 5");
    is("head -n 2>&1 | head -n 1", "head: needs a value: n");
    is("head -h", USAGE);

    line("cd /");
    line("rm -r /home/hd");
    at();
}
