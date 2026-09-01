// mv, both halves: a store rename and the copy that stands in for one.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows, submit } from "./harness.mjs";

export function check() {
    // mv, both halves: Sys::Rename where the store can move a name, and the
    // copy-and-remove fallback where it cannot. The fake answers Unsupported
    // for a directory exactly as OPFS does, so both paths run here.
    {
        const t = shows(13300, 0.01);

        submit("mkdir /home/m", t.at());
        submit("mkdir /home/m/d", t.at());
        submit("echo one > /home/m/a", t.at());
        submit("echo two > /home/m/b", t.at());

        // A rename moves the name and nothing else: the bytes and the stamp
        // are the ones `a` had, which is what a copy could not manage.
        t.is("ls -l /home/m/a", "file 4 Jun 19 20:14 /home/m/a");
        submit("mv /home/m/a /home/m/c", t.at());
        t.is("cat /home/m/c", "one");
        t.is("ls -l /home/m/c", "file 4 Jun 19 20:14 /home/m/c");
        t.is("ls /home/m", "b   c   d/");

        // The destination is replaced, and a move onto itself is a no-op
        // rather than the removal that would leave nothing behind.
        submit("mv /home/m/b /home/m/c", t.at());
        t.is("cat /home/m/c", "two");
        submit("mv /home/m/c /home/m/c", t.at());
        t.is("cat /home/m/c", "two");

        // Several sources, the last operand being the directory they go in.
        submit("echo x > /home/m/x", t.at());
        submit("echo y > /home/m/y", t.at());
        submit("mv /home/m/x /home/m/y /home/m/d", t.at());
        t.is("cat /home/m/d/x /home/m/d/y", "x|y");

        // A link moves as itself: the target is not read and not rewritten.
        submit("ln -s /home/m/c /home/m/link", t.at());
        submit("mv /home/m/link /home/m/moved", t.at());
        t.is("ls -l /home/m/moved", "link 9 Jun 19 20:14 /home/m/moved@ -> /home/m/c");

        // A directory: the store will not move one, so this is the copy path.
        // The tree arrives whole and the source is gone.
        submit("mv /home/m/d /home/m/e", t.at());
        t.is("cat /home/m/e/x /home/m/e/y", "x|y");
        t.is("ls /home/m", "c       e/      moved@");

        // Into itself is refused rather than copied for ever.
        t.is("mv /home/m/e /home/m/e/sub", "mv: /home/m/e: cannot move a directory into itself");

        // Diagnostics. A read-only mount refuses before anything is copied.
        t.is("mv /home/m/gone /home/m/z", "mv: /home/m/gone: not found");
        t.is("mv /home/m/c /proc/x", "mv: /home/m/c: permission denied");
        t.is("mv /home/m/e /home/m/c", "mv: /home/m/e: not a directory");
        t.is("mv /home/m/c /home/m/moved /home/m/e/x", "Usage:|    mv [-fi] <src> <dst>|    mv [-fi] <src>... <dir>|Options:|" +
                "    -f    replace what stands at the destination|" +
                "    -i    ask before replacing it");

        // -i asks before it clobbers, and the answer decides.
        t.is("echo n | mv -i /home/m/c /home/m/e/x", "overwrite /home/m/e/x?");
        t.is("cat /home/m/e/x", "x");
        t.is("echo y | mv -i /home/m/c /home/m/e/x", "overwrite /home/m/e/x?");
        t.is("cat /home/m/e/x", "two");

        // -f overrides -i, as it does in v7: nothing is asked.
        submit("echo three > /home/m/f", t.at());
        t.is("echo n | mv -fi /home/m/f /home/m/e/x", "");
        t.is("cat /home/m/e/x", "three");

        // `mv a b` where `b/a` is an empty directory: rename(2) replaces one,
        // and the copy path performs it since no store moves a directory.
        submit("mkdir /home/m/g", t.at());
        submit("mkdir /home/m/g/sub", t.at());
        submit("mkdir /home/m/i", t.at());
        submit("mkdir /home/m/i/g", t.at());
        submit("mv /home/m/g /home/m/i", t.at());
        t.is("ls /home/m/i/g", "sub/");
        t.is("ls /home/m/g", "ls: /home/m/g: not found");

        // One with children is refused, and refused before anything is
        // removed: the copy path clears the destination first.
        submit("mkdir /home/m/j", t.at());
        submit("mkdir /home/m/j/i", t.at());
        submit("mkdir /home/m/j/i/keep", t.at());
        t.is("mv /home/m/i /home/m/j", "mv: /home/m/i: directory not empty");
        t.is("ls /home/m/j/i", "keep/");
        t.is("ls /home/m/i", "g/");

        submit("rm -r /home/m", t.at());
    }
}
