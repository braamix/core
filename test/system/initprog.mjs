// /etc/init: the program the boot archive asks init to run in place of the
// shell. Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import {
    fail, instantiate, kernel, prompt, resize, row, rows, run, screen, store,
} from "./harness.mjs";

// A boot with whatever /etc/init now says, on a fresh instance over the same
// store. The stamp is untouched, so nothing unpacks.
function reboot(at) {
    store.reopen();
    instantiate();
    kernel().init(0);
    resize(60, 16);
    run(at);
    return screen();
}

const shown = (s) => JSON.stringify(rows(s));

export function check() {
    const named = (path) =>
        store.files.set("/etc/init", new TextEncoder().encode(path + "\n"));

    // A program of its own instead of the shell. uname prints and exits, so the
    // session ends the way any exit ends it -- named by its path, not "the
    // shell", which is what proves init ran this and not /bin/sh.
    named("/bin/uname");
    let s = reboot(4000);
    if (!rows(s).some((line) => line.includes("braam")))
        fail(`/etc/init did not run uname: ${shown(s)}`);
    if (!rows(s).some((line) => line.includes("/bin/uname exited (status 0)")))
        fail(`the session did not end on uname: ${shown(s)}`);
    if (rows(s).some((line) => line.includes("$")))
        fail(`/etc/init was named and a shell came up anyway: ${shown(s)}`);

    // Leading and trailing blanks are not part of the path, and only the first
    // line is read.
    store.files.set("/etc/init", new TextEncoder().encode("  /bin/uname  \nignored\n"));
    s = reboot(4100);
    if (!rows(s).some((line) => line.includes("/bin/uname exited (status 0)")))
        fail(`/etc/init was not trimmed to its first line: ${shown(s)}`);

    // A program that is not there says so and stops. No offer to unpack: the
    // archive is /bin and /etc, so it is no repair for a program /etc/init
    // named -- that offer is the shell's alone.
    named("/bin/nope");
    s = reboot(4200);
    if (!rows(s).some((line) => line.includes("/bin/nope: not in the store")))
        fail(`a missing init program went unmentioned: ${shown(s)}`);
    if (!rows(s).some((line) => line.includes("there is nothing to run")))
        fail(`a missing init program left no ending: ${shown(s)}`);
    if (rows(s).some((line) => line.includes("restore /bin and /etc")))
        fail(`a missing init program was offered an unpack: ${shown(s)}`);

    // An empty file means the shell, as a missing one does.
    store.files.set("/etc/init", new TextEncoder().encode("\n"));
    s = reboot(4300);
    if (row(s, s.cursor_y) !== prompt())
        fail(`an empty /etc/init left ${row(s, s.cursor_y)}, expected a prompt`);

    // And the store goes back as it was found: every case below wants a prompt.
    store.files.delete("/etc/init");
    s = reboot(4400);
    if (row(s, s.cursor_y) !== prompt())
        fail(`no /etc/init left ${row(s, s.cursor_y)}, expected a prompt`);
}
