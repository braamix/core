// Sys::Read's length: a reader that names one takes a span at a time rather
// than SYS_CHUNK, so the round trips fall with the file's size and not with it.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { fail, net, output, shows, store, submit } from "./harness.mjs";

const { at, is, line } = shows(13850);

export function check() {
    line("mkdir /home/ck");

    // 64 KiB is one whole read and a byte over, so the second read is what
    // proves the first did not stop at 512.
    const size = 65537;
    let text = "";
    for (let i = 0; text.length < size; i++)
        text += `line-${i}\n`;
    text = text.slice(0, size);
    store.files.set("/home/ck/big", new TextEncoder().encode(text));

    store.files.set("/home/ck/small", new TextEncoder().encode("one\n"));

    // The difference between the two is the reading, with the spawn and the
    // prompt — the same for both — subtracted out.
    const calls = () => net.proc.stats().calls;
    const cost = (cmd) => {
        submit("clear", at());
        const was = calls();
        const got = output(submit(cmd, at())).join("|");
        return { spent: calls() - was, got };
    };

    const small = cost("wc /home/ck/small");
    const big = cost("wc /home/ck/big");
    const extra = big.spent - small.spent;

    if (!big.got.includes(String(size)))
        fail(`wc printed ${JSON.stringify(big.got)}, expected ${size} in it`);

    // 128 SYS_CHUNKs became one span and the empty read after it, so the big
    // file costs no more round trips than the small one. At SYS_CHUNK this was
    // 128 more.
    if (extra > 2)
        fail(`${size} bytes cost ${extra} round trips more than 4, expected none`);

    // The bytes must still be all of them, whatever the read size: what a
    // caller did not take is kept on the descriptor and served next. A pipe
    // cannot yield a span, so it walks the pushback path instead and must
    // agree with the file one.
    const lines = text.split("\n").length - 1;
    const words = text.split(/\s+/).filter((w) => w).length;
    is("wc /home/ck/big", `${lines} ${words} ${size}`);
    is("cat /home/ck/big | wc", `${lines} ${words} ${size}`);
}
