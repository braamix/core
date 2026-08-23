// /bin/cp: the copy proc/io.h holds, which /bin/mv falls back to wherever the
// store will not rename.
// Part of the smoke suite; test/run.mjs runs the cases in order and
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

    // Several sources need a directory to land in.
    is("cp a n c", "usage: cp [-r] [-fi] [-n] <src>... <dir>");
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
