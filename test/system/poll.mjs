// Sys::Poll: two pipes answering out of turn, a timeout, a descriptor another
// task holds, and ^C on a wait nothing else ends. After spawn, because every
// case here starts children and pipes the way that one does.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { CTRL, KEY, fail, others, press, prompt, row, rows, run, screen, submit, type }
    from "./harness.mjs";

export function check() {
    let s = screen();

    // Two children, two pipes. `echo` answers at once and the shell behind the
    // other pipe sleeps 100 ms first, so the order the lines arrive in is not
    // the order the children were started in — and the second line proves the
    // first pipe was not drained to the end before the second was looked at.
    s = submit("clear", 9300);
    type("polltest two");
    press(KEY.ENTER);
    s = screen();
    run(9301);
    run(9450); // past the sleep: the second child writes here
    s = screen();
    if (!rows(s).includes("a: first"))
        fail(`the first pipe did not arrive: ${JSON.stringify(rows(s))}`);
    if (!rows(s).includes("b: second"))
        fail(`the second pipe did not arrive: ${JSON.stringify(rows(s))}`);
    if (others() !== 0)
        fail(`${others()} instances outlived polltest two`);

    // A pipe nobody writes: the poll parks on a timer and answers 0 when it
    // expires. The harness clock is frozen, so the timeout happens when this
    // case says it does.
    s = submit("clear", 9460);
    type("polltest -t 200");
    press(KEY.ENTER);
    if (run(9461) !== 200)
        fail("a poll with a timeout did not arm one for what was left of it");
    run(9700);
    s = screen();
    if (!rows(s).includes("timeout"))
        fail(`a poll with nothing to wait for printed ${JSON.stringify(rows(s))}`);

    // A descriptor a poll holds. The second task is parked in a poll on the
    // pipe's read end, so the root task's Read of it is Err(Perm) and its own
    // Poll of it is Err(Busy) — the two refusals, told apart.
    s = submit("clear", 9710);
    s = submit("polltest busy", 9711);
    if (!rows(s).includes("read: perm"))
        fail(`a read of a polled descriptor printed ${JSON.stringify(rows(s))}`);
    if (!rows(s).includes("poll: busy"))
        fail(`a poll of a polled descriptor printed ${JSON.stringify(rows(s))}`);
    if (!rows(s).includes("woke"))
        fail(`the parked poll did not wake: ${JSON.stringify(rows(s))}`);

    // ^C on a poll nothing else can end. It arms no timer, so the scheduler
    // has nothing to run until a key arrives, and the call answers Err(Intr).
    s = submit("clear", 9720);
    type("polltest wait");
    press(KEY.ENTER);
    if (run(9721) !== -1)
        fail("a poll with no timeout left the scheduler with work to do");
    if (others() !== 1)
        fail(`a parked poll left ${others()} instances, expected 1`);
    press("c".codePointAt(0), CTRL);
    if (run(9722) !== -1)
        fail("^C during a poll left something armed");
    s = screen();
    if (row(s, s.cursor_y) !== prompt(130))
        fail(`^C during a poll left ${JSON.stringify(row(s, s.cursor_y))}`);
    if (others() !== 0)
        fail(`${others()} instances outlived ^C during a poll`);
}
