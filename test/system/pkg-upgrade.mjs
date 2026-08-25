// pkg upgrade: a second index — what moves, what does not, and the rollback.
// Part of the system suite; test/run.mjs runs the cases in order, the eight
// pkg cases share test/system/pkgfix.mjs, and doc/Testing.md has the rules.

import { linkTarget } from "../../web/fs.js";
import { fail, names, net, run, screen, store, submit, type } from "./harness.mjs";
import {
    RURL, archive, at, bytes, good, plant, prints, repo, serve, text, update,
} from "./pkgfix.mjs";

export function check() {
    let s = screen();
    // P20. An upgrade with no index is the same refusal an install is.
    prints("pkg upgrade", "pkg: no index; run pkg update", 1);

    store.dirs.add("/pkg");
    plant("/etc/repositories", RURL + "\n");
    serve("libz-1.0-r0", good);
    serve("hello-1.0-r0", archive("hello-1.0-r0"));
    serve("hello-1.1-r0", archive("hello-1.1-r0"));
    prints("pkg update", `${RURL}|index 1, 2 packages`);
    prints("pkg install hello",
           ["Installing libz (1.0-r0)", "Installing hello (1.0-r0)",
            "generation 1, 2 packages"].join("|"));

    // The second index moves hello and leaves libz where it was.
    net.routes.set(RURL + "/index",
                   { status: 200, headers: "content-type: text/plain\n", body: repo("index2") });
    prints("pkg update", `${RURL}|index 2, 2 packages`);

    prints("pkg upgrade",
           ["Upgrading hello (1.0-r0 -> 1.1-r0)", "generation 2, 2 packages"].join("|"));
    if (text("/pkg/gen/2/packages") !== "hello 1.1-r0\nlibz 1.0-r0\n")
        fail(`the upgraded generation holds ${JSON.stringify(text("/pkg/gen/2/packages"))}`);
    prints("pkg list -i", "hello  1.1-r0|libz   1.0-r0");

    // The farm was rebuilt, and nothing was told a generation changed.
    prints("hi", "hi from hello 1.1");
    if (linkTarget(bytes("/pkg/gen/2/bin/hi")) !== "/pkg/store/hello-1.1-r0/bin/hi")
        fail(`the farm names ${linkTarget(bytes("/pkg/gen/2/bin/hi"))}`);

    // libz did not move, and its record says so: G is the index version
    // that vouched for it, and only a package this run unpacked has one
    // written. A spurious refetch would show up here as G:2.
    if (!text("/pkg/db/libz-1.0-r0").includes("G:1\n"))
        fail("an upgrade rewrote the record of a package it did not move");
    if (!text("/pkg/db/hello-1.1-r0").includes("G:2\n"))
        fail("the upgraded package was not vouched for by the new index");

    // The version it came from is still on disk, and so is the generation
    // that names it, with its own farm untouched.
    if (!store.dirs.has("/pkg/store/hello-1.0-r0") || !store.files.has("/pkg/db/hello-1.0-r0"))
        fail("an upgrade took the old version with it");
    if (linkTarget(bytes("/pkg/gen/1/bin/hi")) !== "/pkg/store/hello-1.0-r0/bin/hi")
        fail("an upgrade rewrote the farm of the generation it came from");
    if (text("/pkg/world") !== "hello\n")
        fail(`an upgrade changed world: ${JSON.stringify(text("/pkg/world"))}`);

    // And that is what rollback means: swing the link back and the old
    // one runs, which is the whole reason the store keeps both.
    submit("rm /pkg/active", at());
    submit("ln -s /pkg/gen/1 /pkg/active", at());
    prints("hi", "hi from hello");
    submit("rm /pkg/active", at());
    submit("ln -s /pkg/gen/2 /pkg/active", at());
    prints("hi", "hi from hello 1.1");

    // Newest already: nothing to do, and no generation for it.
    prints("pkg upgrade", "generation 2, unchanged");
    if (store.dirs.has("/pkg/gen/3"))
        fail("an upgrade with nothing to do built a generation");

    // An operand upgrades that name and no other. libz is at what the index
    // offers, and a name nothing installed answers to is refused rather than
    // solved for — `upgrade` installs nothing new.
    prints("pkg upgrade libz", "generation 2, unchanged");
    prints("pkg upgrade nonesuch", "pkg: nonesuch: not installed", 1);
    if (store.dirs.has("/pkg/gen/3"))
        fail("a refused operand built a generation");

    // Installing is upgrading what it names: rolled back to the version
    // before, the same operand moves it again. Nothing serves the archive,
    // so the store directory this already holds is what it reuses.
    net.routes.delete(`${RURL}/hello-1.1-r0.zip`);
    submit("rm /pkg/active", at());
    submit("ln -s /pkg/gen/1 /pkg/active", at());
    prints("pkg install hello",
           ["Upgrading hello (1.0-r0 -> 1.1-r0)", "generation 3, 2 packages"].join("|"));
    prints("hi", "hi from hello 1.1");
    if (text("/pkg/world") !== "hello\n")
        fail(`an install changed world: ${JSON.stringify(text("/pkg/world"))}`);

    // A version in world is a hold, and the bare command honours it. Naming
    // the package is what takes it off, in the same run that upgrades it.
    submit("rm /pkg/active", at());
    submit("ln -s /pkg/gen/1 /pkg/active", at());
    plant("/pkg/world", "hello=1.0-r0\n");
    prints("pkg upgrade", "generation 1, unchanged");
    prints("pkg upgrade hello",
           ["Upgrading hello (1.0-r0 -> 1.1-r0)", "generation 4, 2 packages"].join("|"));
    if (text("/pkg/world") !== "hello\n")
        fail(`the operand left world at ${JSON.stringify(text("/pkg/world"))}`);
    prints("hi", "hi from hello 1.1");

    // World is the only root an upgrade has, so an empty one purges
    // everything — `upgrade` is the command typed by habit, and this is
    // deliberate rather than incidental.
    plant("/pkg/world", "");
    prints("pkg upgrade",
           ["Purging hello (1.1-r0)", "Purging libz (1.0-r0)",
            "generation 5, 0 packages"].join("|"));

    // A contradiction refuses under the command's own name, not install's.
    plant("/pkg/world", "ghost\n");
    prints("pkg upgrade",
           ["pkg: cannot upgrade:", "  ghost (no such package)"].join("|"), 1);
    plant("/pkg/world", "");

    // An operand that is not a §6 token at all, which is what is left of the
    // arity check now that a name is one.
    prints("pkg upgrade =1", "Usage: pkg upgrade [<package>...]", 2);
}
