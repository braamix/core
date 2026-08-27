// df, /proc/host, mount and uname.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { fail, names, output, prompt, row, rows, screen, store, submit } from "./harness.mjs";

export function check() {
    let s = screen();
    // M5, second criterion, as amended: df reports the quota and the usage as
    // a BSD table, the durability having moved to the boot banner. The whole
    // line is matched, since a row wider than this grid's sixty columns wraps.
    const blocks = Math.floor(store.quota / 1024);
    s = submit("clear", 1180);
    s = submit("df", 1181);
    const df = rows(s);
    if (!df.includes("Filesystem  1K-blocks     Used    Avail Capacity  Mounted on"))
        fail(`df did not head the table: ${JSON.stringify(df)}`);
    const root = df.find((line) => line.endsWith("    /"));
    if (!root || !new RegExp(`^opfs +${blocks} +\\d+ +\\d+ +\\d+%    /$`).test(root))
        fail(`df did not report the root: ${JSON.stringify(df)}`);
    if (!df.some((line) => /^procfs +0 +0 +0 +-    \/proc$/.test(line)))
        fail(`df did not report /proc: ${JSON.stringify(df)}`);

    // -h, over the fake's ten gibibytes exactly — the same figure as above.
    s = submit("clear", 1181.1);
    s = submit("df -h", 1181.2);
    const dfh = rows(s);
    if (!dfh.includes("Filesystem       Size     Used    Avail Capacity  Mounted on"))
        fail(`df -h did not head the table: ${JSON.stringify(dfh)}`);
    if (!dfh.some((line) => /^opfs +10G +\d/.test(line)))
        fail(`df -h did not scale the quota: ${JSON.stringify(dfh)}`);

    // /proc/host is what the kernel knows about itself and what the host said
    // about the browser at boot, and `uname` reformats it — the arrangement
    // `mount` has over /proc/mounts. The host half comes from the fake's fixed
    // string, so the whole file is deterministic.
    s = submit("clear", 1182);
    s = submit("cat /proc/host", 1183);
    const host = output(s);
    // A plain table, colons and all left to the boot banner: `uname` reads a
    // field out of this with next_field, so a name must not carry punctuation.
    if (host.some((line) => /^[a-z]+:/.test(line)))
        fail(`/proc/host punctuates its names: ${JSON.stringify(host)}`);
    for (const want of ["system   braam", "machine  wasm32", "browser  Fake 1", "agent    fake"])
        if (!host.includes(want))
            fail(`/proc/host is missing ${JSON.stringify(want)}: ${JSON.stringify(host)}`);
    if (!host.some((line) => /^release  \d+\.\d+\.\d+/.test(line)))
        fail(`/proc/host did not report the release: ${JSON.stringify(host)}`);
    // No geometry in here: a terminal is a process's, and `uname -a` asks
    // Sys::Tty for its own, which is checked below.
    if (host.some((line) => line.startsWith("screen")))
        fail(`/proc/host reports a geometry: ${JSON.stringify(host)}`);
    // Every row is a field: the blank line in the stored text is how far the
    // boot banner goes, and boot is the only reader of it.
    if (host.some((line) => !line))
        fail(`/proc/host has a blank row: ${JSON.stringify(host)}`);

    // `mount` is the same arrangement over /proc/mounts, and until now nothing
    // ran it. Every row the table publishes comes back reformatted.
    s = submit("clear", 1183.5);
    const table = output(submit("cat /proc/mounts", 1183.6));
    s = submit("clear", 1183.7);
    const mounted = output(submit("mount", 1183.8));
    const want = table.map((line) => {
        const [prefix, kind, mode] = line.split(/\s+/);
        return `${prefix} — ${kind} (${mode})`;
    });
    if (mounted.join("|") !== want.join("|"))
        fail(`mount printed ${JSON.stringify(mounted)}, expected ${JSON.stringify(want)}`);
    if (!want.some((l) => l.startsWith("/ — ")))
        fail(`/proc/mounts did not report the root: ${JSON.stringify(table)}`);

    // Two operands are Sys::Mount, which refuses: §5.4 has no Fs to build from
    // a special. One is neither form.
    s = submit("clear", 1183.85);
    s = submit("mount /dev/zero /tmp/m; echo $?", 1183.86);
    if (!rows(s).includes("mount: /dev/zero: unsupported"))
        fail(`mount of two printed ${JSON.stringify(rows(s))}`);
    if (!rows(s).includes("1"))
        fail(`mount of two exited ${JSON.stringify(rows(s))}, expected 1`);

    s = submit("clear", 1183.87);
    s = submit("mount /dev/zero; echo $?", 1183.88);
    if (!rows(s).includes("usage: mount [special mount_point]"))
        fail(`mount of one printed ${JSON.stringify(rows(s))}`);
    if (!rows(s).includes("2"))
        fail(`mount of one exited ${JSON.stringify(rows(s))}, expected 2`);

    // The default is the system name; -m is the one field neither the host nor
    // the version supplies.
    s = submit("clear", 1184.1);
    s = submit("uname", 1184.2);
    if (!rows(s).includes("braam"))
        fail(`uname printed ${JSON.stringify(rows(s))}, expected braam`);
    s = submit("uname -m", 1184.3);
    if (!rows(s).includes("wasm32"))
        fail(`uname -m printed ${JSON.stringify(rows(s))}, expected wasm32`);

    // -g is the geometry alone, and the one field that is not in the file. Off
    // the grid there is nothing to print, which is a missing field's status.
    s = submit("clear", 1184.31);
    s = submit("uname -g", 1184.32);
    if (output(s).join("|") !== `${s.cols}x${s.rows}`)
        fail(`uname -g printed ${JSON.stringify(output(s))}, expected ${s.cols}x${s.rows}`);
    s = submit("clear", 1184.33);
    s = submit("uname -g > /dev/null; echo $?", 1184.34);
    if (output(s).join("|") !== "1")
        fail(`uname -g into /dev/null printed ${JSON.stringify(output(s))}, expected 1`);

    // -a is every field, with this terminal's geometry after `machine`, which
    // is where the kernel's own end and the host's begin.
    s = submit("clear", 1184.4);
    s = submit("uname -a", 1184.5);
    const all = rows(s).filter((line) => line && !line.includes("$"));
    if (!all.includes("system   braam") || !all.includes("agent    fake"))
        fail(`uname -a printed ${JSON.stringify(all)}`);
    if (all[all.indexOf("machine  wasm32") + 1] !== `screen   ${s.cols}x${s.rows}`)
        fail(`uname -a did not report this terminal after machine: ${JSON.stringify(all)}`);

    // Down a pipe there is no window to name, and the line is left out.
    s = submit("clear", 1184.55);
    if (output(submit("uname -a | cat", 1184.56)).some((line) => line.startsWith("screen")))
        fail(`uname -a reported a geometry into a pipe: ${JSON.stringify(rows(s))}`);
    s = submit("uname -z", 1184.6);
    if (!rows(s).some((line) => line.startsWith("usage: uname ")))
        fail(`uname -z printed ${JSON.stringify(rows(s))}, expected a usage line`);
    if (!rows(s).includes(prompt(2)))
        fail(`uname -z left ${row(s, s.cursor_y)}, expected ${prompt(2)}`);
}
