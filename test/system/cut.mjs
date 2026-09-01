// /bin/cut: the columns of a line, as bytes, as characters or as fields. The
// in-wasm suite cannot run a program, so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(14050);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    cut -b|-c|-f <list> [-d <char>] [-s] [<file>...]|" +
    "Options:|" +
    "    -b    select these byte positions|" +
    "    -c    select these character positions|" +
    "    -f    select these fields|" +
    "    -d    the field separator; a tab without it|" +
    "    -s    drop the lines that hold no separator";

export function check() {
    line("mkdir /home/sc");
    line("cd /home/sc");
    line("echo abcdef > a; echo ghijkl >> a");
    line("echo 'p:q:r' > f; echo 's:t:u' >> f");
    line("echo nosep >> f");

    // Every list form, and the same answer whichever way it is written.
    is("cut -b 3 a", "c|i");
    is("cut -b 2-4 a", "bcd|hij");
    is("cut -b -3 a", "abc|ghi");
    is("cut -b 4- a", "def|jkl");
    is("cut -b 1,3,5 a", "ace|gik");
    is("cut -b 1-2,5-6 a", "abef|ghkl");
    is("cut -b '1 3' a", "ac|gi"); // blanks separate as commas do
    is("cut -b 5,1 a", "ae|gk");   // out of order
    is("cut -b 1-3,2-4 a", "abcd|ghij"); // overlapping, merged
    is("cut -b 1-99 a", "abcdef|ghijkl");
    is("cut -b 9 a", "|"); // past the end is an empty line, not a dropped one

    // -c is characters and -b is bytes, which is the whole of the difference.
    line("echo привет > u");
    is("cut -c 1-3 u", "при");
    is("cut -b 1-6 u", "при");
    is("cut -c 4- u", "вет");

    // Fields, with the separator between what is kept.
    is("cut -d : -f 2 f", "q|t|nosep");
    is("cut -d : -f 1,3 f", "p:r|s:u|nosep");
    is("cut -d : -f 2- f", "q:r|t:u|nosep");
    is("cut -d : -f 9 f", "||nosep"); // no such field, but the line is there
    is("cut -sd : -f 2 f", "q|t");    // -s drops the one with no separator
    is("cut -d : -f 1 f | wc", "3 3 10");

    // The default separator is a tab, and a line without one passes whole. A
    // tab cannot be typed at this prompt, so /bin/tr writes one.
    line("echo 'x y z' | tr ' ' '\\t' > t");
    is("cut -f 2 t", "y");
    is("cut -f 2 a", "abcdef|ghijkl");
    is("cut -sf 2 a", "");

    // A last line with no newline is a line, and one gets a newline of its own.
    line("echo -n 'mnop' > n");
    is("cut -b 2-3 n", "no");

    // Far past BSD's 2,048-byte line, which was fatal there: 4,096 bytes on
    // one line, sixteen quadrupled four times.
    line("echo -n 0123456789abcdef > w0");
    line("cat w0 w0 w0 w0 > w1; cat w1 w1 w1 w1 > w2");
    line("cat w2 w2 w2 w2 > w3; cat w3 w3 w3 w3 > w4; echo >> w4");
    is("wc w4", "1 1 4097");
    is("cut -b 4094- w4", "def");
    is("cut -b 1-3 w4", "012");

    // Named files are one stream, and stdin is the default.
    is("cat a | cut -b 1", "a|g");
    is("cut -b 1 a a", "a|g|a|g");

    // What is asked for, and what is got wrong.
    is("cut --help", USAGE);
    is("cut 2>&1 | head -n 1", "Usage:");
    is("cut -b 1 -f 1 a 2>&1 | head -n 1", "cut: only one of -b, -c and -f");
    is("cut -s -b 1 a 2>&1 | head -n 1", "cut: -d and -s go with -f");
    is("cut a 2>&1 | head -n 1", "cut: one of -b, -c and -f is needed");
    is("cut -b 0 a 2>&1 | head -n 1", "cut: a position starts at one: 0");
    is("cut -b x a 2>&1 | head -n 1", "cut: not a list: x");
    is("cut -b 2-1 a 2>&1 | head -n 1",
       "cut: the range ends before it starts: 2-1");
    is("cut -d xy -f 1 f 2>&1 | head -n 1",
       "cut: the separator is one character: xy");
    is("cut -q 2>&1 | head -n 1", "cut: bad option: q");
    is("cut -b 2>&1 | head -n 1", "cut: needs a value: b");
    is("cut -q > /dev/null 2>&1; echo $?", "2"); // a bad option is 2
    is("cut -b 1 nope; echo $?", "cut: nope: not found|1");

    line("cd /home");
    line("rm -r /home/sc");
    at(); // the session is cumulative: leave the clock past the last line
}
