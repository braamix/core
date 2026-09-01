// /bin/grep: a substring, and the lines that hold it. Its loop is the one B3
// was about, so the depth case here is the whole of that fix's coverage. The
// in-wasm suite cannot run a program, so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { counts, shows } from "./harness.mjs";

const { at, is, line } = shows(14150);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|" +
    "    grep [-i] [-v] <text> [<file>...]|" +
    "Options:|" +
    "    -i    ignore case|" +
    "    -v    print the lines that do not match";

export function check() {
    line("mkdir /home/gr");
    line("cd /home/gr");
    line("echo one > a; echo Two >> a; echo three >> a");
    line("echo four > b");

    // A substring and nothing more: there is no expression engine.
    is("grep e a", "one|three");
    is("cat a | grep three", "three");
    is("grep -i two a", "Two");
    is("grep two a; echo $?", "1");
    is("grep -v e a", "Two");

    // Several files are one stream, and no name is printed in front.
    is("grep o a b", "one|Two|four");

    // A file that will not open is a diagnostic and a status.
    is("grep one nope 2>&1; echo $?", "grep: nope: not found|1");

    // The line loop is an awaiter, so a line already in the buffer enters no
    // coroutine: this died at 4,096 lines when getline was a Task (B3).
    line("seq 1 65536 > big");
    is("grep zzz big; echo $?", "1");
    is("grep 65536 big", "65536");
    is("grep -v zzz big | wc -l", counts(65536));
    line("rm big");

    is("grep -h", USAGE);

    line("cd /");
    line("rm -r /home/gr");
    at();
}
