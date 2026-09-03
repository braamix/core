// Escape sequences from a program's write to the cells: doc/ANSI_Escape_Codes.md
// §4, over the same path Sys::Write takes. ESC cannot be typed at this prompt —
// KEY_ESCAPE is a code, not a byte (§6.5) — so /bin/tr writes one.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { cell, fail, rows, shows } from "./harness.mjs";

const { at, line } = shows(14090);

// What a command printed: the rows below the typed line, which carries the
// sequence as text and so cannot be looked at.
function printed(s) {
    const all = rows(s);
    return all.slice(all.findIndex((r) => r.includes("$ ")) + 1);
}

function said(s) {
    return printed(s)[0];
}

export function check() {
    line("clear");

    // Colour, and the sequence itself nowhere on the grid: an escape that is
    // parsed is an escape that is not painted (§3, rule 5).
    let s = line("echo '%[31mred%[0m' | tr % '\\033'");
    if (said(s) !== "red")
        fail(`a coloured word printed ${JSON.stringify(said(s))}`);
    if (printed(s).some((r) => r.includes("[31m")))
        fail(`the sequence was painted: ${JSON.stringify(rows(s))}`);
    const y = rows(s).findIndex((r) => r === "red");
    if (cell(s, 0, y).fg !== 1)
        fail(`red printed in colour ${cell(s, 0, y).fg}, expected 1`);

    // Absolute addressing, which lands outside the window output() slices.
    line("clear");
    s = line("echo '%[3;5Hhere' | tr % '\\033'");
    if (rows(s)[2] !== "    here")
        fail(`CUP put it at ${JSON.stringify(rows(s)[2])}`);

    // Erasing to the end of the line, over what was written a moment ago.
    line("clear");
    s = line("echo 'abc%[1;1H%[K' | tr % '\\033'");
    if (rows(s).some((r) => r.includes("abc")))
        fail(`EL left the row: ${JSON.stringify(rows(s))}`);

    // Swallowed, and nothing answers a query (§6.2).
    line("clear");
    s = line("echo 'a%[?12l%[6n%[cb' | tr % '\\033'");
    if (said(s) !== "ab")
        fail(`the swallowed sequences printed ${JSON.stringify(said(s))}`);

    // A tab is a stop rather than a cell.
    line("clear");
    s = line("echo 'a%b' | tr % '\\011'");
    if (said(s) !== "a       b")
        fail(`a tab printed ${JSON.stringify(said(s))}`);

    // A scroll region, and the shell's own row above it left alone. Released
    // before the next case, since the margins outlive the program.
    line("clear");
    s = line("echo '%[3;5r' | tr % '\\033'");
    line("echo one; echo two; echo three; echo four");
    s = line("echo '%[r' | tr % '\\033'");
    if (!rows(s).some((r) => r.includes("$ ")))
        fail(`the region ate the prompt: ${JSON.stringify(rows(s))}`);

    line("clear");
    at();
}
