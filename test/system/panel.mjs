// One program on two screens: a process paints and reads keys on a terminal it
// was not spawned on (Sys::TermOpen).
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.
//
// After `quad`, which leaves terminals 1..3 with grids and no shells — which is
// what frees terminal 1's two claims for an editor started from terminal 0. It
// is also why TERM_NO_SHELL itself is not exercised here: the flag only acts on
// a terminal nothing has made, and TERM_MAX ids are all spoken for by now.

import { CTRL, fail, output, press, rows, run, screen, submit } from "./harness.mjs";

// Terminal 0 runs the editor; it paints on this one.
const T = 1;

let now = 34000;
const at = (by = 100) => (now += by);

function settle(term, want, why) {
    const base = at(300);
    for (let i = 0; i < 200; i++) {
        run(base + i);
        if (want(screen(term)))
            return screen(term);
    }
    return fail(why);
}

const shows = (term, word) => rows(screen(term)).some((line) => line.includes(word));
const prompted = (s) => rows(s).some((line) => line.endsWith("$"));

export function check() {
    // Every terminal the page made, with its own geometry. /proc/host could not
    // hold this: a machine with four grids has no geometry of its own.
    submit("clear", at());
    const terms = output(submit("cat /proc/terms", at()));
    if (terms.length !== 4)
        fail(`/proc/terms listed ${terms.length} terminals: ${JSON.stringify(terms)}`);
    for (const [id, line] of terms.entries()) {
        const want = `${id} ${screen(id).cols} ${screen(id).rows}`;
        if (line !== want)
            fail(`/proc/terms said ${JSON.stringify(line)}, expected ${JSON.stringify(want)}`);
    }

    // A screen the page never put up is refused at the open, so a program on a
    // one-canvas page degrades rather than traps.
    submit("clear", at());
    const missing = output(submit("edit -S 9 /home/panel.t", at()));
    if (!missing.join("|").includes("no such screen"))
        fail(`edit -S 9 said ${JSON.stringify(missing)}, expected "no such screen"`);

    // The editor is terminal 0's foreground job and paints on terminal T. Its
    // status bar is the proof: it names the file, and nothing else writes it.
    submit("edit -S 1 /home/panel.t", at());
    settle(T, () => shows(T, "^Q quits"), `the editor never painted on terminal ${T}`);

    // Nothing of the editor is on the terminal it was started from: it took the
    // alternate screen of the other one.
    if (shows(0, "^Q quits") || shows(0, "panel.t  "))
        fail("the editor painted on the terminal it was started from");

    // Keys follow the screen, not the process: terminal T's console routes to
    // the claim the editor took there, and terminal 0 is left alone.
    for (const ch of "hi")
        press(ch.codePointAt(0), 0, T);
    settle(T, () => shows(T, "hi"), `what was typed on terminal ${T} did not reach the editor`);
    if (shows(0, "hi"))
        fail("what was typed on the second terminal reached the first");

    // ^C is terminal 0's, where the editor is in front — so the program is
    // killable from the screen it was started on, and terminal T's grid comes
    // back to what the claim was taken over.
    press("c".codePointAt(0), CTRL, 0);
    settle(0, prompted, "^C left terminal 0 without a prompt");
    if (shows(T, "^Q quits"))
        fail(`terminal ${T} kept the editor's screen after the editor died`);
}
