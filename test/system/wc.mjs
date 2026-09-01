// /bin/wc: lines, words, bytes, characters and the longest line, per file and
// as a total. The in-wasm suite cannot run a program, so this is the whole of
// the coverage. Part of the system suite; test/run.mjs runs the cases in order
// and doc/Testing.md has the rules they run by.

import { counts, shows } from "./harness.mjs";

const { at, is, line } = shows(14120);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    wc [-Lclmw] [<file>...]|" +
    "Options:|" +
    "    -l    count lines|" +
    "    -w    count words|" +
    "    -c    count bytes, undoing an earlier -m|" +
    "    -m    count characters, undoing an earlier -c|" +
    "    -L    the longest line's length";

export function check() {
    line("mkdir /home/wq");
    line("cd /home/wq");
    line("echo 'a b' > one");
    line("echo hello > two; echo world >> two");
    line("echo привет > u");
    line("echo -n frag > f");

    // The default is -lwc, and stdin gets no name where a file does.
    is("wc < one", counts(1, 2, 4));
    is("echo 'a b' | wc", counts(1, 2, 4));
    is("wc one", counts(1, 2, 4, "one"));

    // Each column alone, and the order fixed however the flags are written.
    is("wc -l two", counts(2, "two"));
    is("wc -w two", counts(2, "two"));
    is("wc -c two", counts(12, "two"));
    is("wc -wl two", counts(2, 2, "two"));
    is("wc -lw two", counts(2, 2, "two"));

    // -m is characters and -c is bytes, which is the whole of the difference:
    // six Cyrillic letters and a newline against twelve bytes and a newline.
    is("wc -m u", counts(7, "u"));
    is("wc -c u", counts(13, "u"));
    is("wc -cm u", counts(7, "u"));  // -m cancels an earlier -c
    is("wc -mc u", counts(13, "u")); // and -c an earlier -m

    // -L is the longest line, without its newline, and it does not put the
    // default columns back.
    is("wc -L two", counts(5, "two"));
    is("wc -lL two", counts(2, 5, "two"));
    is("wc -mL u", counts(7, 6, "u")); // characters under -m, as bytes otherwise
    is("wc -cL u", counts(13, 12, "u"));

    // A final fragment with no newline is a line everywhere else in this tree,
    // so it is one here: BSD folds the running length in only at a newline and
    // reports 0 for this.
    is("wc -L f", counts(4, "f"));
    is("wc f", counts(0, 1, 4, "f")); // and it is still not a line to -l

    // Several files get a row each and a total; one file gets no total. The
    // total's -L column is the longest of all of them, not a sum.
    is("wc one two", counts(1, 2, 4, "one") + "|" + counts(2, 2, 12, "two") + "|" +
        counts(3, 4, 16, "total"));
    is("wc -L one two", counts(3, "one") + "|" + counts(5, "two") + "|" + counts(5, "total"));

    // A file that will not open is a diagnostic and a status, and the rest are
    // still counted — but it still made the operands more than one, so the
    // total prints, as BSD's does.
    is("wc nope one", "wc: nope: not found|" + counts(1, 2, 4, "one") +
        "|" + counts(1, 2, 4, "total"));
    is("wc nope one > /dev/null 2>&1; echo $?", "1");
    is("wc one > /dev/null; echo $?", "0");

    // A directory never opens, so there is nothing to read and nothing to say
    // beyond why.
    is("wc /home", "wc: /home: is a directory");

    is("wc -q 2>&1 | head -n 1", "wc: bad option: q");
    is("wc -h", USAGE);

    line("cd /");
    line("rm -r /home/wq");
    at();
}
