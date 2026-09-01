// /bin/cp: the copy proc/io.h holds, which /bin/mv falls back to wherever the
// store will not rename.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(13900);

export function check() {
    // Relative names throughout: the screen is 60 columns and a command that
    // wraps puts its own tail in the output.
    line("mkdir /home/cp");
    line("cd /home/cp");
    line("mkdir d");
    line("echo one > a");
    line("echo two > d/b");

    // A file to a new name, and into a directory that exists.
    is("cp a c; cat c", "one");
    is("cp a d; cat d/a", "one");

    // The source is still there — the whole difference from mv.
    is("cat a", "one");

    // A directory needs -r, and says so rather than copying nothing.
    is("cp d e", "cp: d: is a directory");
    is("cp -r d e; cat e/b", "two");

    // -n declines an existing destination and still succeeds; without it the
    // destination is taken.
    line("echo new > n");
    is("cp -n n c; cat c", "one");
    is("cp n c; cat c", "new");

    // A link is recreated rather than followed: copy_tree descends on
    // SYS_KIND_DIR alone, so no cycle guard is needed.
    line("ln -s /home/cp/a d/l");
    line("cp -r d f");
    is("test -h f/l && cat f/l", "one");

    // A destination that already holds the copy is merged into rather than
    // removed and remade: a name only it has survives. The link is replaced
    // rather than refused, so a second copy is not an error either.
    line("mkdir m");
    line("cp -r d m");
    line("echo keep > m/d/keep");
    is("cp -r d m; cat m/d/keep m/d/b", "keep|two");
    is("test -h m/d/l && cat m/d/l", "one");

    // A file where a directory must go is told apart from a directory that is
    // already there: the stat make_dir's Err(Exists) does not give.
    line("mkdir d/sub");
    line("echo x > m/d/sub");
    is("cp -r d m", "cp: d: already exists");

    // And a directory where a file must go, which is the open's answer.
    line("rm m/d/sub m/d/b");
    line("mkdir m/d/b");
    is("cp -r d m", "cp: d: is a directory");
    line("rm -r m/d/b d/sub");

    // -n and -i still decide at the named destination, not per file inside it.
    is("cp -rn d m; cat m/d/keep", "keep");
    is("echo n | cp -ri d m", "overwrite /home/cp/m/d?");

    // -i on a file: the answer decides, and -f overrides the ask, as in mv.
    // Only the refusal above had ever run, though cp.cpp asks on the same
    // `!force && ask` mv does — doc/TODO.md D3 is why that is worth pinning.
    line("echo old > y1");
    line("echo fresh > y2");
    is("echo n | cp -i y2 y1", "overwrite /home/cp/y1?");
    is("cat y1", "old");
    is("echo y | cp -i y2 y1", "overwrite /home/cp/y1?");
    is("cat y1", "fresh");
    line("echo old > y1");
    is("echo n | cp -fi y2 y1", "");
    is("cat y1", "fresh");
    line("rm y1 y2");

    // Bare is asking rather than getting it wrong: 0, and nothing removed.
    is("rm; echo $?", "Usage:|    rm [-r] <path>...|Options:|" +
                      "    -r    remove directories, and what is in them|0");
    is("cat a", "one");

    // Several sources need a directory to land in.
    is("cp a n c", "Usage:|    cp [-r] [-fi] [-n] <src> <dst>|    cp [-r] [-fi] [-n] <src>... <dir>|Options:|" +
                "    -r    copy directories, and what is in them|" +
                "    -f    replace what stands at the destination|" +
                "    -i    ask before replacing it|" +
                "    -n    keep it, and copy nothing over it");
    line("mkdir g");
    line("cp a n g");
    is("cat g/a g/n", "one|new");

    // Onto itself, and into itself: refused rather than looping.
    is("cp a a", "cp: a: invalid");
    is("cp -r d d/in", "cp: d: cannot copy a directory into itself");

    // A source that is not there is reported, and the status says so.
    is("cp nope z; echo $?", "cp: nope: not found|1");

    line("cd /home"); // the session is cumulative: leave the cwd as it was
}
