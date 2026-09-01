// Headless driver for kernel.wasm and tests.wasm. Node stands in for the
// browser: instantiating a freestanding module needs nothing browser-specific,
// and test/fakefs.mjs stands in for OPFS.
//
// The system suite is the ordered list below and nothing else. Every case is
// one file in test/system/, taking its kernel, screen and shell from
// test/system/harness.mjs; doc/Testing.md has the rules they run by. The order
// is load-bearing — the suite is one cumulative session — so an entry that
// depends on an earlier one says so beside it.

import { fail, hasRootfs, init, kernel } from "./system/harness.mjs";

import * as abi from "./system/abi.mjs";
import * as boot from "./system/boot.mjs";
import * as cmp from "./system/cmp.mjs";
import * as cut from "./system/cut.mjs";
import * as cwd from "./system/cwd.mjs";
import * as diff from "./system/diff.mjs";
import * as dual from "./system/dual.mjs";
import * as du from "./system/du.mjs";
import * as entry from "./system/entry.mjs";
import * as find from "./system/find.mjs";
import * as flow from "./system/flow.mjs";
import * as fullscreen from "./system/fullscreen.mjs";
import * as complete from "./system/complete.mjs";
import * as glob from "./system/glob.mjs";
import * as grep from "./system/grep.mjs";
import * as head from "./system/head.mjs";
import * as help from "./system/help.mjs";
import * as interrupt from "./system/interrupt.mjs";
import * as jobs from "./system/jobs.mjs";
import * as language from "./system/language.mjs";
import * as lists from "./system/lists.mjs";
import * as ls from "./system/ls.mjs";
import * as net from "./system/net.mjs";
import * as persist from "./system/persist.mjs";
import * as pipe from "./system/pipe.mjs";
import * as pkgcli from "./system/pkgcli.mjs";
import * as pkgClean from "./system/pkg-clean.mjs";
import * as pkgCrash from "./system/pkg-crash.mjs";
import * as pkgInstall from "./system/pkg-install.mjs";
import * as pkgLocal from "./system/pkg-local.mjs";
import * as pkgRemove from "./system/pkg-remove.mjs";
import * as pkgScripts from "./system/pkg-scripts.mjs";
import * as pkgUpdate from "./system/pkg-update.mjs";
import * as pkgUpgrade from "./system/pkg-upgrade.mjs";
import * as pkgVerify from "./system/pkg-verify.mjs";
import * as procfs from "./system/procfs.mjs";
import * as quad from "./system/quad.mjs";
import * as panel from "./system/panel.mjs";
import * as process_ from "./system/process.mjs";
import * as redirect from "./system/redirect.mjs";
import * as rename from "./system/rename.mjs";
import * as respawn from "./system/respawn.mjs";
import * as script from "./system/script.mjs";
import * as seq from "./system/seq.mjs";
import * as sh from "./system/sh.mjs";
import * as sort from "./system/sort.mjs";
import * as signal from "./system/signal.mjs";
import * as spawn from "./system/spawn.mjs";
import * as subst from "./system/subst.mjs";
import * as sysinfo from "./system/sysinfo.mjs";
import * as columns from "./system/columns.mjs";
import * as term from "./system/term.mjs";
import * as tail from "./system/tail.mjs";
import * as tee from "./system/tee.mjs";
import * as tr from "./system/tr.mjs";
import * as truncate from "./system/truncate.mjs";
import * as chunk from "./system/chunk.mjs";
import * as cp from "./system/cp.mjs";
import * as path from "./system/path.mjs";
import * as uniq from "./system/uniq.mjs";
import * as unzip from "./system/unzip.mjs";
import * as vars from "./system/vars.mjs";
import * as worker from "./system/worker.mjs";
import * as wc from "./system/wc.mjs";
import * as wrap from "./system/wrap.mjs";
import * as xargs from "./system/xargs.mjs";

// A third field marks a case that reads the archive and is skipped without one.
const ARCHIVE = true;

