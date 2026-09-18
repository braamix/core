// The harness as the SDK installs it: imported from <prefix>/share/braam, it
// boots the kernel and archive installed beside it and runs examples/hello,
// then a round trip through examples/zpipe, braam::zlib's caller.
// A file the harness comes to import and the install rules miss fails here.
//
//     node test/sdk.mjs <prefix> <hello.wasm> <zpipe.wasm>

import { existsSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";

const [prefix, hello, zpipe] = process.argv.slice(2).map((p) => resolve(p));
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

console.log("sdk ok: the installed harness boots, runs hello, and round-trips zpipe");
