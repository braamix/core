// /bin/sort: the lines held in memory, the keys over them, and the heapsort.
// The in-wasm suite cannot run a program, so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(13930);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    sort [-bfnru] [-t <char>] [-k <key>]... [<file>...]|Options:|" +
    "    -b    ignore blanks at the front of a field|" +
    "    -f    fold upper case onto lower|" +
    "    -n    compare an initial number by value|" +
    "    -r    reverse the order|" +
    "    -u    print one line of each equal run|" +
    "    -t    the field separator; blanks without it|" +
    "    -k    a key, <field>[.<byte>][bfnr], and after a comma|" +
    "          the field it ends in|" +
    "The input is held in memory.";

export function check() {
    line("mkdir /home/so");
    line("cd /home/so");
    line("echo pear > w; echo Apple >> w; echo banana >> w");
    line("echo apple >> w; echo pear >> w");
    line("echo b > f; echo A >> f; echo a >> f; echo B >> f");
    line("echo 10 > n; echo 9 >> n; echo -2 >> n; echo 1.5 >> n");
    line("echo 1.25 >> n; echo x >> n");
    line("echo 99999999999999999999999 >> n");
    line("echo 'c 3 x' > k; echo 'a 10 y' >> k");
    line("echo 'b 2 z' >> k; echo 'a 2 w' >> k");
    line("echo xb1 > c; echo xa2 >> c; echo xc0 >> c");
    line("echo b:2:q > t; echo a:10:p >> t; echo c:2:r >> t");
    line("echo '   b' > s; echo ' a' >> s; echo c >> s");
    line("echo zebra > u8; echo привет >> u8; echo мир >> u8");
    line("echo a > nl; echo -n b >> nl"); // no newline at the end
    line("echo -n '' > e");               // nothing at all

    // Bytes, which in UTF-8 is codepoint order, and a shorter line first where
    // one is the other's prefix.
    is("sort w", "Apple|apple|banana|pear|pear");
    is("sort -r w", "pear|pear|banana|apple|Apple");
    is("sort -u w", "Apple|apple|banana|pear");
    is("sort u8", "zebra|мир|привет");

    // -f folds ASCII only, and the whole line still breaks the tie — so the
    // upper case comes first inside each folded pair.
    is("sort f", "A|B|a|b");
    is("sort -f f", "A|a|B|b");

    // -n: an initial number by value. A line with no digits is zero, a
    // fraction is compared column by column, and 23 digits is not a range.
    is("sort -n n", "-2|x|1.25|1.5|9|10|99999999999999999999999");
    is("sort -nr n", "99999999999999999999999|10|9|1.5|1.25|x|-2");

    // -b is what the leading blanks of a field are for.
    is("sort s", "   b| a|c");
    is("sort -b s", " a|   b|c");

    // A key is a field and what follows it; -k2,2 stops at the field's end.
    // Ties fall back to the whole line, so `a 2 w` precedes `b 2 z`.
    is("sort -k2 k", "a 10 y|a 2 w|b 2 z|c 3 x");
    is("sort -k2n k", "a 2 w|b 2 z|c 3 x|a 10 y");
    is("sort -k2,2n k", "a 2 w|b 2 z|c 3 x|a 10 y");
    is("sort -k1,1 -k2n k", "a 2 w|a 10 y|b 2 z|c 3 x");

    // .byte, counted from the field's start.
    is("sort -k1.2,1.2 c", "xa2|xb1|xc0");
    is("sort -k1.3 c", "xc0|xb1|xa2");

    // -t: the field is what lies between two separators.
    is("sort -t: -k2 t", "a:10:p|b:2:q|c:2:r");
    is("sort -t: -k2,2n t", "b:2:q|c:2:r|a:10:p");
    // A key's own modifiers replace the globals for that key: the second key
    // is reversed and the first is not.
    is("sort -t: -k2,2n -k1r t", "c:2:r|b:2:q|a:10:p");

    // A final fragment with no newline is a line, and nothing at all is
    // nothing at all.
    is("sort nl", "a|b");
    is("sort e", "");

    // Past one 64 KiB block of lines, which is what tells a chain of blocks
    // from a single buffer: 32,768 copies of two lines, 128 KB and 65,536 of
    // them, sorted and counted.
    line("echo b > d0; echo a >> d0");
    line("cat d0 d0 d0 d0 d0 d0 d0 d0 > d1");
    line("cat d1 d1 d1 d1 d1 d1 d1 d1 > d2");
    line("cat d2 d2 d2 d2 d2 d2 d2 d2 > d3");
    line("cat d3 d3 d3 d3 d3 d3 d3 d3 > d4");
    line("cat d4 d4 d4 d4 d4 d4 d4 d4 > d5");
    is("sort d5 | uniq -c", "32768 a|32768 b");
    is("sort -u d5", "a|b");
    is("sort d5 | wc", "65536 65536 131072");

    // Several files are one stream, and so is a pipe.
    is("sort -u c c | wc", "3 3 12");
    is("cat w | sort -u | head -n 2", "Apple|apple");

    // What is asked for, and what is got wrong.
    is("sort --help", USAGE);
    is("sort -q 2>&1 | head -n 2", "sort: bad option: q|Usage:");
    is("sort -k 2>&1 | head -n 1", "sort: needs a value: k");
    is("sort -k0 2>&1 | head -n 1", "sort: bad key: 0");
    is("sort -k1.2.3 2>&1 | head -n 1", "sort: bad key: 1.2.3");
    is("sort -t xy 2>&1 | head -n 1", "sort: -t takes one character: xy");
    is("sort -q > /dev/null 2>&1; echo $?", "2"); // a bad option is 2
    is("sort nope; echo $?", "sort: nope: not found|1");

    line("cd /home");
    line("rm -r /home/so");
    at(); // the session is cumulative: leave the clock past the last line
}
