// Tab completion: the word under the cursor, and what finishes it.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { KEY, chdir, fail, press, prompt, row, rows, run, screen, submit, type } from "./harness.mjs";

let clock = 13350;
const at = () => (clock += 0.01);

// Types a line, presses Tab `tabs` times, and gives back the row the cursor
// ended on — which is the line as completion left it.
function tab(text, tabs = 1) {
    type(text);
    for (let i = 0; i < tabs; i++)
        press(KEY.TAB);
    run(at());
    const s = screen();
    return row(s, s.cursor_y);
}

// Enter, and the row above the prompt that follows: what the line printed.
function ran() {
    press(KEY.ENTER);
    run(at());
    const s = screen();
    return row(s, s.cursor_y - 1);
}

// Every row on the screen, for the two cases that assert about a listing.
function shown() {
    return rows(screen());
}

function is(got, want, why) {
    if (got !== want)
        fail(`${why}: ${JSON.stringify(got)}, expected ${JSON.stringify(want)}`);
}

export function check() {
    // A tree of its own, so `ls`, `columns` and `glob`'s fixtures are untouched.
    // After `rename`, because the store stamps an mtime from a counter it moves
    // on every write, and the three cases above pin one. One witness per rule:
    // a unique name, a shared prefix, a directory, a name needing quotes, and a
    // leading dot.
    submit("mkdir /home/c /home/c/adir", at());
    submit("touch /home/c/alpha /home/c/alpine /home/c/beta", at());
    submit("touch '/home/c/two words'", at());
    submit("echo x > /home/c/.hidden", at());
    submit("cd /home/c", at());
    chdir("/home/c");
    const p = prompt();

    // A unique file: the name, and a space after it.
    is(tab("echo bet"), `${p} echo beta`, "a unique file");
    is(ran(), "beta", "the completed line");

    // A path is completed against its last component; the rest is already typed.
    is(tab("echo /home/c/be"), `${p} echo /home/c/beta`, "an absolute path");
    is(ran(), "/home/c/beta", "the completed path");

    // Only the bytes before the cursor are the word, and what follows stays.
    type("echo betX");
    press(KEY.LEFT);
    is(tab(""), `${p} echo beta X`, "a completion mid-line");
    press(KEY.ENTER);
    run(at());

    // Two names sharing what is already typed: nothing is inserted, and
    // nothing is said.
    is(tab("echo alp"), `${p} echo alp`, "an ambiguous prefix");

    // The second Tab in a row is what asks for the list, and the prompt is
    // drawn again under it.
    const listed = tab("", 1);
    is(listed, `${p} echo alp`, "the line after a listing");
    if (!shown().includes("alpha   alpine"))
        fail(`the second Tab listed ${JSON.stringify(shown())}`);
    press(KEY.ENTER);
    run(at());

    // A directory takes a slash rather than a space, so the next Tab descends.
    is(tab("echo adi"), `${p} echo adir/`, "a directory");
    press(KEY.ENTER);
    run(at());

    // A builtin, ahead of anything in PATH.
    is(tab("ec"), `${p} echo`, "a builtin");
    is(ran(), "", "a bare echo");

    // PATH, which is where the rest of a command word comes from.
    tab("l", 2);
    for (const name of ["less", "ln", "ls"])
        if (!shown().some((line) => line.split(/ +/).includes(name)))
            fail(`the PATH listing has no ${name}: ${JSON.stringify(shown())}`);
    press(KEY.ENTER);
    run(at());

    // A shell function, ahead of both.
    submit("fixture() { echo hi; }", at());
    is(tab("fix"), `${p} fixture`, "a function");
    is(ran(), "hi", "the completed function call");

    // A variable, bare and braced. Neither takes a trailing space: a name is
    // usually the head of a longer word.
    submit("VAL=hello", at());
    is(tab("echo $VA"), `${p} echo $VAL`, "a variable");
    is(ran(), "hello", "the completed variable");
    is(tab("echo \${VA"), `${p} echo \${VAL}`, "a braced variable");
    is(ran(), "hello", "the completed brace");

    // A name needing quotes, both ways of asking for it. The escaping matches
    // what was opened, and a closing quote goes in with the space.
    is(tab("echo two\\ w"), `${p} echo two\\ words`, "an escaped space");
    is(ran(), "two words", "the escaped line");
    is(tab("echo 'two w"), `${p} echo 'two words'`, "a quoted space");
    is(ran(), "two words", "the quoted line");

    // A leading dot has to be asked for, as a glob's does.
    tab("echo ", 2);
    if (shown().some((line) => line.split(/ +/).includes(".hidden")))
        fail(`the listing showed a dot file: ${JSON.stringify(shown())}`);
    press(KEY.ENTER);
    run(at());
    is(tab("echo .h"), `${p} echo .hidden`, "a dot file asked for");
    press(KEY.ENTER);
    run(at());

    // Nothing matches: the line is untouched and nothing is printed.
    is(tab("echo zzz", 2), `${p} echo zzz`, "no match");
    press(KEY.ENTER);
    run(at());

    submit("unset -f fixture", at());
    submit("unset VAL", at());
    submit("cd /home", at());
    chdir("/home");
    submit("rm -r /home/c", at());
}
