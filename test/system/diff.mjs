// /bin/diff: the lines that differ, in three formats, over one file pair or
// two trees. The in-wasm suite has the algorithm and the emitters
// (test/unit/test_diff.cpp); what it cannot do is run the program, so the
// reading, the walk and the statuses are here.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { counts, shows } from "./harness.mjs";

const { at, is, line } = shows(14180);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|" +
    "    diff [-c|-u|-q] [-C <n>|-U <n>] [-bBiNrw]|" +
    "         [-L <label>] <file1> <file2>|" +
    "Options:|" +
    "    -c -u -q  context, unified, or only whether they differ|" +
    "    -C -U     the same two formats, with a count of lines|" +
    "    -r        walk two directories, and the trees under them|" +
    "    -N        a file only one side has counts as empty|" +
    "    -L        the name to print for a file; twice for both|" +
    "    -i        upper and lower case compare equal|" +
    "    -b -w     -b folds runs of blanks, -w drops them all|" +
    "    -B        a change of blank lines alone does not count|" +
    "Both files are held in memory.";

export function check() {
    line("mkdir /home/df");
    line("cd /home/df");
    line("for w in a b c d e; do echo $w; done > f1");
    line("for w in a B c d e; do echo $w; done > f2");

    // The same file says nothing and is 0; a change is one hunk and is 1.
    is("diff f1 f1; echo $?", "0");
    is("diff f1 f2; echo $?", "2c2|< b|---|> B|1");

    // An insertion and a deletion are `a` and `d`, and name the line in
    // front of the gap rather than a line that is not there.
    line("echo one > s1");
    line("echo one > s2; echo two >> s2");
    is("diff s1 s2", "1a2|> two");
    is("diff s2 s1", "2d1|< two");

    // Several hunks in one file, in line order.
    line("for w in a b c d e f g h; do echo $w; done > m1");
    line("for w in a c d X e f g h i; do echo $w; done > m2");
    is("diff m1 m2", "2d1|< b|4a4|> X|8a9|> i");

    // Unified: one @@ for the three, since they fall inside 2*3 lines of
    // each other, and the counts are the whole span.
    is("diff -u m1 m2 | tail -n 11",
       "@@ -1,8 +1,9 @@| a|-b| c| d|+X| e| f| g| h|+i");
    is("diff -u s1 s2 | tail -n 3", "@@ -1 +1,2 @@| one|+two");
    is("diff -U 0 s1 s2 | tail -n 2", "@@ -1,0 +2 @@|+two");

    // Context: the two halves, and `!` where a line was replaced.
    is("diff -c f1 f2 | tail -n 13",
       "***************|*** 1,5 ****|  a|! b|  c|  d|  e|" +
       "--- 1,5 ----|  a|! B|  c|  d|  e");
    is("diff -C 1 f1 f2 | tail -n 9",
       "***************|*** 1,3 ****|  a|! b|  c|--- 1,3 ----|  a|! B|  c");

    // The two headers, and -L in place of a name. The stamp is a mtime, so
    // only the shape of it can be pinned.
    is("diff -u -L old -L new f1 f2 | head -n 2 | cut -c 1-7",
       "--- old|+++ new");
    is("diff -c -L old -L new f1 f2 | head -n 2 | cut -c 1-7",
       "*** old|--- new");
    is("diff -u f1 f2 | head -n 1 | cut -c 1-6", "--- f1");

    // -q is the answer alone, whichever way the files differ.
    is("diff -q f1 f2; echo $?", "Files f1 and f2 differ|1");
    is("diff -q f1 f1; echo $?", "0");

    // What the comparison may ignore.
    line("echo 'a  b' > w1; echo 'a b' > w2; echo ab > w3");
    is("diff -b w1 w2; echo $?", "0");
    is("diff -b w2 w3; echo $?", "1c1|< a b|---|> ab|1");
    is("diff -w w2 w3; echo $?", "0");
    is("diff -i f1 f2; echo $?", "0");
    is("diff f1 f2 | wc -l", counts(4)); // and without -i, a hunk

    // A tab cannot be typed at this prompt, so /bin/tr writes one.
    line("echo 'x y' | tr ' ' '\\t' > t1; echo 'x y' > t2");
    is("diff -b t1 t2; echo $?", "0");
    is("diff -w t1 t2; echo $?", "0");

    // -B: a change that is only blank lines does not count, and one that
    // carries anything else still does.
    line("echo x > b1; echo y >> b1");
    line("echo x > b2; echo >> b2; echo y >> b2");
    is("diff b1 b2; echo $?", "1a2|>|1");
    is("diff -B b1 b2; echo $?", "0");
    line("echo x > b3; echo >> b3; echo z >> b3");
    is("diff -B b1 b3; echo $?", "2c2,3|< y|---|>|> z|1");

    // A file that stops without a newline is not the same as one that does
    // not, and the marker says which side it was.
    line("echo -n nl > n1; echo nl > n2");
    is("diff n1 n2",
       "1c1|< nl|\\ No newline at end of file|---|> nl");
    is("diff n2 n1",
       "1c1|< nl|---|> nl|\\ No newline at end of file");
    is("diff n1 n1; echo $?", "0");

    // A NUL anywhere makes it binary, and then only the fact is reported.
    line("cp /bin/true bin1");
    is("diff bin1 f1; echo $?", "Binary files bin1 and f1 differ|1");

    // Two directories: the names in one alone, the pairs that differ, and
    // the subdirectory that is only descended under -r.
    line("mkdir d1 d2 d1/s d2/s");
    line("echo a > d1/f; echo b > d2/f");
    line("echo same > d1/g; echo same > d2/g");
    line("echo p > d1/h; echo q > d2/k");
    line("echo y > d1/s/x; echo z > d2/s/x");
    is("diff d1 d2; echo $?",
       "diff d1/f d2/f|1c1|< a|---|> b|Only in d1: h|Only in d2: k|" +
       "Common subdirectories: d1/s and d2/s|1");
    is("diff -q -r d1 d2",
       "Files d1/f and d2/f differ|Only in d1: h|Only in d2: k|" +
       "Files d1/s/x and d2/s/x differ");

    // The banner carries the options, and -N diffs against nothing rather
    // than reporting a name.
    is("diff -q -r -N d1 d2 | head -n 3",
       "Files d1/f and d2/f differ|Files d1/h and d2/h differ|" +
       "Files d1/k and d2/k differ");
    is("diff -N d1 d2 | head -n 8",
       "diff -N d1/f d2/f|1c1|< a|---|> b|diff -N d1/h d2/h|1d0|< p");

    // A directory on one side names the same leaf inside it.
    is("diff d1 d2/f", "1c1|< a|---|> b");
    is("diff d1/f d2", "1c1|< a|---|> b");

    // A file against a directory of the same name, inside a walk.
    line("mkdir d1/j; echo j > d2/j");
    is("diff -r d1 d2 | grep directory",
       "File d1/j is a directory while file d2/j is a regular file");
    is("diff -r d2 d1 | grep directory",
       "File d2/j is a regular file while file d1/j is a directory");

    // Stdin is one of the two, and the label it takes is "-".
    is("cat f2 | diff f1 -", "2c2|< b|---|> B");

    // Far past one line at a time: 2,000 lines with one dropped and one
    // added, which is two hunks and nothing between them.
    line("seq 1 2000 > q1; seq 2 2001 > q2");
    is("diff q1 q2", "1d0|< 1|2000a2000|> 2001");
    is("diff q1 q1; echo $?", "0");

    // Two files with nothing in common at all: the cost ceiling, and the
    // whole of both sides in one hunk.
    line("seq 1 400 > r1; seq 401 800 > r2");
    is("diff r1 r2 | wc -l", counts(802));
    is("diff r1 r2 | head -n 1", "1,400c1,400");

    // An empty file on either side.
    line("echo -n > z1");
    is("diff z1 s1; echo $?", "0a1|> one|1");
    is("diff s1 z1; echo $?", "1d0|< one|1");
    is("diff z1 z1; echo $?", "0");

    // What is asked for, and what is got wrong.
    is("diff --help", USAGE);
    is("diff f1 2>&1 | head -n 1", "diff: two files are needed");
    is("diff -c -u f1 f2 2>&1 | head -n 1",
       "diff: only one of -c, -u and -q");
    is("diff -U x f1 f2 2>&1 | head -n 1", "diff: not a line count: x");
    is("diff -L a -L b -L c f1 f2 2>&1 | head -n 1",
       "diff: at most two labels: c");
    is("diff -Z f1 f2 2>&1 | head -n 1", "diff: bad option: Z");
    is("diff -U 2>&1 | head -n 1", "diff: needs a value: U");
    is("diff -Z f1 f2 > /dev/null 2>&1; echo $?", "2"); // a bad option is 2
    is("diff f1 nope 2>&1; echo $?", "diff: nope: not found|2");

    line("cd /home");
    line("rm -r /home/df");
    at(); // the session is cumulative: leave the clock past the last line
}
