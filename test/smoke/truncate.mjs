// /bin/truncate: a file's length, set through Sys::Truncate.
// Part of the smoke suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { fail, shows, store } from "./harness.mjs";

const { is, line } = shows(13950);

// Relative names throughout, as cp does: the screen is 60 columns and a
// command that wraps puts its own tail in the output.
const DIR = "/home/q";

const put = (name, text) => store.files.set(`${DIR}/${name}`, new TextEncoder().encode(text));

// The bytes, from the store rather than off the screen: a grow puts NULs in,
// which no terminal shows.
const bytes = (name) => Array.from(store.files.get(`${DIR}/${name}`) ?? []).join(",");

const USAGE = "usage: truncate [-co] [-r <rfile>] [-s <size>] <file>...";

// What `wc` says of a file with no whitespace in it: one word unless empty.
const wc = (n) => (n ? `0 1 ${n}` : "0 0 0");

export function check() {
    line(`mkdir ${DIR}`);
    line(`cd ${DIR}`);

    // A missing file is made and grown, and what a grow adds is zeros.
    is("truncate -s 4 a; wc a", wc(4));
    if (bytes("a") !== "0,0,0,0")
        fail(`a grow wrote ${bytes("a")}`);

    // Shrink, then grow again: what survives is the front of the file.
    put("b", "abcdef");
    is("truncate -s 3 b; cat b", "abc");
    is("truncate -s 6 b; wc b", wc(6));
    if (bytes("b") !== "97,98,99,0,0,0")
        fail(`a regrow wrote ${bytes("b")}`);
    is("truncate -s 0 b; wc b", wc(0));

    // + and - work off the size the file has now, and a shrink past the start
    // is empty rather than an error.
    put("c", "0123456789");
    is("truncate -s +5 c; wc c", wc(15));
    is("truncate -s -5 c; wc c", wc(10));
    is("truncate -s -99 c; wc c", wc(0));

    // < is at most and > at least, so each leaves the other way alone.
    put("d", "0123456789");
    is("truncate -s '<4' d; wc d", wc(4));
    is("truncate -s '<9' d; wc d", wc(4));
    is("truncate -s '>9' d; wc d", wc(9));
    is("truncate -s '>2' d; wc d", wc(9));

    // / rounds down to a multiple and % up.
    is("truncate -s /4 d; wc d", wc(8));
    is("truncate -s %6 d; wc d", wc(12));

    // A unit, and -o counting in 512-byte blocks.
    is("truncate -s 1K d; wc d", wc(1024));
    is("truncate -o -s 1 d; wc d", wc(512));
    is("truncate -s 2KB d; wc d", wc(2000));

    // -r takes the size from another file, and a modifier then works off that
    // rather than off the file being changed.
    put("r", "seven!!");
    is("truncate -r r d; wc d", wc(7));
    is("truncate -r r -s +3 a; wc a", wc(10));

    // -c makes nothing, and a file that is not there is not a failure.
    is("truncate -c -s 8 g; echo $?", "0");
    if (store.files.has(`${DIR}/g`))
        fail("-c made the file anyway");
    is("truncate -s 8 g; wc g", wc(8));

    // Several operands in one run, and one that fails does not stop the rest.
    is("truncate -s 2 a b; wc a", wc(2));
    is("truncate -s 1 n/x a; echo $?", "truncate: n/x: not found|1");
    is("wc a", wc(1));

    // Neither -s nor -r is a usage error, and so is a size that is not one.
    is("truncate a; echo $?", `${USAGE}|2`);
    is("truncate -s zz a; echo $?", "truncate: zz: invalid|2");
    is("truncate -s /0 a; echo $?", "truncate: a: invalid|1");
    is("truncate -q -s 1 a; echo $?", `truncate: illegal option -- q|${USAGE}|2`);
    is("truncate -s; echo $?", `truncate: option requires an argument -- s|${USAGE}|2`);

    // A directory has no length to set.
    is(`truncate -s 1 ${DIR}; echo $?`, `truncate: ${DIR}: is a directory|1`);

    line("cd /home"); // the session is cumulative: leave the cwd as it was
    line(`rm -r ${DIR}`);
}
