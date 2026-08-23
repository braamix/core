// Signals: a resize a parked program is told about, and a ^C nobody caught.
// Part of the smoke suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.
//
// After fullscreen: the pager it proved works is what a SIG_WINCH is delivered
// to. The grid is put back to boot's 60x16 before the case returns, because
// every case after this one reads it.

import {
    CTRL, KEY, fail, press, prompt, regrid, row, rows, run, screen, submit, type,
} from "./harness.mjs";

export function check() {
    const readme_top = "Braam is a small operating system in a browser tab.";

    // A pager holds the screen and is parked on a key. Geometry rides on a key
    // reply, so before signals this repainted only when one was pressed.
    let s = submit("clear", 3210);
    s = submit("less /README", 3211);
    if (row(s, 0) !== readme_top)
        fail(`less painted ${JSON.stringify(rows(s))}`);
    if (!row(s, 15).startsWith(" /README "))
        fail(`less put its status line elsewhere: ${JSON.stringify(rows(s))}`);

    // The resize, and **no keystroke after it**. SIG_WINCH abandons the parked
    // KeyRead with Err(Intr), the grid is asked for and resized, and the loop
    // repaints — all of which is the point.
    regrid(70, 20, "the grid would not grow");
    run(3212);
    s = screen();
    if (row(s, 0) !== readme_top)
        fail(`the pager did not repaint after a resize: ${JSON.stringify(rows(s))}`);
    if (!row(s, 19).startsWith(" /README "))
        fail(`the status line did not follow the resize: ${JSON.stringify(rows(s))}`);

    // Shrinking is the direction that used to fail the next blit with
    // Err(Invalid) and exit the pager with status 1.
    regrid(60, 16, "the grid would not shrink back");
    run(3213);
    s = screen();
    if (row(s, 0) !== readme_top)
        fail(`the pager did not repaint after a shrink: ${JSON.stringify(rows(s))}`);
    if (!row(s, 15).startsWith(" /README "))
        fail(`the status line did not follow the shrink: ${JSON.stringify(rows(s))}`);

    press("q".codePointAt(0));
    run(3214);
    s = screen();
    if (row(s, s.cursor_y) !== prompt())
        fail(`less did not give the screen back: ${JSON.stringify(rows(s))}`);

    // A ^C nobody caught is what it always was: the stage is cancelled and the
    // prompt reports 130. The mask starts empty, so this is every binary.
    s = submit("clear", 3215);
    type("sleep -m 60000");
    press(KEY.ENTER);
    run(3216);
    press("c".codePointAt(0), CTRL);
    run(3217);
    s = screen();
    if (!rows(s).some((line) => line.startsWith(prompt(130))))
        fail(`an uncaught ^C left ${JSON.stringify(rows(s))}`);

    // A ^C a shell *did* catch, in a script — which before signals could not
    // happen at all: the interrupt cancelled the process, so every await after
    // it answered Err(Cancelled) and a `trap … 2` there never ran. Now the
    // trap asks for SIG_INT, the wait it was parked on answers Err(Intr), and
    // the shell is alive to run the action.
    s = submit("clear", 3218);
    type("sh -c \"trap 'echo caught' 2; sleep -m 60000; echo after\"");
    press(KEY.ENTER);
    run(3219);
    press("c".codePointAt(0), CTRL);
    run(3220);
    s = screen();
    if (!rows(s).includes("caught"))
        fail(`a script's trap on 2 never ran: ${JSON.stringify(rows(s))}`);
    if (rows(s).includes("after"))
        fail(`the interrupt did not stop the script: ${JSON.stringify(rows(s))}`);

    // The other awaitable a signal abandons: a timer rather than a channel.
    // `vmstat` sleeps between rows, and a resize used to leave its row count
    // stale for the rest of the run — the gap its own comment named.
    s = submit("clear", 3218.5);
    type("vmstat 1");
    press(KEY.ENTER);
    run(3219.5);
    regrid(70, 20, "the grid would not grow under vmstat");
    run(3220.5);
    s = screen();
    // Still running is the whole assertion: a prompt here would mean the
    // abandoned sleep was taken for an error and vmstat left with 130.
    if (row(s, s.cursor_y).endsWith("$"))
        fail(`vmstat left on the resize: ${JSON.stringify(rows(s))}`);

    press("c".codePointAt(0), CTRL);
    run(3221.5);
    s = screen();
    if (!rows(s).some((line) => line.startsWith(prompt(130))))
        fail(`^C did not end vmstat: ${JSON.stringify(rows(s))}`);
    regrid(60, 16, "the grid would not shrink back");
    run(3221.6);

    // `kill` with a signal, which is the other half of Sys::Kill's payload.
    // The id is whatever the table is up to: this case takes one of its own.
    s = submit("clear", 3221);
    s = submit("sleep -m 60000 &", 3222);
    const announced = rows(s).find((line) => /^\[\d+\] \d+$/.test(line));
    if (!announced)
        fail(`the job was not announced: ${JSON.stringify(rows(s))}`);
    const id = announced.slice(1, announced.indexOf("]"));

    // A signal nothing sends is refused before the job is looked at.
    s = submit(`kill -TSTP %${id}`, 3223);
    if (!rows(s).some((line) => line.includes("kill: TSTP: unsupported")))
        fail(`kill took a signal nothing sends: ${JSON.stringify(rows(s))}`);

    s = submit(`kill -TERM %${id}`, 3224);
    s = submit("clear", 3225);
    s = submit("jobs", 3226);
    if (rows(s).some((line) => line.includes("sleep")))
        fail(`kill -TERM left the job running: ${JSON.stringify(rows(s))}`);
}
