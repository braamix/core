// /bin/uniq: the runs of adjacent lines, and the two skips in front of the
// comparison. The in-wasm suite cannot run a program, so this is the whole of
// the coverage. Part of the system suite; test/run.mjs runs the cases in order
// and doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(13940);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    uniq [-cdu] [-f <n>] [-s <n>] [<file>...]|Options:|" +
    "    -c    prefix each line with how many times it ran|" +
    "    -d    print only the lines that repeated|" +
    "    -u    print only the lines that did not|" +
    "    -f    ignore the first <n> blank-delimited fields|" +
    "    -s    ignore <n> further bytes";

export function check() {
    line("mkdir /home/su");
    line("cd /home/su");
    line("echo a > u; echo a >> u; echo b >> u; echo a >> u");
    line("echo c >> u; echo c >> u; echo c >> u");
    line("echo 'p xa' > g; echo 'q ya' >> g; echo 'r xb' >> g");
    line("echo one > one");
    line("echo -n '' > e");

    // Adjacent only: the second `a` run is a run of its own.
    is("uniq u", "a|b|a|c");
    is("uniq -c u", "   2 a|   1 b|   1 a|   3 c");
    is("uniq -d u", "a|c");
    is("uniq -u u", "b|a");
    is("uniq -c -d u", "   2 a|   3 c");
    is("uniq -cu u", "   1 b|   1 a"); // bundled, and -c counts either way

    // -f stops at the blank in front of the next field rather than past it,
    // which is v7's skip and what -s is then for. -s counts bytes from there.
    is("uniq -f 1 g", "p xa|q ya|r xb");
    is("uniq -f 1 -s 2 g", "p xa|r xb");
    is("uniq -f 1 -s 1 g", "p xa|q ya|r xb");
    is("uniq -s 3 g", "p xa|r xb");
    is("uniq -c -f 1 -s 2 g", "   2 p xa|   1 r xb");

    // One line is one run, and nothing at all is nothing at all.
    is("uniq one", "one");
    is("uniq -c one", "   1 one");
    is("uniq e", "");

    // 65,536 lines, none of them next to its like: a group per line, and the
    // output written in batches rather than a syscall at a time.
    line("echo b > d0; echo a >> d0");
    line("cat d0 d0 d0 d0 d0 d0 d0 d0 > d1");
    line("cat d1 d1 d1 d1 d1 d1 d1 d1 > d2");
    line("cat d2 d2 d2 d2 d2 d2 d2 d2 > d3");
    line("cat d3 d3 d3 d3 d3 d3 d3 d3 > d4");
    line("cat d4 d4 d4 d4 d4 d4 d4 d4 > d5");
    is("uniq d5 | wc", "65536 65536 131072");
    is("uniq -c d5 | tail -n 1", "   1 a");

    // What it is for: the sort in front of it, through a real pipe.
    is("sort u | uniq -c", "   3 a|   1 b|   3 c");
    is("cat u | uniq -d | wc", "2 2 4");

    // What is asked for, and what is got wrong.
    is("uniq --help", USAGE);
    is("uniq -q 2>&1 | head -n 2", "uniq: bad option: q|Usage:");
    is("uniq -f 2>&1 | head -n 1", "uniq: needs a value: f");
    is("uniq -s x 2>&1 | head -n 1", "uniq: not a count: x");
    is("uniq -q > /dev/null 2>&1; echo $?", "2"); // a bad option is 2
    is("uniq nope; echo $?", "uniq: nope: not found|1");

    line("cd /home");
    line("rm -r /home/su");
    at(); // the session is cumulative: leave the clock past the last line
}
