// The harness as the SDK installs it: imported from <prefix>/share/braam, it
// boots the kernel and archive installed beside it and runs examples/hello.
// A file the harness comes to import and the install rules miss fails here.
//
//     node test/sdk.mjs <prefix> <hello.wasm>

import { existsSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";

const [prefix, hello] = process.argv.slice(2).map((p) => resolve(p));
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

H.store.files.set("/bin/hello", new Uint8Array(readFileSync(hello)));
H.type("hello sdk >/tmp/o");
H.press(H.KEY.ENTER);
let now = 1;
for (let delay = H.run(now), i = 0; delay !== -1; delay = H.run(now)) {
    now += delay > 0 ? delay : 1;
    if (++i > 10000) die("hello did not finish");
}

const out = new TextDecoder().decode(H.store.files.get("/tmp/o") ?? new Uint8Array());
if (out !== "Hello, sdk!\n") die(`hello printed ${JSON.stringify(out)}`);
console.log("sdk ok: the installed harness boots and runs hello");