const CASES = [
    ["abi",        abi.check],
    ["help",       help.check,       ARCHIVE],
    ["boot",       boot.check],       // boots the kernel: every case below needs a prompt
    ["term",       term.check],
    ["pipe",       pipe.check],
    ["vars",       vars.check],
    ["lists",      lists.check],
    ["flow",       flow.check],
    ["cwd",        cwd.check],        // leaves the shell in /home
    ["ls",         ls.check],         // after cwd: the fixture tree it lists
    ["columns",    columns.check],   // after ls: the same listing over a wide tree
    ["sysinfo",    sysinfo.check],
    ["pkgcli",     pkgcli.check],
    // The nine share test/system/pkgfix.mjs and one clock, and run in this
    // order because each leaves the /pkg the next one starts from.
    ["pkg-update",  pkgUpdate.check,  ARCHIVE],
    ["pkg-install", pkgInstall.check, ARCHIVE],
    ["pkg-remove",  pkgRemove.check,  ARCHIVE], // after install: it left /pkg broken on purpose
    ["pkg-upgrade", pkgUpgrade.check, ARCHIVE],
    ["pkg-verify",  pkgVerify.check,  ARCHIVE],
    ["pkg-clean",   pkgClean.check,   ARCHIVE], // after upgrade: the generations it collects
    ["pkg-scripts", pkgScripts.check, ARCHIVE],
    ["pkg-local",   pkgLocal.check,   ARCHIVE], // takes a /pkg of its own, as pkg-install does
    ["pkg-crash",   pkgCrash.check,   ARCHIVE], // last: puts the shipped /etc back, drops /pkg
    ["procfs",     procfs.check],
    ["interrupt",  interrupt.check],
    ["persist",    persist.check],    // reloads the kernel and resets the store
    ["net",        net.check],        // after persist: a working store again
    ["fullscreen", fullscreen.check],
    ["jobs",       jobs.check],
    ["signal",     signal.check],   // after jobs: it takes a job id of its own
    ["process",    process_.check],
    ["worker",     worker.check],     // after process: the worker under one
    ["spawn",      spawn.check],
    ["sh",         sh.check],
    ["wrap",       wrap.check],
    ["respawn",    respawn.check],    // takes workers away; its blocks are 1 s apart
    ["glob",       glob.check],
    ["rename",     rename.check],
    ["complete",   complete.check],  // after rename: its fixture moves the store clock the stamps above pin
    ["subst",      subst.check],
    ["redirect",   redirect.check],
    ["script",     script.check],
    ["tail",       tail.check],
    ["chunk",      chunk.check],
    ["cp",         cp.check],
    ["find",       find.check],   // after cp: the walk proc/io.h now shares with it
    ["du",         du.check],         // after find: the same walk, summed
    ["sort",       sort.check],
    ["uniq",       uniq.check],       // after sort: the pipeline it is written for
    ["truncate",   truncate.check],
    ["path",       path.check],
    // The four filters of A6, together: each needs a shell and nothing more.
    ["tee",        tee.check],
    ["cut",        cut.check],
    ["tr",         tr.check],
    ["seq",        seq.check],
    ["wc",         wc.check],
    ["head",       head.check],       // after wc: it counts what head printed
    ["xargs",      xargs.check],      // after the four: it runs them in batches
    ["grep",       grep.check],       // after xargs: the same shell, and a file of its own
    // Bytes, then lines: each needs a shell and nothing more.
    ["cmp",        cmp.check],
    ["diff",       diff.check],
    ["unzip",      unzip.check],
    ["entry",      entry.check],
    ["dual",       dual.check],       // leaves a second shell up, so others() gains one
    ["quad",       quad.check],       // after dual: four terminals at once, and it exits three
    ["panel",      panel.check],      // after quad: one program on two of its screens
    ["language",   language.check],   // last: it exits the shell for good
];

function usage() {
    console.error("usage: run.mjs --kernel <wasm> [--upto=<case>] [<rootfs.zip> [<proc.wasm>...]]" +
                  " | --tests <wasm> [<rootfs.zip>] | --list");
    process.exit(2);
}

if (process.argv.includes("--list")) {
    for (const [name] of CASES)
        console.log(name);
    process.exit(0);
}

const argv = process.argv.slice(2).filter((a) => !a.startsWith("--upto="));
const upto = process.argv.find((a) => a.startsWith("--upto="))?.slice(7);
const [mode, file, rootfs] = argv;
const binaries = argv.slice(3);
if (!file || (mode !== "--kernel" && mode !== "--tests"))
    usage();

await init(file, rootfs, mode);

if (upto && !CASES.some(([name]) => name === upto))
    fail(`--upto=${upto} names no case; --list prints them`);

if (mode === "--kernel") {
    // Each case leaves the state the next one starts from, so --upto is a
    // prefix rather than a filter: there is no way to run one on its own.
    let surface = { imports: 0, exports: 0 };
    for (const [name, check, archive] of CASES) {
        if (archive && !hasRootfs)
            continue;
        surface = check(binaries) ?? surface;
        if (name === upto) {
            console.log(`system ok: stopped after ${name}`);
            process.exit(0);
        }
    }
    console.log(`system ok: ${surface.imports} imports, ${surface.exports} exports`);
} else {
    const failures = kernel().run_tests();
    if (failures !== 0)
        fail(`${failures} check(s) failed`);
}
