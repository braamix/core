// /bin/find: proc/io.h's TreeWalk, and the boolean expression over it. The
// in-wasm suite cannot run a program, so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(13920);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    find <path>... [<expression>]|Expression:|" +
    "    -name <pat>    the name matches: * ? [a-z] [!a-z]|" +
    "    -type f|d|l    a file, a directory, or a link|" +
    "    -newer <file>  changed more recently than <file>|" +
    "    -print         print the path; what happens anyway|" +
    "    ! ( )          not, and grouping|" +
    "    -a  -o         and, or; -a is what a space means";

export function check() {
    // Written oldest first: every write moves the fake store's clock, so
    // -newer has a real ordering to assert.
    line("mkdir /home/fd");
    line("echo a > /home/fd/a");
    line("mkdir /home/fd/d");
    line("echo c > /home/fd/d/c");
    line("mkdir /home/fd/d/e");
    line("echo x > /home/fd/d/e/x");
    line("echo hidden > /home/fd/.dot");
    line("ln -s /home/fd/a /home/fd/l");
    line("ln -s /home/fd/d /home/fd/toward");
    line("echo b > /home/fd/b"); // last written, so the newest

    // Pre-order, the operand first, and each directory before what is in it.
    // list_dir delivers name order, so the whole listing is assertable.
    const all = "/home/fd|/home/fd/.dot|/home/fd/a|/home/fd/b|/home/fd/d|" +
                "/home/fd/d/c|/home/fd/d/e|/home/fd/d/e/x|/home/fd/l|/home/fd/toward";
    is("find /home/fd", all);

    // A trailing slash on the operand does not double in what is reported.
    is("find /home/fd/", all.replace("/home/fd|", "/home/fd/|"));

    // No expression means everything, and -print alone means the same thing —
    // and prints once, not twice.
    is("find /home/fd -print", all);
    is("find /home/fd -print -print", all);

    // -name is the shell's matcher over the last part of the path, with no
    // leading-dot rule: a dotfile is matched by a plain *.
    is("find /home/fd -name 'd*'", "/home/fd/d");
    is("find /home/fd -name '*t'", "/home/fd/.dot"); // no leading-dot rule
    is("find /home/fd -name '?'", "/home/fd/a|/home/fd/b|/home/fd/d|/home/fd/d/c|" +
                                  "/home/fd/d/e|/home/fd/d/e/x|/home/fd/l");
    is("find /home/fd -name '[ab]'", "/home/fd/a|/home/fd/b");
    is("find /home/fd -name '[!ab]'",
       "/home/fd/d|/home/fd/d/c|/home/fd/d/e|/home/fd/d/e/x|/home/fd/l");
    is("find /home/fd -name fd", "/home/fd"); // the operand is tested too

    // The three kinds. A link is never followed, so toward -> d contributes
    // nothing under it and the walk cannot loop.
    is("find /home/fd -type f",
       "/home/fd/.dot|/home/fd/a|/home/fd/b|/home/fd/d/c|/home/fd/d/e/x");
    is("find /home/fd -type d", "/home/fd|/home/fd/d|/home/fd/d/e");
    is("find /home/fd -type l", "/home/fd/l|/home/fd/toward");

    // -newer, against a file written in the middle of the tree above. A
    // directory keeps no mtime through this store, so it is newer than nothing.
    is("find /home/fd -newer /home/fd/d/c -type f",
       "/home/fd/.dot|/home/fd/b|/home/fd/d/e/x");
    is("find /home/fd -newer /home/fd/b", ""); // nothing is newer than the last
    is("find /home/fd -newer /home/fd/a -type d", ""); // a directory keeps none
    // Followed, so the reference is what the link points at: toward -> d, a
    // directory, whose mtime is 0 — and everything is newer than that.
    is("find /home/fd -newer /home/fd/toward -name b", "/home/fd/b");
    is("find /home/fd -newer /home/fd/nope; echo $?", "find: /home/fd/nope: not found|1");

    // The operators. Juxtaposition is -a, ! binds tighter than either, and the
    // parentheses have to be quoted past the shell.
    is("find /home/fd -type f -name 'b'", "/home/fd/b");
    is("find /home/fd -type f -a -name 'b'", "/home/fd/b");
    is("find /home/fd -name a -o -name b", "/home/fd/a|/home/fd/b");
    is("find /home/fd ! -type f ! -type l", "/home/fd|/home/fd/d|/home/fd/d/e");
    is("find /home/fd '(' -name a -o -name b ')' -o -name c", "/home/fd/a|/home/fd/b|/home/fd/d/c");
    // -a binds tighter than -o: `a` or (`c` and a file).
    is("find /home/fd -name a -o -name c -type f", "/home/fd/a|/home/fd/d/c");
    is("find /home/fd '(' -name a -o -name c ')' -type d", "");
    is("find /home/fd ! ! -name a", "/home/fd/a");

    // A positioned -print is what suppresses the implicit one, so the second
    // arm of a -o is the only thing that prints here.
    is("find /home/fd -name 'd*' -o -name a -print", "/home/fd/a");

    // Several operands, in the order given, each walked whole.
    is("find /home/fd/d /home/fd/a", "/home/fd/d|/home/fd/d/c|/home/fd/d/e|" +
                                     "/home/fd/d/e/x|/home/fd/a");

    // A relative operand builds against the cwd, and a plain file is itself.
    line("cd /home/fd");
    is("find . -name c", "./d/c");
    is("find d/e", "d/e|d/e/x");
    is("find a", "a");
    is("find a b -type f", "a|b");
    line("cd /home");

    // A filesystem the kernel generates rather than the store: /proc keeps no
    // mtime at all, and a walk over it is the same walk.
    is("find /proc -name host", "/proc/host");

    // A pipeline's input, so the output is not the terminal's.
    is("find /home/fd -type f | wc", "5 5 64");

    // What is asked for, and what is got wrong.
    is("find --help", USAGE);
    is("find /home/fd -nope 2>&1 | head -n 2", "find: bad option: -nope|Usage:");
    is("find /home/fd -name 2>&1 | head -n 1", "find: needs an operand: -name");
    is("find /home/fd -type q 2>&1 | head -n 1", "find: -type takes f, d or l: q");
    is("find /home/fd '(' -name a 2>&1 | head -n 1", "find: expected )");
    is("find /home/fd -name a ')' 2>&1 | head -n 1", "find: expected an operator: )");
    // Asking is stdout and 0; a bad expression is stderr and 2, and no path
    // at all is the usage block alone.
    is("find /home/fd -nope > /dev/null 2>&1; echo $?", "2");
    is("find -name a 2>&1 | head -n 2", "Usage:|    find <path>... [<expression>]");
    is("find; echo $?", `${USAGE}|0`);

    // A path that is not there is reported, the rest is still walked, and the
    // status says one of them failed.
    is("find /home/fd/nope /home/fd/a; echo $?", "find: /home/fd/nope: not found|/home/fd/a|1");

    line("rm -r /home/fd");
    at(); // the session is cumulative: leave the clock past the last line
}
