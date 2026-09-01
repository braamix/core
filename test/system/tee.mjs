// /bin/tee: the input to the output and to every named file at once. The
// in-wasm suite cannot run a program, so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(14020);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    tee [-ai] [<file>...]|Options:|" +
    "    -a    add to the files rather than truncating them|" +
    "    -i    keep going when ^C arrives";

export function check() {
    line("mkdir /home/st");
    line("cd /home/st");

    // Through, and into a file at the same time.
    is("echo hi | tee a", "hi");
    is("cat a", "hi");

    // Truncating by default, adding under -a.
    is("echo two | tee a > /dev/null; cat a", "two");
    is("echo three | tee -a a > /dev/null; cat a", "two|three");

    // Two files at once, and the output still goes through.
    is("echo both | tee b c", "both");
    is("cat b c", "both|both");

    // In the middle of a pipeline, which is what it is for.
    is("echo mid | tee d | wc", "1 1 4");
    is("cat d", "mid");

    // Nothing named is a cat, and nothing at all is nothing at all.
    is("echo plain | tee", "plain");
    is("echo -n '' | tee e; cat e", "");

    // A file that will not open is reported and the status is 1; whatever
    // else was named still gets the bytes.
    is("echo x | tee /nosuch/f g 2>&1", "tee: /nosuch/f: not found|x");
    is("cat g", "x");
    is("echo x | tee /nosuch/f > /dev/null 2>&1; echo $?", "1");

    // Bytes, not lines: what goes in comes out, newline or none.
    is("echo -n no-nl | tee h | wc", "0 1 5");
    is("cat h | wc", "0 1 5");

    // Past a single read, and both ways out of it agree. Sixteen bytes
    // doubled six times, so nothing here tracks the size of another file.
    line("echo 0123456789abcde > w0");
    line("cat w0 w0 w0 w0 > w1; cat w1 w1 w1 w1 > w2");
    line("cat w2 w2 w2 w2 > w3; cat w3 w3 w3 w3 > big");
    is("cat big | tee big2 | wc", "256 256 4096");
    is("wc big2", "256 256 4096");

    // A reader that hangs up ends the run rather than spinning through the
    // rest of the input, which is what stops the producer upstream.
    is("cat big | tee big3 | head -n 2", "0123456789abcde|0123456789abcde");

    // What is asked for, and what is got wrong.
    is("tee --help", USAGE);
    is("tee -q 2>&1 | head -n 2", "tee: bad option: q|Usage:");
    is("tee -q < /dev/null > /dev/null 2>&1; echo $?", "2");

    line("cd /home");
    line("rm -r /home/st");
    at(); // the session is cumulative: leave the clock past the last line
}
