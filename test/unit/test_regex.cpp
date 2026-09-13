// braam::regex (src/regex/). Two halves: regex.data replays 2,211 cases the
// host's own <regex.h> answered, three eflags each, and the rest is what no
// host can be asked -- UTF-8 by the sequence, \1 in an ERE, the error codes'
// spelling, REG_STARTEND, and the limits that must return rather than hang.
#include "harness.h"
#include "kernel/fmt.h"
#include "kernel/string.h"
#include "regex/regex.h"

namespace {

struct RegexCase {
    const char *pat;
    const char *str;
    const char *want[3]; // eflags 0, REG_NOTBOL, REG_NOTEOL
};

#include "regex.data"

const int EFLAGS[3] = { 0, REG_NOTBOL, REG_NOTEOL };

// The match as "so:eo", then every group that took part, trailing unset ones
// dropped -- which is the spelling regex.data records.
void put_match(Buf<128> &b, const regmatch_t *m, usize n)
{
    usize last = 0;
    for (usize i = 1; i < n; i++)
        if (m[i].rm_so >= 0)
            last = i;
    for (usize i = 0; i <= last; i++) {
        if (i)
            b.put(' ');
        if (m[i].rm_so < 0) {
            b.put('-');
            continue;
        }
        b.put(i32(m[i].rm_so)).put(':').put(i32(m[i].rm_eo));
    }
}

// Runs one pattern and renders the answer: "-" for no match, "!<message>" for
// a pattern regcomp refused.
void run(Buf<128> &b, const char *pat, Str str, int cflags, int eflags)
{
    regex_t re;
    int rc = regcomp(&re, pat, cflags);

    if (rc != 0) {
        char msg[64];
        usize n = regerror(rc, &re, msg, sizeof msg) - 1;
        b.put('!').put(Str(msg, n));
        return;
    }

    regmatch_t m[10];
    if (regexec(&re, str.data(), 10, m, eflags) != 0)
        b.put('-');
    else
        put_match(b, m, 10);
    regfree(&re);
}

void check_at(const char *pat, Str str, int cflags, int eflags, Str want, u32 line)
{
    Buf<128> b;

    run(b, pat, str, cflags, eflags);
    if (b.str() == want)
        return;

    Buf<200> msg;
    msg.put("/").put(Str(pat)).put("/ on \"").put(str).put("\" gave ").put(b.str());
    msg.put(", wanted ").put(want);
    test_check(false, msg.str(), __FILE_NAME__, line);
}

#define CHECK_RE(pat, str, cflags, want) check_at(pat, str, cflags, 0, want, __LINE__)

const int ERE = REG_EXTENDED | REG_NEWLINE;
const int BRE = REG_NEWLINE;

void table(const RegexCase *cases, usize n, int cflags)
{
    for (usize i = 0; i < n; i++)
        for (usize e = 0; e < 3; e++)
            check_at(cases[i].pat, Str(cases[i].str), cflags, EFLAGS[e], Str(cases[i].want[e]),
                     __LINE__);
}

// ------------------------------------------------------------- the sections

// What the host cannot judge: its `.` takes one byte in the C locale, and this
// engine takes a whole sequence on purpose, so a match never ends mid-rune.
void utf8()
{
    CHECK_RE(".", "\xc3\xa9", ERE, "0:2");
    CHECK_RE("..", "\xc3\xa9\xc3\xa9", ERE, "0:4");
    CHECK_RE(".", "\xf0\x9f\x94\xa5", ERE, "0:4");
    CHECK_RE("a.b",
             "a\xc3\xa9"
             "b",
             ERE, "0:4");
    CHECK_RE(".$", "x\xc3\xa9", ERE, "1:3");
    CHECK_RE("[^x]", "\xc3\xa9", ERE, "0:2");
    CHECK_RE("[\xc3\xa9]", "\xc3\xa9", ERE, "0:2");
    CHECK_RE("\xc3\xa9*", "\xc3\xa9\xc3\xa9", ERE, "0:4");
    CHECK_RE("[\xce\xb1-\xcf\x89]+", "\xce\xb2\xce\xb3", ERE, "0:4");

    // A malformed byte is one unit, never a swallowed sequence.
    CHECK_RE(".", "\x82", ERE, "0:1");
    CHECK_RE("..", "\x82\x82", ERE, "0:2");

    // The classes are the codepoint's, so a Greek letter is alpha.
    CHECK_RE("[[:alpha:]]+", "\xce\xb2\xce\xb3", ERE, "0:4");
    CHECK_RE("[[:digit:]]+", "\xce\xb2", ERE, "-");
    CHECK_RE("[[:upper:]]", "\xce\x91", ERE, "0:2");
    CHECK_RE("[[:lower:]]", "\xce\x91", ERE, "-");
    CHECK_RE("[[:blank:]]", "\xc2\xa0", ERE, "0:2");
    CHECK_RE("[[:cntrl:]]", "\xc2\x85", ERE, "0:2");
    CHECK_RE("[[:punct:]]", "\xc2\xab", ERE, "0:2");
}

// REG_ICASE over the ranges rune_lower maps, which is past ASCII.
void icase()
{
    const int F = ERE | REG_ICASE;

    CHECK_RE("\xc3\xa9", "\xc3\x89", F, "0:2");
    CHECK_RE("\xc3\x89", "\xc3\xa9", F, "0:2");
    CHECK_RE("[\xc3\xa9]", "\xc3\x89", F, "0:2");
    CHECK_RE("\xce\xb1+", "\xce\x91\xce\xb1", F, "0:4");
    CHECK_RE("\xd0\xb0", "\xd0\x90", F, "0:2");
    // The prefilter has to name both lead bytes, and ÿ/Ÿ is where they differ.
    CHECK_RE("x\xc3\xbfz", "x\xc5\xb8z", F, "0:4");
    CHECK_RE("x\xc5\xb8z", "x\xc3\xbfz", F, "0:4");
    // Folding happens before [^...] inverts, so [^a] refuses 'A'.
    CHECK_RE("[^a]", "A", F, "-");
    CHECK_RE("[^a]", "b", F, "0:1");
    // A back-reference folds too.
    CHECK_RE("(a)\\1", "aA", F, "0:2 0:1");
    CHECK_RE("(\xc3\xa9)\\1", "\xc3\xa9\xc3\x89", F, "0:4 0:2");
}

// REG_NOSUB reports the match and nothing about it.
void nosub()
{
    regex_t re;
    regmatch_t m[3] = { { -1, -1 }, { -1, -1 }, { -1, -1 } };

    CHECK_EQ(regcomp(&re, "(a)(b)", REG_EXTENDED | REG_NOSUB), 0);
    CHECK_EQ(re.re_nsub, 0);
    CHECK_EQ(regexec(&re, "xab", 3, m, 0), 0);
    // Untouched: nmatch is taken as zero.
    CHECK(m[0].rm_so == -1);
    CHECK(m[1].rm_so == -1);
    CHECK_EQ(regexec(&re, "xyz", 3, m, 0), REG_NOMATCH);
    regfree(&re);

    // Without it the same pattern reports both groups.
    CHECK_EQ(regcomp(&re, "(a)(b)", REG_EXTENDED), 0);
    CHECK_EQ(re.re_nsub, 2);
    regfree(&re);
}

// The subject is a region: no NUL is needed, and a NUL in it is data.
void startend()
{
    regex_t re;

    CHECK_EQ(regcomp(&re, "b+", REG_EXTENDED), 0);

    // No terminator anywhere near: the bytes past rm_eo must not be read.
    static const char raw[] = { 'a', 'b', 'b', 'b', 'b' };
    regmatch_t m            = { 1, 3 };
    CHECK_EQ(regexec(&re, raw, 1, &m, REG_STARTEND), 0);
    CHECK(m.rm_so == 1 && m.rm_eo == 3);

    // An offset start is still a line start unless REG_NOTBOL says otherwise.
    regfree(&re);
    CHECK_EQ(regcomp(&re, "^b", REG_EXTENDED), 0);
    m = regmatch_t{ 1, 5 };
    CHECK_EQ(regexec(&re, raw, 1, &m, REG_STARTEND), 0);
    m = regmatch_t{ 1, 5 };
    CHECK_EQ(regexec(&re, raw, 1, &m, REG_STARTEND | REG_NOTBOL), REG_NOMATCH);
    regfree(&re);

    // A NUL is a byte. Without REG_STARTEND the same subject stops at it.
    CHECK_EQ(regcomp(&re, "a.c", REG_EXTENDED), 0);
    static const char withnul[] = { 'a', '\0', 'c' };
    m                           = regmatch_t{ 0, 3 };
    CHECK_EQ(regexec(&re, withnul, 1, &m, REG_STARTEND), 0);
    CHECK(m.rm_so == 0 && m.rm_eo == 3);
    m = regmatch_t{ 0, 3 };
    CHECK_EQ(regexec(&re, withnul, 1, &m, 0), REG_NOMATCH);
    regfree(&re);
}

// One pattern per code, each checked through the message regerror gives it.
void errors()
{
    CHECK_RE("[", "x", ERE, "!Unmatched [");
    CHECK_RE("[[:nosuch:]]", "x", ERE, "!Unknown character class");
    CHECK_RE("[a", "x", ERE, "!Unmatched [");
    CHECK_RE("[z-a]", "x", ERE, "!Invalid range end");
    CHECK_RE("(", "x", ERE, "!Unmatched (");
    CHECK_RE("(a", "x", ERE, "!Unmatched (");
    CHECK_RE(")(", "x", ERE, "!Unmatched (");
    CHECK_RE("a{2,1}", "x", ERE, "!Invalid repetition count");
    // A bound past 32767, and one long enough to wrap an int if it were let to.
    CHECK_RE("a{32768}", "x", ERE, "!Invalid repetition count");
    CHECK_RE("a{0,32768}", "x", ERE, "!Invalid repetition count");
    CHECK_RE("a{9876543210}", "x", ERE, "!Invalid repetition count");
    CHECK_RE("a\\{9876543210\\}", "x", BRE, "!Invalid repetition count");
    // The bound itself still compiles, and an unclosed brace is still the brace.
    CHECK_RE("a{32767}", "x", ERE, "-");
    CHECK_RE("a{9876543210", "x", ERE, "!Unmatched {");
    CHECK_RE("a{2", "x", ERE, "!Unmatched {");
    CHECK_RE("*", "x", ERE, "!Nothing to repeat");
    CHECK_RE("+a", "x", ERE, "!Nothing to repeat");
    CHECK_RE("?a", "x", ERE, "!Nothing to repeat");
    CHECK_RE("a\\", "x", ERE, "!Trailing backslash");
    CHECK_RE("\\1", "x", ERE, "!No such subexpression");
    CHECK_RE("(a)\\2", "x", ERE, "!No such subexpression");
    // And the BRE spellings.
    CHECK_RE("\\(a", "x", BRE, "!Unmatched (");
    CHECK_RE("a\\{2", "x", BRE, "!Unmatched {");
    CHECK_RE("a\\{2,1\\}", "x", BRE, "!Invalid repetition count");
    CHECK_RE("\\{2\\}", "x", BRE, "!Nothing to repeat");
    CHECK_RE("\\1", "x", BRE, "!No such subexpression");

    // A truncated buffer is still terminated, and the length asked for is the
    // whole message.
    regex_t re;
    CHECK_EQ(regcomp(&re, "[", REG_EXTENDED), REG_EBRACK);
    char small[5];
    CHECK_EQ(regerror(REG_EBRACK, &re, small, sizeof small), 12);
    CHECK(Str(small) == "Unma");
    CHECK_EQ(regerror(REG_EBRACK, &re, small, 0), 12);
}

// Where this engine departs from GNU on purpose.
void deviations()
{
    // A BRE has no alternation: \| is a literal bar, which glibc reads as one.
    CHECK_RE("a\\|b", "a|b", BRE, "0:3");
    CHECK_RE("a\\|b", "ab", BRE, "-");
    // \+ and \? likewise.
    CHECK_RE("a\\+", "a+", BRE, "0:2");
    CHECK_RE("a\\?", "a?", BRE, "0:2");
    // A backslash inside a bracket escapes the next byte, which POSIX leaves
    // literal -- so [\]] is a bracket holding ] and [\t] one holding t. The
    // escape spells a character out, it does not name one.
    CHECK_RE("[\\]]", "]", ERE, "0:1");
    CHECK_RE("[\\t]", "t", ERE, "0:1");
    CHECK_RE("[\\t]", "\t", ERE, "-");
    // Outside a bracket it does name one.
    CHECK_RE("\\t", "\t", ERE, "0:1");
    // ^a|^b is conservatively unanchored: only a leading ^ sets the flag.
    CHECK_RE("^a|^b", "x\nb", ERE, "2:3");
}

// \1 in an ERE, which is GNU's extension: the BSDs make it a literal 1, so
// regex.data cannot carry these and the BRE spellings there are the pair.
void backrefs()
{
    CHECK_RE("(a)\\1", "aa", ERE, "0:2 0:1");
    CHECK_RE("(a)\\1", "ab", ERE, "-");
    CHECK_RE("(.)\\1", "foobar", ERE, "1:3 1:2");
    CHECK_RE("(.)\\1", "abc", ERE, "-");
    CHECK_RE("(ab)c\\1", "abcabc", ERE, "0:5 0:2");
    CHECK_RE("(a|b)\\1", "aaa", ERE, "0:2 0:1");
    CHECK_RE("(a|b)\\1", "ab", ERE, "-");
    // An empty capture is a capture: \1 then takes nothing and succeeds.
    CHECK_RE("(a*)b\\1", "ab", ERE, "1:2 1:1");
    CHECK_RE("(a*)b\\1", "aabaa", ERE, "0:5 0:2");
    // A group that did not take part refuses, which covers \(a\1\) too.
    CHECK_RE("(a)|\\1", "b", ERE, "-");
    CHECK_RE("(a\\1)", "aa", ERE, "-");
    // Nine are addressable, and the tenth group is not.
    CHECK_RE("(a)(b)(c)(d)(e)(f)(g)(h)(i)\\9", "abcdefghii", ERE,
             "0:10 0:1 1:2 2:3 3:4 4:5 5:6 6:7 7:8 8:9");
}

// [[.x.]] and [[=x=]] name x. The corpus has only the two that are errors.
void collating()
{
    CHECK_RE("[[.x.]]", "x", ERE, "0:1");
    CHECK_RE("[[=x=]]", "x", ERE, "0:1");
    CHECK_RE("[a[=b=]c]", "b", ERE, "0:1");
    CHECK_RE("[^[.a.]]", "a", ERE, "-");
    // As a range endpoint, either end.
    CHECK_RE("[[.a.]-c]", "b", ERE, "0:1");
    CHECK_RE("[[.a.]-[.c.]]", "b", ERE, "0:1");
    // The name is whatever the terminator ends, punctuation and runes included.
    CHECK_RE("[[.].]]", "]", ERE, "0:1");
    CHECK_RE("[[.-.]]", "-", ERE, "0:1");
    CHECK_RE("[x[.-.]y]", "-", ERE, "0:1");
    CHECK_RE("[[.\xc3\xa9.]]", "\xc3\xa9", ERE, "0:2");
    CHECK_RE("[[=a=]]", "A", ERE | REG_ICASE, "0:1");
    // A name that is not one character names nothing, and an unclosed one is
    // the bracket's complaint rather than its own.
    CHECK_RE("[[.NIL.]]", "x", ERE, "!Invalid collating element");
    CHECK_RE("[[=aleph=]]", "x", ERE, "!Invalid collating element");
    CHECK_RE("[[..]]", "x", ERE, "!Invalid collating element");
    CHECK_RE("[[.NIL.]]", "x", BRE, "!Invalid collating element");
    CHECK_RE("[[.x]]", "x", ERE, "!Unmatched [");
}

// POSIX assigns subexpressions by leftmost-longest applied outward. AT&T's
// corpus is the authority on it (test_attregex.cpp); these are the rules it
// states only through its answers.
void subexpressions()
{
    // A turn reports what it matched: a branch this one skipped is unset, not
    // left over from the last.
    CHECK_RE("((..)|(.)){2}", "aaa", ERE, "0:3 2:3 - 2:3");
    CHECK_RE("(a(b)?)+", "aba", ERE, "0:3 2:3");
    // Which also decides a backreference to it, inside the repeat and out.
    CHECK_RE("((a)|b)*\\2", "abaa", ERE, "0:4 2:3 2:3");
    CHECK_RE("\\(a\\(b\\)*\\)*\\2", "abab", BRE, "-");

    // A repeat takes as few turns as it can, so no trailing empty one -- but
    // one empty turn beats none.
    CHECK_RE("(a*)*", "aaa", ERE, "0:3 0:3");
    CHECK_RE("(a*)*", "x", ERE, "0:0 0:0");
    // Turns owed to min are taken even so, and the last one is what shows.
    CHECK_RE("(a*){2}(x)", "ax", ERE, "0:2 1:1 1:2");
    CHECK_RE("(a*)*(x)", "ax", ERE, "0:2 0:1 1:2");
    // One entry stands for all of them, so a large min is not 2000 frames.
    CHECK_RE("(a*){2000}b", "aaab", ERE, "0:4 3:3");
    CHECK_RE("(a*){2000}(x)", "x", ERE, "0:1 0:0 0:1");

    // The whole match moves with it: \1 is the empty turn's, not the first's.
    CHECK_RE("\\(a*\\)*\\(x\\)\\(\\1\\)", "ax", BRE, "0:2 1:1 1:2 2:2");

    // Outward: the outer group settles before what is inside it, and a repeat
    // before its turns.
    CHECK_RE("((a*)(b|abc))(c*)", "abc", ERE, "0:3 0:3 0:0 0:3 3:3");
    CHECK_RE("(ab|a|c|bcd)*(d*)", "ababcd", ERE, "0:6 3:6 6:6");
    // A repeat with no group in it is still an extent, and this one takes all.
    CHECK_RE(".*(.*)", "ab", ERE, "0:2 2:2");
}

// The two limits: an answer, not a hang and not a trap.
void limits()
{
    // The mark stack rather than the native stack -- one frame per character
    // here would trap in a wasm process.
    String big;
    bool built = big.reserve(200001);
    for (usize i = 0; built && i < 199998; i++)
        built = big.push('a');
    built = built && big.push('b') && big.push('\0');
    CHECK(built);

    Str s(big.data(), big.size() - 1);
    CHECK_RE(".*b", s, ERE, "0:199999");
    CHECK_RE("a*b", s, ERE, "0:199999");
    CHECK_RE("[a]*b", s, ERE, "0:199999");

    // The budget: this one is exponential, so no match is the right answer and
    // coming back at all is the point.
    CHECK_RE("(a*)*(a*)*(a*)*c", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", ERE, "-");
    // Ten groups, of which the tenth is past the ten slots and unreported.
    CHECK_RE("((((((((((a))))))))))", "a", ERE, "0:1 0:1 0:1 0:1 0:1 0:1 0:1 0:1 0:1 0:1");
}

} // namespace

void test_regex()
{
    test_begin("regex");

    table(ERE_CASES, sizeof ERE_CASES / sizeof ERE_CASES[0], ERE);
    table(BRE_CASES, sizeof BRE_CASES / sizeof BRE_CASES[0], BRE);
    table(ICASE_CASES, sizeof ICASE_CASES / sizeof ICASE_CASES[0], ERE | REG_ICASE);

    utf8();
    icase();
    nosub();
    startend();
    errors();
    backrefs();
    collating();
    subexpressions();
    deviations();
    limits();
}
