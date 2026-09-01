// /bin/du: proc/io.h's TreeWalk summed, and the post-order the walk is not.
// The in-wasm suite cannot run a program, so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { chdir, shows } from "./harness.mjs";

const { at, is, line } = shows(13925);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    du [-a | -s] [-hk] [<path>...]|Options:|" +
    "    -a    a line for every file, not only every directory|" +
    "    -s    one line per argument, and nothing under it|" +
    "    -h    scale the sizes for a reader|" +
    "    -k    kibibytes, the default, and what undoes -h";

// The count's column: right-aligned in seven, then two spaces. A tab is what
// du(1) writes and the grid has no tab stop for (Concept.md §2.3).
const at7 = (n, path) => `${String(n).padStart(7)}  ${path}`;

export function check() {
    // Sizes chosen so every number below is arithmetic and not a reading:
    // a, b, c, x, y, z are two bytes each and cost a 512-byte block apiece,
    // big is exactly eight blocks, and the link is the magic plus its target.
    line("mkdir /home/du");
    line("echo a > /home/du/a");
    line("echo b > /home/du/b");
    line("truncate -s 4096 /home/du/big");
    line("mkdir /home/du/d");
    line("echo c > /home/du/d/c");
    line("mkdir /home/du/d/e");
    line("ln -s /home/du/a /home/du/l");
    line("mkdir /home/du/t");
    line("echo x > /home/du/t/x");
    line("echo y > /home/du/t/y");
    line("echo z > /home/du/t/z");

    // Post-order, which the walk is not: a directory after what is in it, and
    // an empty one is nought rather than absent. 15 blocks under the root.
    const tree = [at7(0, "/home/du/d/e"), at7(1, "/home/du/d"),
                  at7(2, "/home/du/t"), at7(8, "/home/du")].join("|");
    is("du /home/du", tree);

    // The count is in blocks until it is printed: t's three two-byte files are
    // three blocks and print as 2K, where rounding each first would say 3.
    is("du -a /home/du/t", [at7(1, "/home/du/t/x"), at7(1, "/home/du/t/y"),
                            at7(1, "/home/du/t/z"), at7(2, "/home/du/t")].join("|"));

    // -a is a line per file as well, in the order the walk reaches them.
    is("du -a /home/du", [at7(1, "/home/du/a"), at7(1, "/home/du/b"),
                          at7(4, "/home/du/big"), at7(1, "/home/du/d/c"),
                          at7(0, "/home/du/d/e"), at7(1, "/home/du/d"),
                          at7(1, "/home/du/l"), at7(1, "/home/du/t/x"),
                          at7(1, "/home/du/t/y"), at7(1, "/home/du/t/z"),
                          at7(2, "/home/du/t"), at7(8, "/home/du")].join("|"));

    // -s is the operand's own line and nothing under it.
    is("du -s /home/du", at7(8, "/home/du"));

    // -h scales the bytes those blocks are; -k is the default, and is what
    // undoes an earlier -h.
    is("du -h /home/du", [at7("0B", "/home/du/d/e"), at7("512B", "/home/du/d"),
                          at7("1.5K", "/home/du/t"), at7("7.5K", "/home/du")].join("|"));
    is("du -hks /home/du", at7(8, "/home/du"));
    is("du -kh -s /home/du", at7("7.5K", "/home/du"));

    // A file names itself, which v7's own manual called a bug in v7. A link is
    // the link: not followed, and its size is the magic plus the target.
    is("du /home/du/big", at7(4, "/home/du/big"));
    is("du /home/du/l", at7(1, "/home/du/l"));
    is("du -s /home/du/l", at7(1, "/home/du/l"));

    // Several operands, in the order given, each walked whole.
    is("du -s /home/du/t /home/du/d", [at7(2, "/home/du/t"), at7(1, "/home/du/d")].join("|"));

    // A relative operand builds against the cwd, and no operand at all is `.`.
    line("cd /home/du");
    chdir("/home/du");
    is("du", [at7(0, "./d/e"), at7(1, "./d"), at7(2, "./t"), at7(8, ".")].join("|"));
    is("du -s d", at7(1, "d"));
    line("cd /home");
    chdir("/home");

    // A pipeline's input, so the output is not the terminal's.
    is("du /home/du | wc", "4 8 80");

    // A filesystem the kernel generates rather than the store: the sizes are
    // whatever ProcFs says, so only that the walk gets through it is assertable.
    is("du -s /proc > /dev/null; echo $?", "0");

    // What is asked for, and what is got wrong.
    is("du --help", USAGE);
    is("du -q 2>&1 | head -n 2", "du: bad option: q|Usage:");
    is("du -as /home/du 2>&1 | head -n 1", "du: -a and -s ask for different listings");
    // Asking is stdout and 0; a bad command line is stderr and 2.
    is("du -as /home/du > /dev/null 2>&1; echo $?", "2");
    is("du --help > /dev/null; echo $?", "0");

    // A path that is not there is reported, the rest is still walked, and the
    // status says one of them failed.
    is("du /home/du/nope /home/du/t; echo $?",
       `du: /home/du/nope: not found|${at7(2, "/home/du/t")}|1`);

    line("rm -r /home/du");
    at(); // the session is cumulative: leave the clock past the last line
}
