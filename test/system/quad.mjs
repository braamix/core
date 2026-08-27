// Four terminals in one kernel — TERM_MAX exactly, which is what web/quad.html
// puts on a page: four grids, four consoles, four shells, one filesystem.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.
//
// After `dual`, which leaves its shell on terminal 1 running for this case to
// count, and before `language`, which exits terminal 0's shell for good. All
// three shells above terminal 0 are exited here: `net.peak` and `others()`
// count instances, and one left running would move both.

import { CTRL, fail, net, output, press, regrid, resize, rows, run, screen, submit } from
    "./harness.mjs";

// dual made terminal 1; this case makes the other two.
const NE = 1, SW = 2, SE = 3;
const ALL = [0, NE, SW, SE];

// The clock, strictly increasing: a settle spends 200 ticks, so it takes a
// wider step than a submit.
let now = 32000;
const at = (by = 100) => (now += by);

// Ticks until `want` reads true of terminal `term`'s grid, or gives up. A shell
// is an exec and a spawn, so it takes several rounds of the host owing a
// process a step.
function settle(term, want, why) {
    const base = at(300);
    for (let i = 0; i < 200; i++) {
        run(base + i);
        if (want(screen(term)))
            return screen(term);
    }
    return fail(why);
}

const prompted = (s) => rows(s).some((line) => line.endsWith("$"));
const said = (term, word) => rows(screen(term)).some((line) => line.includes(word));

export function check() {
    if (!prompted(screen(NE)))
        fail(`terminal ${NE} has no shell, so the case before this one exited it`);

    // Resizing a terminal nothing has named is what makes one.
    regrid(40, 10, "the third terminal would not size", SW);
    regrid(44, 11, "the fourth terminal would not size", SE);

    for (const [t, cols, rows_] of [[SW, 40, 10], [SE, 44, 11]]) {
        const s = screen(t);
        if (s.cols !== cols || s.rows !== rows_)
            fail(`terminal ${t} came up ${s.cols}x${s.rows}, expected ${cols}x${rows_}`);
    }

    // Four grids, four addresses.
    const cells = ALL.map((t) => screen(t).cells);
    if (new Set(cells).size !== ALL.length)
        fail(`four terminals do not have four grids: ${cells}`);

    for (const t of [SW, SE]) {
        const s = settle(t, prompted, `terminal ${t} never reached a prompt`);
        if (!rows(s).some((line) => line.startsWith("braam ")))
            fail(`terminal ${t} came up without a banner: ${JSON.stringify(rows(s))}`);
    }

    // Four shells at once, which is the state web/quad.html boots into and the
    // reason `dual` hands this case a running one.
    if (net.proc.live() !== ALL.length)
        fail(`${net.proc.live()} instances for four terminals, expected four shells`);

    // A command on each, and the output lands on the grid it was typed on.
    const words = ["north", "east", "west", "south"]; // by terminal id
    for (const t of ALL) {
        submit("clear", at(), t);
        const out = output(submit(`echo ${words[t]}`, at(), t)).join("|");
        if (out !== words[t])
            fail(`terminal ${t} printed ${JSON.stringify(out)}, expected ${words[t]}`);
    }
    for (const t of ALL)
        for (const other of ALL)
            if (other !== t && said(other, words[t]))
                fail(`what was typed on terminal ${t} reached terminal ${other}`);

    // One kernel is one filesystem, whatever the screen count.
    submit("echo shared > /home/quad.t", at(), SE);
    submit("clear", at(), SW);
    let out = output(submit("cat /home/quad.t", at(), SW)).join("|");
    if (out !== "shared")
        fail(`terminal ${SW} read ${JSON.stringify(out)} of terminal ${SE}'s file`);

    // `uname -g` is the caller's geometry, and four callers have four.
    for (const [t, want] of [[SW, "40x10"], [SE, "44x11"]]) {
        submit("clear", at(), t);
        out = output(submit("uname -g", at(), t)).join("|");
        if (out !== want)
            fail(`terminal ${t} measured ${JSON.stringify(out)}, expected ${want}`);
    }

    // ^C is the console's, and each terminal has one: it cancels what is in
    // front of this terminal and leaves the other three alone.
    submit("clear", at(), SE);
    const before = [0, NE, SW].map((t) => rows(screen(t)).join("|"));
    submit("sleep 30", at(), SE);
    press("c".codePointAt(0), CTRL, SE);
    const s = settle(SE, prompted, `^C left terminal ${SE} without a prompt`);
    if (!rows(s).some((line) => line.includes("^C")))
        fail(`^C was not echoed on terminal ${SE}: ${JSON.stringify(rows(s))}`);
    [0, NE, SW].forEach((t, i) => {
        if (rows(screen(t)).join("|") !== before[i])
            fail(`^C on terminal ${SE} disturbed terminal ${t}`);
    });

    // TERM_MAX is four, so 4 is the first id that does not fit: refused rather
    // than made, and a keystroke for it dropped rather than queued.
    if (resize(20, 5, 4) !== 0)
        fail("a fifth terminal was made; TERM_MAX is 4");
    if (press("x".codePointAt(0), 0, 4) !== 0)
        fail("a keystroke for a terminal that does not exist was taken");

    // Each shell exits and is not replaced — terminal 0's rule exactly. The
    // instances go with them, which is what leaves the count where `dual` and
    // this case found it.
    for (const t of [NE, SW, SE]) {
        const live = net.proc.live();
        submit("exit", at(), t);
        settle(t, () => net.proc.live() < live, `terminal ${t}'s shell did not exit`);
        if (!said(t, "the shell exited"))
            fail(`terminal ${t} did not report the exit: ${JSON.stringify(rows(screen(t)))}`);
    }

    // Back to the geometry the next case expects on terminal 0.
    regrid(60, 16, "the first terminal would not size back");
}
