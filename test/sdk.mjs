// The harness as the SDK installs it: imported from <prefix>/share/braam, it
// boots the kernel and archive installed beside it and runs examples/hello,
// then round trips through examples/zpipe, examples/bzpipe, examples/xzpipe and
// examples/zstdpipe, the callers of braam::zlib, braam::bzip2, braam::lzma and
// braam::zstd. zstdpipe's frame is also read back by Node's own zstd, when this
// Node has one.
// A file the harness comes to import and the install rules miss fails here.
//
//     node test/sdk.mjs <prefix> <hello.wasm> <zpipe.wasm> <bzpipe.wasm> <xzpipe.wasm> \
//         <zstdpipe.wasm>

import { existsSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import * as zlib from "node:zlib";

const [prefix, hello, zpipe, bzpipe, xzpipe, zstdpipe] = process.argv.slice(2).map((p) => resolve(p));
const HARNESS = join(prefix, "share/braam");

const die = (msg) => {
    console.error(`sdk: ${msg}`);
    process.exit(1);
};

for (const f of ["test/system/harness.mjs", "web/kernel.wasm", "web/rootfs.zip"])
    if (!existsSync(join(HARNESS, f))) die(`no ${f} under ${HARNESS}`);

const H = await import(join(HARNESS, "test/system/harness.mjs"));
await H.init(join(HARNESS, "web/kernel.wasm"), join(HARNESS, "web/rootfs.zip"));
H.kernel().init(0);
if (H.run(0) !== -1) die("the kernel did not settle after boot");
H.regrid(80, 24, "resize returned no screen descriptor");
if (!H.store.files.has("/bin/sh")) die("the archive did not unpack");

let now = 1;
const line = (cmd, what) => {
    H.type(cmd);
    H.press(H.KEY.ENTER);
    for (let delay = H.run(now), i = 0; delay !== -1; delay = H.run(now)) {
        now += delay > 0 ? delay : 1;
        if (++i > 10000) die(`${what} did not finish`);
    }
};
const file = (path) => H.store.files.get(path) ?? new Uint8Array();

H.store.files.set("/bin/hello", new Uint8Array(readFileSync(hello)));
line("hello sdk >/tmp/o", "hello");
const out = new TextDecoder().decode(file("/tmp/o"));
if (out !== "Hello, sdk!\n") die(`hello printed ${JSON.stringify(out)}`);

H.store.files.set("/bin/zpipe", new Uint8Array(readFileSync(zpipe)));
line("zpipe </etc/help >/tmp/z", "zpipe");
line("zpipe -d </tmp/z >/tmp/u", "zpipe -d");
const help = file("/etc/help");
const z = file("/tmp/z");
const u = file("/tmp/u");
if (help.length === 0) die("/etc/help is empty");
if (z[0] !== 0x78 || z.length >= help.length) die(`zpipe made ${z.length} bytes of no zlib stream`);
if (Buffer.compare(Buffer.from(u), Buffer.from(help)) !== 0)
    die(`zpipe -d gave back ${u.length} bytes, not /etc/help's ${help.length}`);

H.store.files.set("/bin/bzpipe", new Uint8Array(readFileSync(bzpipe)));
line("bzpipe </etc/help >/tmp/b", "bzpipe");
line("bzpipe -d </tmp/b >/tmp/v", "bzpipe -d");
const b = file("/tmp/b");
const v = file("/tmp/v");
if (new TextDecoder().decode(b.subarray(0, 4)) !== "BZh9" || b.length >= help.length)
    die(`bzpipe made ${b.length} bytes of no bzip2 stream`);
if (Buffer.compare(Buffer.from(v), Buffer.from(help)) !== 0)
    die(`bzpipe -d gave back ${v.length} bytes, not /etc/help's ${help.length}`);

H.store.files.set("/bin/xzpipe", new Uint8Array(readFileSync(xzpipe)));
line("xzpipe </etc/help >/tmp/x", "xzpipe");
line("xzpipe -d </tmp/x >/tmp/w", "xzpipe -d");
const x = file("/tmp/x");
const w = file("/tmp/w");
const XZ_MAGIC = [0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00];
if (XZ_MAGIC.some((c, i) => x[i] !== c) || x.length >= help.length)
    die(`xzpipe made ${x.length} bytes of no .xz stream`);
if (Buffer.compare(Buffer.from(w), Buffer.from(help)) !== 0)
    die(`xzpipe -d gave back ${w.length} bytes, not /etc/help's ${help.length}`);

H.store.files.set("/bin/zstdpipe", new Uint8Array(readFileSync(zstdpipe)));
line("zstdpipe </etc/help >/tmp/s", "zstdpipe");
line("zstdpipe -d </tmp/s >/tmp/t", "zstdpipe -d");
const s = file("/tmp/s");
const t = file("/tmp/t");
const ZSTD_MAGIC = [0x28, 0xb5, 0x2f, 0xfd];
if (ZSTD_MAGIC.some((c, i) => s[i] !== c) || s.length >= help.length)
    die(`zstdpipe made ${s.length} bytes of no zstd frame`);
if (Buffer.compare(Buffer.from(t), Buffer.from(help)) !== 0)
    die(`zstdpipe -d gave back ${t.length} bytes, not /etc/help's ${help.length}`);
let nodeZstd = "Node has no zstd to check it against";
if (zlib.zstdDecompressSync) {
    const back = zlib.zstdDecompressSync(s);
    if (Buffer.compare(back, Buffer.from(help)) !== 0)
        die(`Node's zstd read ${back.length} bytes from zstdpipe, not /etc/help's ${help.length}`);
    nodeZstd = "Node's zstd agrees";
}

console.log("sdk ok: the installed harness boots, runs hello, and round-trips zpipe, bzpipe, " +
            `xzpipe and zstdpipe; ${nodeZstd}`);
