// Two terminals in one kernel: a second grid, a second console, a second shell,
// and the one filesystem underneath both.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.
//
// After `entry`, and before `language`, which exits terminal 0's shell for
// good. The second shell is exited again at the end: `net.peak` and `others()`
// count instances, and a case that left one running would move both.

import { CTRL, fail, net, output, press, regrid, resize, row, rows, run, screen, submit } from
    "./harness.mjs";

// The terminal this case makes. Terminal 0 is the one every other case drives.
const T = 1;

// Ticks until `want` reads true of terminal T's grid, or gives up. A second
// shell is an exec and a spawn, so it takes several rounds of the host owing a
// process a step.
function settle(now, want, why) {
    for (let i = 0; i < 200; i++) {
        run(now + i);
        if (want(screen(T)))
            return screen(T);
    }
    return fail(why);
}

const prompted = (s) => rows(s).some((line) => line.endsWith("$"));

export function check() {
    // Resizing a terminal nothing has named is what makes one: the kernel
    // spawns its pump, and init a shell of its own on it.
    if (screen(0).cols === 0)
        fail("terminal 0 has no grid, so the case before this one left none");

    regrid(48, 12, "the second terminal would not size", T);

    let s = screen(T);
    if (s.cols !== 48 || s.rows !== 12)
        fail(`terminal ${T} came up ${s.cols}x${s.rows}, expected 48x12`);

    // Its own grid, at its own address.
    if (screen(0).cells === s.cells)
        fail("both terminals point at one grid");

    s = settle(30000, prompted, `terminal ${T} never reached a prompt`);
    if (!rows(s).some((line) => line.startsWith("braam ")))
        fail(`terminal ${T} came up without a banner: ${JSON.stringify(rows(s))}`);

    // A command on each, and the output lands on the grid it was typed on.
    submit("clear", 30300, T);
    let out = output(submit("echo lower", 30400, T)).join("|");
    if (out !== "lower")
        fail(`terminal ${T} printed ${JSON.stringify(out)}, expected "lower"`);
    if (rows(screen(0)).some((line) => line.includes("lower")))
        fail("what was typed on the second terminal reached the first");

    submit("clear", 30500);
    out = output(submit("echo upper", 30600)).join("|");
    if (out !== "upper")
        fail(`terminal 0 printed ${JSON.stringify(out)}, expected "upper"`);
    if (rows(screen(T)).some((line) => line.includes("upper")))
        fail("what was typed on the first terminal reached the second");

    // One kernel is one filesystem: no second store, no lock to lose.
    submit("echo shared > /home/dual.t", 30700, T);
    submit("clear", 30800);
    out = output(submit("cat /home/dual.t", 30900)).join("|");
    if (out !== "shared")
        fail(`the first terminal read ${JSON.stringify(out)} of the second's file`);

    // `uname -a` names the terminal it runs on, not terminal 0's: the geometry
    // comes from Sys::Tty and the rest from /proc/host.
    submit("clear", 30920, T);
    const mine = output(submit("uname -a", 30930, T));
    if (!mine.includes(`screen   ${screen(T).cols}x${screen(T).rows}`))
        fail(`terminal ${T} named ${JSON.stringify(mine)}, expected its own 48x12`);
    submit("clear", 30940);
    const other = output(submit("uname -a", 30950));
    if (!other.includes(`screen   ${screen(0).cols}x${screen(0).rows}`))
        fail(`terminal 0 named ${JSON.stringify(other)}, expected its own grid`);
    if (mine.join("|") === other.join("|"))
        fail(`both terminals reported one geometry: ${JSON.stringify(mine)}`);

    // ^C is the console's, and each terminal has one. It cancels what is in
    // front of this terminal and leaves the other terminal's prompt alone.
    submit("clear", 31000, T);
    const before = rows(screen(0)).join("|");
    submit("sleep 30", 31100, T);
    press("c".codePointAt(0), CTRL, T);
    s = settle(31200, prompted, `^C left terminal ${T} without a prompt`);
    if (!rows(s).some((line) => line.includes("^C")))
        fail(`^C was not echoed on terminal ${T}: ${JSON.stringify(rows(s))}`);
    if (rows(screen(0)).join("|") !== before)
        fail("^C on the second terminal disturbed the first");

    // A terminal the kernel has no room for is refused rather than made, and a
    // keystroke for one that does not exist is dropped rather than queued.
    if (resize(20, 5, 9) !== 0)
        fail("a terminal id past TERM_MAX was accepted");
    if (press("x".codePointAt(0), 0, 9) !== 0)
        fail("a keystroke for a terminal that does not exist was taken");

    // The second shell exits and is not replaced — a shell that ended on its
    // own terms is the end of that session, terminal 0's rule exactly. The
    // instance goes with it, which is what leaves the count where this case
    // found it.
    const live = net.proc.live();
    submit("exit", 31400, T);
    settle(31500, () => net.proc.live() < live, `terminal ${T}'s shell did not exit`);
    s = screen(T);
    if (!rows(s).some((line) => line.includes("the shell exited")))
        fail(`terminal ${T} did not report the exit: ${JSON.stringify(rows(s))}`);

    // Back to the geometry the next case expects on terminal 0.
    regrid(60, 16, "the first terminal would not size back");
    if (row(screen(T), 0) === undefined)
        fail(`terminal ${T} lost its grid`);
}
