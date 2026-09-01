// /bin/tr: the sets a rune belongs to, and what it becomes. TODO.md's D1 —
// File::get, File::put and rune_lower had no caller in src/cmd/ until this one.
// The in-wasm suite cannot run a program, so this is the whole of the coverage.
// Part of the system suite; test/run.mjs runs the cases in order and
// doc/Testing.md has the rules they run by.

import { shows } from "./harness.mjs";

const { at, is, line } = shows(14080);

// The whole block, since asking for it is the program saying what it takes.
const USAGE = "Usage:|    tr [-cds] <string1> [<string2>]|Options:|" +
    "    -c    use the characters not in <string1>|" +
    "    -d    delete them rather than translating|" +
    "    -s    squeeze runs of a repeated output character|" +
    "A set takes a-z ranges, \\n \\t \\\\ and \\ooo escapes, and the|" +
    "classes [:alnum:] [:alpha:] [:digit:] [:lower:] [:punct:]|" +
    "[:space:] and [:upper:].";

export function check() {
    line("mkdir /home/sr");
    line("cd /home/sr");

    // Position for position, and v7's padding when string2 runs out.
    is("echo hello | tr el ip", "hippo");
    is("echo abcd | tr abcd xy", "xyyy");
    is("echo abc | tr a-c x-z", "xyz");
    is("echo abc | tr ab ''", "abc"); // an empty string2 is identity
    is("echo abc | tr '' xyz", "abc");

    // Ranges, and the two degenerate forms.
    is("echo 'a-b' | tr 'a-' _", "__b"); // a trailing - is a literal one
    is("tr c-a x 2>&1 | head -n 1",
       "tr: the range ends before it starts: c-a");

    // Escapes are GNU's: \n is a newline, not the letter n.
    is("echo 'a b' | tr ' ' ,", "a,b");
    is("echo a,b | tr , '\\n'", "a|b");
    is("echo abc | tr '\\141' X", "Xbc"); // \141 is 'a'
    is("echo abc | tr '\\q' X", "abc");   // an unrecognised escape is the char
    is("echo aqb | tr '\\q' X", "aXb");

    // Deleting.
    is("echo a1b2c3 | tr -d '[:digit:]'", "abc");
    is("echo 'a b  c' | tr -d ' '", "abc");
    is("tr -d a b 2>&1 | head -n 1",
       "tr: -d takes one set unless -s is given too");

    // Squeezing. v7 squeezed over string2 alone, so `tr -s a` did nothing
    // there; POSIX squeezes over string1 when it is the only set.
    is("echo aaabbb | tr -s ab", "ab");
    is("echo 'a   b' | tr -s ' '", "a b");
    is("echo aaabbb | tr -s ab xy", "xy"); // on the translated rune
    is("echo aXbXXc | tr -s X ' '", "a b c");
    is("echo aab | tr -s a", "ab"); // the first rune is never squeezed away
    is("echo a1b22c | tr -ds '[:digit:]' b", "abc");

    // Complementing, which is a predicate rather than a list.
    is("echo a1b2 | tr -cd '[:digit:]'", "12");
    is("echo 'a1 b2' | tr -cs '[:alnum:]' '\\n'", "a1|b2");
    is("tr -c ab xy 2>&1 | head -n 1",
       "tr: -c wants a one-character string2");

    // The case pair, which is why rune_lower is here: a class rather than a
    // written-out range reaches every script the two functions know.
    is("echo hello | tr '[:lower:]' '[:upper:]'", "HELLO");
    is("echo ПРИВЕТ | tr '[:upper:]' '[:lower:]'", "привет");
    is("echo Привет | tr '[:lower:]' '[:upper:]'", "ПРИВЕТ");
    is("echo abc | tr a-z A-Z", "ABC");
    // The contrast that makes the pair worth having: a written-out ASCII range
    // must leave Cyrillic alone.
    is("echo привет | tr a-z A-Z", "привет");

    // The finite classes are walked out, so their positions pair.
    is("echo a1b2 | tr '[:digit:]' '#'", "a#b#");
    is("echo a1b2 | tr '[:digit:]' xy", "ayby");
    is("echo abc | tr abc '[:digit:]'", "012"); // and in string2 too
    is("echo 'a,b.c' | tr -d '[:punct:]'", "abc");
    is("echo 'a b' | tr '[:space:]' _", "a_b_"); // the newline is one too
    is("echo a1b | tr -d '[:alpha:]'", "1");

    // The honest boundary: rune_lower and rune_upper map one codepoint to one,
    // and a caseless script has neither.
    is("echo 日本 | tr -d '[:alnum:]'", "日本");

    // The combinations that cannot mean anything.
    is("tr '[:digit:]-z' x 2>&1 | head -n 1",
       "tr: a class cannot end a range: [:digit:]-z");
    is("tr '[:nosuch:]' x 2>&1 | head -n 1",
       "tr: not a character class: [:nosuch:]");
    is("tr abc '[:alpha:]' 2>&1 | head -n 1",
       "tr: only the opposite case class can be a target");
    is("tr -c '[:lower:]' '[:upper:]' 2>&1 | head -n 1",
       "tr: -c cannot be given a case mapping");

    // A run that outlasts one buffer, through a real pipe: File::get and
    // File::put answer from the buffer, so a rune is not a coroutine.
    line("echo -n abcdefghij > d0");
    line("cat d0 d0 d0 d0 d0 d0 d0 d0 > d1");
    line("cat d1 d1 d1 d1 d1 d1 d1 d1 > d2");
    line("cat d2 d2 d2 d2 d2 d2 d2 d2 > d3; echo >> d3");
    is("wc d3", "1 1 5121");
    is("cat d3 | tr a-j A-J | cut -b 1-4", "ABCD");
    is("cat d3 | tr -d a-i | wc", "1 1 513");

    // What is asked for, and what is got wrong.
    is("tr --help", USAGE);
    is("tr 2>&1 | head -n 1", "Usage:");
    is("tr -q a b 2>&1 | head -n 1", "tr: bad option: q");
    is("tr -q a b > /dev/null 2>&1; echo $?", "2"); // a bad option is 2
    is("tr a b < /dev/null; echo $?", "0");
    is("echo -n ab | tr a b", "bb"); // no newline in, none added

    line("cd /home");
    line("rm -r /home/sr");
    at(); // the session is cumulative: leave the clock past the last line
}
