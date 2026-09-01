// /bin/xargs: arguments off a pipe, batched into commands. The one program in
// src/cmd/ that spawns a command it was handed, and what answers for the
// `find -exec` that /bin/find does not have. The in-wasm suite cannot run a
// program, so this is the whole of the coverage. Part of the system suite;
// test/run.mjs runs the cases in order and doc/Testing.md has the rules they
// run by.

import { counts, shows } from "./harness.mjs";

const { at, is, line } = shows(14140);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|" +
    "    xargs [-0rtx] [-E <eof>] [-I <str>] [-L <lines>]|" +
    "          [-n <count>] [-s <size>] [<command> [<arg>...]]|" +
    "Options:|" +
    "    -0    the input is separated by nulls; nothing quotes|" +
    "    -E    stop at an argument equal to <eof>|" +
    "    -I    put one input line where <str> is in an|" +
    "          argument, and run once a line|" +
    "    -L    input lines per run|" +
    "    -n    input arguments per run|" +
    "    -r    accepted and ignored: an empty input runs nothing|" +
    "    -s    the most bytes one command line may be|" +
    "    -t    print each command on stderr before running it|" +
    "    -x    fail rather than run a command that does not fit";

export function check() {
    // Batching. With no command the input goes to /bin/echo.
    is("seq 3 | xargs", "1 2 3");
    is("seq 3 | xargs echo", "1 2 3");
    is("seq 5 | xargs -n2", "1 2|3 4|5");
    is("seq 3 | xargs -n1 echo x", "x 1|x 2|x 3");
    is("seq 4 | xargs -L2", "1 2|3 4");
    is("seq 5 | xargs -L2", "1 2|3 4|5");
    is("echo a b | xargs -n1", "a|b"); // a line is not an argument
    is("seq 2 | xargs -r -n1", "1|2"); // -r is taken and does nothing

    // A line with nothing on it is not a line, so -L does not count it and no
    // run is made over no arguments at all.
    is("echo | xargs echo .", "");
    is("xargs echo . < /dev/null", "");
    is("seq 40 | xargs -n1 | wc", counts(40, 40, 111)); // forty runs, one at a time

    // Quoting, which -0 turns off along with everything else.
    is("echo '\"a b\" c' | xargs -n1", "a b|c");
    is("echo \"'a b' c\" | xargs -n1", "a b|c");
    is("echo 'a\\ b' | xargs -n1", "a b");
    is("echo 'a\"b\"c' | xargs", "abc");
    // Quoted, so it is an argument: `echo ""` prints a line, and no argument
    // at all would have run nothing.
    is("echo \\'\\' | xargs | wc", counts(1, 0, 1));
    is("echo '\"a' | xargs 2>&1", "xargs: unterminated quote");
    is("echo '\"a' | xargs > /dev/null 2>&1; echo $?", "1");

    // -0: nulls separate, and the quotes above are ordinary bytes.
    is("seq 3 | tr '\\n' '\\000' | xargs -0 -n1", "1|2|3");
    is("echo '\"a b\"' | tr '\\n' '\\000' | xargs -0", '"a b"');

    // -I, which is a run to the line, puts the line where the marker is, and
    // does not also pass the arguments.
    is("seq 2 | xargs -I% echo [%]", "[1]|[2]");
    is("echo a | xargs -I% echo x", "x");
    is("echo a b | xargs -I% echo [%]", "[a b]"); // the line, not the words
    is("echo a | xargs -I% echo %-%", "a-a"); // every one of them
    is("seq 2 | xargs -Iq echo q", "1|2");

    // -E ends the input at an argument equal to its own, whole.
    is("seq 3 | xargs -E 2", "1");
    is("seq 3 | xargs -E 9", "1 2 3");
    is("echo a ab b | xargs -E a", ""); // whole, so `ab` is not `a`

    // -t writes what it is about to run, on stderr.
    is("echo a | xargs -t echo 2>&1", "echo a|a");

    // -s bounds the command line in bytes, the command's own words included.
    is("seq 5 | xargs -s 16", "1 2 3|4 5"); // ten of the sixteen are /bin/echo
    is("seq 5 | xargs -s16 -n9 -x 2>&1 | head -n 1",
       "xargs: insufficient space for arguments");
    // An argument the batch ran out in the middle of: what is read of it so
    // far moves to the front of the buffer and the rest follows it.
    is("echo aa bbb | xargs -s 15", "aa|bbb");
    is("echo abcdefghij | xargs -s12 2>&1 | head -n 1",
       "xargs: insufficient space for an argument");
    is("xargs -x 2>&1 | head -n 1", "xargs: -x wants -n");

    // The child reads /dev/null, not what is left of this pipe: `wc` in a
    // shell of its own counts nothing, twice.
    is("seq 2 | xargs -n1 sh -c wc", counts(0, 0, 0) + "|" + counts(0, 0, 0));

    // What A7 exists for, and what /bin/find was left without.
    line("mkdir /home/sx");
    line("cd /home/sx");
    line("echo one > a; echo two > b; echo three > c");
    is("find . -type f | sort | xargs cat", "one|two|three");
    is("find . -name 'b' | xargs -I% cat %", "two");

    // The status: a run that failed is 1, and the two that stop the reading.
    is("seq 2 | xargs -n1 false; echo $?", "1");
    is("seq 2 | xargs -n1 true; echo $?", "0");
    is("echo a | xargs nocmd 2>&1; echo $?", "xargs: nocmd: not found|127");

    // What is asked for, and what is got wrong.
    is("xargs --help", USAGE);
    is("xargs -q 2>&1 | head -n 1", "xargs: bad option: q");
    is("xargs -q > /dev/null 2>&1; echo $?", "2"); // a bad option is 2
    is("xargs -n x 2>&1 | head -n 1", "xargs: not a count: x");
    is("xargs -I 2>&1 | head -n 1", "xargs: needs a value: I");

    line("cd /home");
    line("rm -r /home/sx");
    at(); // the session is cumulative: leave the clock past the last line
}
