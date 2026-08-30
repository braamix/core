// `ls -l` over a wide tree. Part of the system suite; test/run.mjs runs the
// cases in order and doc/Testing.md has the rules.
//
// The gap this fills: every other ls -l case lists a fixture whose sizes are
// one or two digits, so the size column's width — measured over the entries in
// a pass of its own, src/cmd/ls.cpp — is only ever exercised at 1 or 2. /bin is
// fifty entries of four and five digits. Nothing here looks at a value, so a
// binary that grew leaves it alone; what it pins is that every row agrees on
// where the fields end, which is what a width bug breaks and a compile does not
// notice. doc/TODO.md D2 is why that is worth a case.

import { fail, rows, submit } from "./harness.mjs";

// Offsets where a field ends: a non-space whose right neighbour is a space or
// the end of the line. A right-aligned column ends at the same offset on every
// row however wide its value.
function ends(line) {
    const s = line.replace(/\s+$/, "");
    const at = [];
    for (let i = 0; i < s.length; i++)
        if (s[i] !== " " && (i + 1 === s.length || s[i + 1] === " "))
            at.push(i);
    return at;
}

export function check() {
    const listing = rows(submit("ls -l /bin", 1187.5))
        .filter((r) => /^(file|dir |link) /.test(r));

    // Enough rows for the width pass to have had something to measure. The
    // grid is sixteen rows, so a listing this long fills it and the count is
    // what fits rather than what /bin holds.
    if (listing.length < 10)
        fail(`ls -l /bin listed ${listing.length} rows, expected the screen full`);

    // Kind, size, month, day, time — every field before the name, which is
    // last and left-aligned and so ends wherever it likes.
    const want = ends(listing[0]).slice(0, 5);
    if (want.length !== 5)
        fail(`ls -l /bin row has too few fields: ${JSON.stringify(listing[0])}`);

    // The size column is measured, not a constant, so this also says the
    // measurement agreed with the padding on every row.
    if (want[1] - want[0] < 4)
        fail(`ls -l /bin sized its column at ${want[1] - want[0]}, expected four digits or more`);

    for (const line of listing) {
        const at = ends(line);
        for (const w of want)
            if (!at.includes(w))
                fail(`ls -l /bin: no field ends at ${w} in ${JSON.stringify(line)} (ends ${at})`);
    }
}
