// /bin/tail: the last lines of a file, read from a window at its end rather
// than from the start (Sys::Seek).
// Part of the smoke suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { fail, output, shows, store, submit } from "./harness.mjs";

const { at, is, line } = shows(13650);

const put = (path, text) => store.files.set(path, new TextEncoder().encode(text));

// What one command printed, the screen cleared first so nothing above it counts.
const printed = (cmd) => {
    submit("clear", at());
    return output(submit(cmd, at())).join("|");
};

export function check() {
    line("mkdir /home/tl");

    // A file inside one window, and one whose last line has no newline: tail
    // ends every line it prints, as it always has.
    put("/home/tl/a", "one\ntwo\nthree\n");
    put("/home/tl/b", "one\ntwo\nthree");
    is("tail -n 2 /home/tl/a", "two|three");
    is("tail -n 2 /home/tl/b", "two|three");
    is("tail -n 9 /home/tl/a", "one|two|three");
    is("tail /home/tl/a", "one|two|three"); // ten by default
    is("tail -n 1 /home/tl/a", "three");
    is("tail -n 0 /home/tl/a", "");

    // Past one window, so it doubles: 200 lines is some 1,800 bytes and
    // SYS_CHUNK is 512. The answer must not depend on where a window fell.
    let big = "";
    for (let i = 1; i <= 200; i++)
        big += `line-${i}\n`;
    put("/home/tl/big", big);
    is("tail -n 3 /home/tl/big", "line-198|line-199|line-200");
    is("tail -n 1 /home/tl/big", "line-200");

    // The window and the streaming path are the same program: stdin cannot
    // seek, so `cat |` reads the file through and must still agree.
    for (const n of [1, 3, 12]) {
        const seeks    = printed(`tail -n ${n} /home/tl/big`);
        const streamed = printed(`cat /home/tl/big | tail -n ${n}`);
        if (seeks !== streamed)
            fail(`tail -n ${n} printed ${JSON.stringify(seeks)} from the file, ` +
                 `but ${JSON.stringify(streamed)} from a pipe`);
    }

    // Several files are one concatenation, which no window can serve.
    put("/home/tl/c", "x\ny\n");
    is("tail -n 3 /home/tl/a /home/tl/c", "three|x|y");
    // The concatenation has no separator of its own, so a file whose last line
    // has no newline runs into the next one's first.
    is("tail -n 3 /home/tl/b /home/tl/c", "two|threex|y");

    // A file that will not open is reported, and nothing is printed.
    is("tail /home/tl/nope", "tail: /home/tl/nope: not found");

    line("rm -r /home/tl");
}
