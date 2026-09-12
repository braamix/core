// /bin/grep: an ERE by default, a plain string under -F, and the lines that
// match. The engine itself is test/unit/test_regex.cpp's; what is here is the
// program around it. Its loop is the one B3 was about, so the depth case here
// is the whole of that fix's coverage. The in-wasm suite cannot run a program,
// so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { counts, shows } from "./harness.mjs";

const { at, is, line } = shows(14150);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|" +
    "    grep [-i] [-v] [-F] <pattern> [<file>...]|" +
    "Options:|" +
    "    -i    ignore case|" +
    "    -v    print the lines that do not match|" +
    "    -F    a plain string, not a regular expression";

export function check() {
    line("mkdir /home/gr");
    line("cd /home/gr");
    line("echo one > a; echo Two >> a; echo three >> a");
    line("echo four > b");

    // A pattern with nothing in it is still a substring.
    is("grep e a", "one|three");
    is("cat a | grep three", "three");
    is("grep -i two a", "Two");
    is("grep two a; echo $?", "1");
    is("grep -v e a", "Two");

    // Several files are one stream, and no name is printed in front.
    is("grep o a b", "one|Two|four");

    // The engine, reached: anchors, a class, a bracket, alternation, a repeat.
    is("grep '^t' a", "three");
    is("grep 'e$' a", "one|three");
    is("grep 'o.e' a", "one");
    is("grep 'one|four' a b", "one|four");
    is("grep '[Tt]' a", "Two|three");
    is("grep 'e+' a", "one|three");
    is("grep '[[:upper:]]' a", "Two");
    is("grep -i '^TW' a", "Two");

    // -F takes the pattern as bytes, which is the difference.
    line("echo 'a.c' > d; echo abc >> d");
    is("grep 'a.c' d", "a.c|abc");
    is("grep -F 'a.c' d", "a.c");
    is("grep -F '[' d; echo $?", "1");
    line("rm d");

    // A pattern the engine refuses is a diagnostic and 2, not a no-match.
    is("grep '[' a 2>&1; echo $?", "grep: Unmatched [|2");
    is("grep 'a{2,1}' a 2>&1; echo $?", "grep: Invalid repetition count|2");

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
