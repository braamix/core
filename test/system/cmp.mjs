// /bin/cmp: two byte streams, and where they first differ. The in-wasm suite
// cannot run a program, so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(14160);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|" +
    "    cmp [-bhlsxz] [-i <skip>[:<skip>]] [-n <count>]|" +
    "        <file1> <file2> [<skip1> [<skip2>]]|" +
    "Options:|" +
    "    -b    show the two bytes that differ|" +
    "    -h    compare a symbolic link, not what it points at|" +
    "    -i    skip this many bytes, both or one for each|" +
    "    -l    list every difference, not only the first|" +
    "    -n    compare no more than this many bytes|" +
    "    -s    print nothing; the status is the answer|" +
    "    -x    list them in hex, counting from zero|" +
    "    -z    files of different sizes differ, unread|" +
    "A count is bytes; K M G T are 1024, KB MB GB TB 1000.";

export function check() {
    line("mkdir /home/cp2");
    line("cd /home/cp2");
    line("echo ab > a; echo ab > b; echo ac > c");
    line("echo abcd > d; echo xy > x");
    line("echo ab > p; echo cd >> p"); // a, then a second line

    // The same file is silence and 0; the first difference is char and line.
    is("cmp a b; echo $?", "0");
    is("cmp a c; echo $?", "a c differ: char 2, line 1|1");
    is("cmp a d; echo $?", "a d differ: char 3, line 1|1");

    // A shorter file that agrees as far as it goes is an EOF on stderr.
    is("cmp a p; echo $?", "cmp: EOF on a|1");
    is("cmp p a; echo $?", "cmp: EOF on a|1"); // the short one, either way round

    // Every difference rather than the first, three ways of showing it.
    line("echo aXcY > e; echo abcd > f");
    is("cmp -l e f", "     2 130 142|     4 131 144");
    is("cmp -x e f", "00000001 58 62|00000003 59 64"); // hex, counting from 0
    is("cmp -l -b e f", "     2 130 X 142 b|     4 131 Y 144 d");
    is("cmp -b a c", "a c differ: char 2, line 1 is 142 b 143 c");

    // The line number counts newlines in what has been read.
    line("echo one > g; echo two >> g");
    line("echo one > h; echo TWO >> h");
    is("cmp g h", "g h differ: char 5, line 2");

    // -s is the status alone, and says nothing on either stream.
    is("cmp -s a b; echo $?", "0");
    is("cmp -s a c; echo $?", "1");
    is("cmp -s a p 2>&1; echo $?", "1"); // not even the EOF
    is("cmp -s a nope 2>&1; echo $?", "2");

    // -z answers from the sizes without reading, and -n stops early.
    is("cmp -z a d; echo $?", "a d differ: size|1");
    is("cmp -z a b; echo $?", "0");
    is("cmp -n 2 a d; echo $?", "0"); // the first two bytes agree
    is("cmp -n 3 a d; echo $?", "a d differ: char 3, line 1|1");
    is("cmp -n 1K a b; echo $?", "0"); // a count takes a unit

    // Skips, as -i and as operands. Both are counted from the skip.
    is("cmp -i 1 a x; echo $?", "a x differ: char 1, line 1|1");
    is("cmp -i 1:0 a a; echo $?", "a a differ: char 1, line 1|1");
    is("cmp a d 0 2; echo $?", "a d differ: char 1, line 1|1");
    is("cmp a p 0 3; echo $?", "a p differ: char 1, line 1|1");

    // A pipe has no seek, so the skip is read away instead.
    is("cat a | cmp -i 1 - x; echo $?", "stdin x differ: char 1, line 1|1");
    is("cat a | cmp - b; echo $?", "0");

    // -h compares the links themselves, which is their targets as text.
    line("ln -s a l1; ln -s a l2; ln -s p l3");
    is("cmp -h l1 l2; echo $?", "0");
    is("cmp -h l1 l3; echo $?", "l1 l3 differ: char 1, line 1|1");
    is("cmp l1 l2; echo $?", "0"); // without it, what they point at
    is("cmp -h l1 a 2>&1; echo $?", "cmp: a: not a symbolic link|2");

    // What is asked for, and what is got wrong.
    is("cmp --help", USAGE);
    is("cmp a 2>&1 | head -n 1", "cmp: two files are needed");
    is("cmp a b c d e 2>&1 | head -n 1", "cmp: two files are needed");
    is("cmp -s -l a b 2>&1 | head -n 1",
       "cmp: -s goes with neither -l nor -x");
    is("cmp -i x a b 2>&1 | head -n 1", "cmp: not a count: x");
    is("cmp -i +1 a b 2>&1 | head -n 1", "cmp: not a count: +1"); // no modifier
    is("cmp -n z a b 2>&1 | head -n 1", "cmp: not a count: z");
    is("cmp a b q 2>&1 | head -n 1", "cmp: not a count: q");
    is("cmp -q a b 2>&1 | head -n 1", "cmp: bad option: q");
    is("cmp -n 2>&1 | head -n 1", "cmp: needs a value: n");
    is("cmp -q a b > /dev/null 2>&1; echo $?", "2"); // a bad option is 2
    is("cmp a nope 2>&1; echo $?", "cmp: nope: not found|2");
    is("cat a | cmp - - 2>&1 | head -n 1", "cmp: - names the input twice");

    line("cd /home");
    line("rm -r /home/cp2");
    at(); // the session is cumulative: leave the clock past the last line
}
