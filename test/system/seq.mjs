// /bin/seq: a range of doubles, printed. The first program in src/cmd/ to link
// braam::math. The in-wasm suite cannot run a program, so this is the whole of
// the coverage. Part of the system suite; test/run.mjs runs the cases in order
// and doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(14110);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    seq [-w] [-f <fmt>] [-s <str>] [-t <str>]|" +
    "        [<first> [<incr>]] <last>|" +
    "Options:|" +
    "    -w    pad with zeros so every number is the same width|" +
    "    -f    a printf conversion for one number, %g without it|" +
    "    -s    what goes between them; a newline without it|" +
    "    -t    what goes after the last one";

export function check() {
    // One to three operands.
    is("seq 3", "1|2|3");
    is("seq 2 5", "2|3|4|5");
    is("seq 1 2 9", "1|3|5|7|9");
    is("seq 1 1", "1");
    is("seq 5 -1 1", "5|4|3|2|1");

    // The increment defaults to +1 whichever way the range points, which is
    // GNU's and not FreeBSD's: `for i in $(seq 1 $n)` with n of 0 is why.
    is("seq 1 0", "");
    is("seq 3 1", "");
    is("seq 1 -1 5", ""); // an increment pointing the wrong way, likewise
    is("seq 1 0 5 2>&1 | head -n 1", "seq: the increment is zero: 0");

    // Fractions, and the rounding fixup that is the whole reason for the
    // formatted comparison at the end of the loop.
    is("seq 1 0.1 1.2", "1|1.1|1.2");
    is("seq 0 0.25 1", "0|0.25|0.5|0.75|1");
    is("seq -1 0.5 0.5", "-1|-0.5|0|0.5");

    // A negative operand is an operand, not a bundle of flags.
    is("seq -3 -1", "-3|-2|-1");
    is("seq -- -3 -1", "-3|-2|-1");
    is("seq -1", ""); // one operand, and 1 to -1 by +1 is empty

    // -s and -t, both unescaped.
    is("seq -s , 1 3", "1,2,3");
    is("seq -s '' 1 3", "123");
    is("seq -s '-->' 1 3", "1-->2-->3");
    is("seq -t END 1 3", "1|2|3|END");
    is("seq -s , -t '!' 1 3", "1,2,3,!");
    is("seq -s 5 1 3", "15253"); // a separator that is itself a number
    is("seq -s5 1 3", "15253");

    // -w, and the manual's own example.
    is("seq -w 8 11", "08|09|10|11");
    is("seq -w 1 10 | head -n 2", "01|02");
    is("seq -w 0 .05 .1", "0.00|0.05|0.10");
    is("seq -w 1 0.5 3", "1.0|1.5|2.0|2.5|3.0");
    is("seq -w -1 1", "-1|00|01"); // the zero pad goes inside the sign
    is("seq -w -w 1 10 | head -n 1", "01"); // a second -w is not a space pad

    // -f, which is printf's conversion and nothing else. It beats -w either
    // way round.
    is("seq -f %.2f 1 3", "1.00|2.00|3.00");
    is("seq -f %05.1f -1 1", "-01.0|000.0|001.0");
    is("seq -f 'x%gy' 1 2", "x1y|x2y");
    is("seq -f '%%%g' 1 2", "%1|%2");
    is("seq -f 'foo%g' 1 2", "foo1|foo2");
    is("seq -f \"%'g\" 1 3", "1|2|3"); // grouping, with no locale to group by
    is("seq -w -f %g 1 3", "1|2|3");
    is("seq -f %g -w 1 3", "1|2|3");

    // Validated before and after unescaping: an escape must not manufacture a
    // conversion, nor a second one. \45 is a per cent.
    is("seq -f foo 1 2 2>&1 | head -n 1", "seq: not a format: foo");
    is("seq -f '\\45g' 1 2 2>&1 | head -n 1", "seq: not a format: \\45g");
    is("seq -f '%g\\45g' 1 2 2>&1 | head -n 1", "seq: not a format: %g\\45g");
    is("seq -f %d 1 2 2>&1 | head -n 1", "seq: not a format: %d");
    is("seq -f '%g%g' 1 2 2>&1 | head -n 1", "seq: not a format: %g%g");
    is("seq -f '%1000f' 1 1 2>&1 | head -n 1", "seq: not a format: %1000f");

    // Through a real pipe, and out the far end of one that hangs up.
    is("seq 1 5 | wc", "5 5 10");
    is("seq 100000 | wc", "100000 100000 588895");
    is("seq 1000000 | head -n 2", "1|2");

    // What is asked for, and what is got wrong.
    is("seq --help", USAGE);
    is("seq 2>&1 | head -n 1", "Usage:");
    is("seq x 3 2>&1 | head -n 1", "seq: not a number: x");
    is("seq 1e 3 2>&1 | head -n 1", "seq: not a number: 1e");
    is("seq 1 inf 2>&1 | head -n 1", "seq: not a finite number: inf");
    is("seq -q 3 2>&1 | head -n 1", "seq: bad option: q");
    is("seq -f 2>&1 | head -n 1", "seq: needs a value: f");
    is("seq -q > /dev/null 2>&1; echo $?", "2"); // a bad option is 2

    at(); // the session is cumulative: leave the clock past the last line
}
