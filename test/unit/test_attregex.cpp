// AT&T Research's regex(3) conformance corpus (test/unit/att/, upstream
// verbatim), replayed against braam::regex. Where test_regex.cpp records what
// one host's <regex.h> answered, these answers are POSIX's own, so a
// disagreement here is a defect rather than a difference.
//
// The reader is testregex.c's main loop reduced to what the corpus uses: the B
// and E dialects, the i, n and $ modifiers, an nmatch override, SAME, the { }
// skip blocks, and the ? | ; categorisation lines. The comparison is its
// matchcheck() -- exact endpoints for every entry the answer lists, (-1,-1) for
// every slot past them, and a sentinel past nmatch that must not be written.
#include "harness.h"
#include "kernel/fmt.h"
#include "kernel/str.h"
#include "regex/regex.h"

namespace {

// Each file is one string literal: upstream between R"DAT( and )DAT". The
// literal therefore opens with the newline after R"DAT(, which run_file drops
// so that a reported line number is upstream's own.
constexpr Str BASIC =
#include "att/basic.dat"
    ;
constexpr Str FORCEDASSOC =
#include "att/forcedassoc.dat"
    ;
constexpr Str NULLSUBEXPR =
#include "att/nullsubexpr.dat"
    ;
constexpr Str REPETITION =
#include "att/repetition.dat"
    ;
constexpr Str LEFTASSOC =
#include "att/leftassoc.dat"
    ;
constexpr Str RIGHTASSOC =
#include "att/rightassoc.dat"
    ;
constexpr Str CATEGORIZE =
#include "att/categorize.dat"
    ;

// testregex.c's default, and the corpus never lists more than ten entries.
const int NMATCH = 20;

// The value a slot holds before regexec, so writing past nmatch is visible.
const regoff_t UNSET = -2;

// ------------------------------------------------------------------ the text

// A field copied out and terminated, since regcomp and regexec take a C string
// and nothing in kernel/str.h is terminated.
struct Field {
    char b[256];
    usize n = 0;

    bool set(Str s)
    {
        if (s.size() >= sizeof b)
            return false;
        for (usize i = 0; i < s.size(); i++)
            b[i] = s[i];
        b[s.size()] = 0;
        n           = s.size();
        return true;
    }
};

// Fields are separated by runs of tabs, so a column can be reached by tabbing
// to it. Trailing empty fields are not produced.
usize split_fields(Str line, Str out[5])
{
    usize n = 0, i = 0;

    while (i < line.size() && n < 5) {
        usize start = i;
        while (i < line.size() && line[i] != '\t')
            i++;
        out[n++] = line.substr(start, i - start);
        while (i < line.size() && line[i] == '\t')
            i++;
    }
    return n;
}

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

int hex_of(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// The C escapes of field 1's '$', which this corpus uses for \n and \xNN.
bool expand_escapes(Str in, Field &out)
{
    usize k = 0;

    for (usize i = 0; i < in.size();) {
        char c = in[i++];
        if (c != '\\' || i == in.size()) {
            if (k + 1 >= sizeof out.b)
                return false;
            out.b[k++] = c;
            continue;
        }
        char e = in[i++];
        int v  = -1;
        switch (e) {
        case 'a':
            v = 0x07;
            break;
        case 'b':
            v = 0x08;
            break;
        case 'e':
            v = 0x1b;
            break;
        case 'f':
            v = 0x0c;
            break;
        case 'n':
            v = 0x0a;
            break;
        case 'r':
            v = 0x0d;
            break;
        case 't':
            v = 0x09;
            break;
        case 'v':
            v = 0x0b;
            break;
        case '\\':
            v = '\\';
            break;
        case 'x': {
            v       = 0;
            usize d = 0;
            while (i < in.size() && hex_of(in[i]) >= 0 && d < 2) {
                v = v * 16 + hex_of(in[i++]);
                d++;
            }
            if (d == 0)
                return false;
            break;
        }
        default:
            if (e >= '0' && e <= '7') {
                v       = e - '0';
                usize d = 1;
                while (i < in.size() && in[i] >= '0' && in[i] <= '7' && d < 3) {
                    v = v * 8 + (in[i++] - '0');
                    d++;
                }
                break;
            }
            return false; // an escape upstream's bad() would refuse
        }
        if (k + 1 >= sizeof out.b)
            return false;
        out.b[k++] = char(v);
    }
    out.b[k] = 0;
    out.n    = k;
    return true;
}

// ---------------------------------------------------------------- the answer

// REG_NOMATCH through REG_BADRPT, spelled as testregex.c's codes[] spells them.
// ECOLLATE has no code here, so an answer naming it can only be met by BADPAT.
const char *const CODE_NAME[] = { "",        "NOMATCH", "BADPAT", "ECTYPE", "EESCAPE",
                                  "ESUBREG", "EBRACK",  "EPAREN", "EBRACE", "BADBR",
                                  "ERANGE",  "ESPACE",  "BADRPT" };

const usize NCODE = sizeof CODE_NAME / sizeof CODE_NAME[0];

Str code_name(int rc)
{
    return rc > 0 && usize(rc) < NCODE ? Str(CODE_NAME[rc]) : Str("UNKNOWN");
}

// An answer that is a bare regcomp error name rather than a match array.
// NOMATCH is not one: it is what regexec answers. ECOLLATE is, even though
// nothing here returns it.
bool is_code_answer(Str ans)
{
    if (ans == "NOMATCH")
        return false;
    if (ans == "ECOLLATE")
        return true;
    for (usize i = 2; i < NCODE; i++)
        if (ans == Str(CODE_NAME[i]))
            return true;
    return false;
}

// The match array as testregex.c prints it: (m,n) per slot, ? for -1, X for the
// untouched sentinel, and trailing (-1,-1) dropped.
void put_match(Buf<256> &b, const regmatch_t *m, int nmatch)
{
    int last = 0;
    for (int i = 0; i < nmatch; i++)
        if (m[i].rm_so != -1 || m[i].rm_eo != -1)
            last = i;
    for (int i = 0; i <= last; i++) {
        b.put('(');
        for (int half = 0; half < 2; half++) {
            regoff_t v = half ? m[i].rm_eo : m[i].rm_so;
            if (half)
                b.put(',');
            if (v == -1)
                b.put('?');
            else if (v == UNSET)
                b.put('X');
            else
                b.put(int(v));
        }
        b.put(')');
    }
}

// matchcheck(): every entry the answer lists must match exactly, every slot
// past them must be (-1,-1), and the slot past nmatch must be untouched.
bool answer_ok(Str ans, const regmatch_t *m, int nmatch)
{
    usize i = 0;
    int k   = 0;

    while (i < ans.size() && k < nmatch) {
        if (ans[i] != '(')
            return false;
        i++;
        regoff_t got[2];
        for (int half = 0; half < 2; half++) {
            if (i < ans.size() && ans[i] == '?') {
                got[half] = -1;
                i++;
            } else {
                if (i >= ans.size() || !is_digit(ans[i]))
                    return false;
                regoff_t v = 0;
                while (i < ans.size() && is_digit(ans[i]))
                    v = v * 10 + (ans[i++] - '0');
                got[half] = v;
            }
            if (i >= ans.size() || ans[i] != (half ? ')' : ','))
                return false;
            i++;
        }
        if (m[k].rm_so != got[0] || m[k].rm_eo != got[1])
            return false;
        k++;
    }
    if (i != ans.size())
        return false; // more entries than nmatch: the answer was not consumed

    for (; k < nmatch; k++)
        if (m[k].rm_so != -1 || m[k].rm_eo != -1)
            return false;

    return m[nmatch].rm_so == UNSET && m[nmatch].rm_eo == UNSET;
}

// ----------------------------------------------------------------- the cases

// Where this engine departs from the corpus on purpose. An entry suppresses the
// failure and asserts it: a listed case that starts passing is reported too, so
// nothing moves in either direction unremarked.
struct Deviation {
    const char *file;
    u32 line;
    const char *why;
};

// Four classes, and only the first is a documented absence. The other three are
// one property this backtracker does not have: POSIX assigns subexpressions by
// the leftmost-longest rule applied outward, where a backtracker reports
// whichever split it reached success by. The whole match is right in every case
// below but the two marked `leftmost`.
//
//   absent      regex.h says the feature is not here
//   subexpr     the groups inside a correct whole match are split another way
//   iteration   a starred group's last iteration, and what it leaves behind
//   leftmost    the missing empty iteration moves the whole match
const Deviation DEVIATIONS[] = {
    { "basic.dat", 61, "absent: [[.x.]], and ECOLLATE has no code here" },
    { "basic.dat", 62, "absent: [[=x=]], and ECOLLATE has no code here" },

    { "forcedassoc.dat", 9, "subexpr: the outer group takes the shorter side" },
    { "forcedassoc.dat", 10, "subexpr: the outer group takes the shorter side" },
    { "forcedassoc.dat", 11, "subexpr: the first alternative wins the prefix" },
    { "forcedassoc.dat", 12, "subexpr: the first alternative wins the prefix" },
    { "forcedassoc.dat", 17, "subexpr: (a*) takes 'a' where POSIX leaves it empty" },
    { "forcedassoc.dat", 18, "subexpr: (a*) takes 'a' where POSIX leaves it empty" },
    { "forcedassoc.dat", 23, "subexpr: (a*) takes 'a' where POSIX leaves it empty" },
    { "forcedassoc.dat", 24, "subexpr: (a*) takes 'a' where POSIX leaves it empty" },
    { "forcedassoc.dat", 29, "subexpr: (a|ab) takes 'a' where POSIX takes 'ab'" },

    { "nullsubexpr.dat", 45, "iteration: (z) keeps an extent the last pass did not set" },
    { "nullsubexpr.dat", 58, "leftmost: the empty pass of \\(a*\\)* is not taken" },
    { "nullsubexpr.dat", 61, "leftmost: the empty pass of \\(a*\\)* is not taken" },
    { "nullsubexpr.dat", 72, "iteration: {2} reports the first pass, not the last" },
    { "nullsubexpr.dat", 73, "iteration: {2} reports the first pass, not the last" },

    { "repetition.dat", 46, "iteration: a branch the last pass skipped stays set" },
    { "repetition.dat", 49, "iteration: a branch the last pass skipped stays set" },
    { "repetition.dat", 57, "iteration: a branch the last pass skipped stays set" },
    { "repetition.dat", 67, "iteration: a branch the last pass skipped stays set" },
    { "repetition.dat", 69, "iteration: a branch the last pass skipped stays set" },
    { "repetition.dat", 94, "iteration: (.?) reports the last full pass, not the empty one" },
    { "repetition.dat", 103, "iteration: (.?) reports the last full pass, not the empty one" },
    { "repetition.dat", 130, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 131, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 132, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 133, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 135, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 136, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 137, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 138, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 140, "subexpr: the repeat splits 'bcd' rather than taking it" },
    { "repetition.dat", 141, "subexpr: the repeat splits 'bcd' rather than taking it" },
};

const Deviation *deviation_for(const char *file, u32 line)
{
    for (usize i = 0; i < sizeof DEVIATIONS / sizeof DEVIATIONS[0]; i++)
        if (Str(DEVIATIONS[i].file) == Str(file) && DEVIATIONS[i].line == line)
            return &DEVIATIONS[i];
    return nullptr;
}

struct Stats {
    u32 run  = 0;
    u32 fail = 0;
    u32 skip = 0; // a dialect or modifier this engine does not have
    u32 dev  = 0; // a case DEVIATIONS accounts for
};

void report(const char *file, u32 line, char dialect, Str pat, Str sub, Str want, Str got)
{
    Buf<256> msg;

    msg.put(Str(file)).put(':').put(line).put(' ').put(dialect);
    msg.put(" /").put(pat).put("/ on \"").put(sub).put("\" gave ").put(got);
    msg.put(", want ").put(want);
    test_check(false, msg.str(), __FILE_NAME__, __LINE__);
}

// One dialect of one specification line. Returns whether it passed, and fills
// `got` with what the engine actually answered.
bool run_one(Str pat, Str sub, Str ans, int cflags, int eflags, int nmatch, Buf<256> &got)
{
    Field p, s;

    if (!p.set(pat) || !s.set(sub)) {
        got.put("<field too long>");
        return false;
    }

    regex_t re;
    int rc = regcomp(&re, p.b, cflags);

    if (is_code_answer(ans)) {
        if (rc == 0) {
            got.put("OK");
            regfree(&re);
            return false;
        }
        got.put(code_name(rc));
        // Upstream's rule: any expected compile error is met by BADPAT too.
        return ans == code_name(rc) || rc == REG_BADPAT;
    }
    if (rc != 0) {
        got.put(code_name(rc));
        // Upstream tolerates the same substitution the other way round.
        return ans == "NOMATCH" && rc == REG_BADPAT;
    }

    regmatch_t m[NMATCH + 1];
    for (int i = 0; i <= nmatch; i++)
        m[i] = regmatch_t{ UNSET, UNSET };

    int xrc = regexec(&re, s.b, usize(nmatch), m, eflags);
    regfree(&re);

    if (ans == "NOMATCH") {
        if (xrc == 0) {
            put_match(got, m, nmatch);
            return false;
        }
        got.put("NOMATCH");
        return true;
    }
    if (xrc != 0) {
        got.put("NOMATCH");
        return false;
    }
    if (ans == "OK")
        return true;

    put_match(got, m, nmatch);
    if (!answer_ok(ans, m, nmatch))
        return false;

    // Upstream re-runs every passing test with REG_NOSUB, which must reach the
    // same verdict while reporting nothing about it.
    if (regcomp(&re, p.b, cflags | REG_NOSUB) != 0) {
        got.clear();
        got.put("<REG_NOSUB refused the pattern>");
        return false;
    }
    int nrc = regexec(&re, s.b, 0, nullptr, eflags);
    regfree(&re);
    if (nrc != 0) {
        got.clear();
        got.put("<REG_NOSUB found no match>");
        return false;
    }
    return true;
}

// --------------------------------------------------------------- the reader

// A file's parse state. `skip` is the { } block; the verify fields are the
// ? | ; groups, which only categorize.dat uses.
struct Reader {
    const char *file;
    Stats *st;

    Field prev_pat; // what SAME refers to
    bool have_prev = false;
    bool skipping  = false;

    bool in_group   = false;
    bool group_done = false;
    Buf<64> verdict;
};

// Runs one specification line in every dialect it names, counting into `st`.
// Returns whether all of them passed, which is what { and ?/| ask. `loud` is
// off for those two: a feature probe and a categorisation are questions, and
// the answer to a question is not a failure.
bool run_spec(Reader &r, Stats &st, u32 line, Str spec, Str f[5], usize nf, bool loud)
{
    if (nf < 4)
        return false;

    int extra   = 0;
    int eflags  = 0;
    int nmatch  = NMATCH;
    bool expand = false;
    bool known  = true;
    char dial[4];
    int ndial = 0;

    for (usize i = 0; i < spec.size(); i++) {
        char c = spec[i];
        if (c == 'B' || c == 'E') {
            if (ndial < 4)
                dial[ndial++] = c;
        } else if (c == 'i') {
            extra |= REG_ICASE;
        } else if (c == 'n') {
            extra |= REG_NEWLINE;
        } else if (c == 'b') {
            eflags |= REG_NOTBOL;
        } else if (c == 'e') {
            eflags |= REG_NOTEOL;
        } else if (c == '$') {
            expand = true;
        } else if (is_digit(c)) {
            nmatch = 0;
            while (i < spec.size() && is_digit(spec[i]))
                nmatch = nmatch * 10 + (spec[i++] - '0');
            i--;
            if (nmatch < 1 || nmatch > NMATCH)
                known = false;
        } else if (c == '?' || c == '|' || c == ';' || c == '{' || c == '}') {
            // A control prefix, handled by the caller.
        } else {
            known = false; // A, S, K, L and the modifiers this engine lacks
        }
    }

    if (!known || ndial == 0) {
        st.skip++;
        return false;
    }

    Field pat, sub;
    if (f[1] == "SAME") {
        if (!r.have_prev)
            return false;
        pat = r.prev_pat;
    } else if (expand) {
        if (!expand_escapes(f[1], pat))
            return false;
    } else if (!pat.set(f[1])) {
        return false;
    }
    r.prev_pat  = pat;
    r.have_prev = true;

    Str subject = f[2] == "NULL" ? Str("") : f[2];
    if (expand) {
        if (!expand_escapes(subject, sub))
            return false;
    } else if (!sub.set(subject)) {
        return false;
    }

    bool all = true;
    for (int d = 0; d < ndial; d++) {
        int cflags = (dial[d] == 'E' ? REG_EXTENDED : 0) | extra;
        Buf<256> got;
        st.run++;

        bool ok = run_one(Str(pat.b, pat.n), Str(sub.b, sub.n), f[3], cflags, eflags, nmatch, got);

        const Deviation *dev = loud ? deviation_for(r.file, line) : nullptr;
        if (dev) {
            st.dev++;
            if (ok) {
                Buf<256> msg;
                msg.put(Str(r.file)).put(':').put(line);
                msg.put(" no longer deviates -- drop the entry: ").put(Str(dev->why));
                test_check(false, msg.str(), __FILE_NAME__, __LINE__);
            }
            continue; // asserted either way, never counted as a failure
        }
        if (!ok) {
            all = false;
            st.fail++;
            if (loud)
                report(r.file, line, dial[d], Str(pat.b, pat.n), Str(sub.b, sub.n), f[3],
                       got.str());
        }
    }
    return all;
}

// One file, upstream's line numbers. `loud` reports each failure; the
// categorisation files run quiet, since what they measure is not a defect.
// `verdicts` collects categorize.dat's answers, one per line.
void run_file(const char *file, Str text, Stats &st, bool loud, Buf<1024> *verdicts)
{
    Reader r;
    r.file = file;
    r.st   = &st;

    // The literal opens with the newline after R"DAT(; upstream's line 1 is
    // what follows it.
    Str rest = text.starts_with("\n") ? text.substr(1) : text;
    u32 line = 0;

    while (rest.size()) {
        Str raw = rest.split('\n', rest);
        line++;

        while (raw.size() && (raw[raw.size() - 1] == '\r' || raw[raw.size() - 1] == ' '))
            raw = raw.substr(0, raw.size() - 1);

        // :label: prefixes the line rather than commenting it out.
        if (raw.size() > 1 && raw[0] == ':' && raw[1] != ' ' && raw[1] != '\t') {
            usize end = raw.find(':', 1);
            if (end == Str::npos)
                continue;
            raw = raw.substr(end + 1);
        }
        if (raw.size() == 0 || raw[0] == '#' || raw[0] == 'T' || raw[0] == 'N' || raw[0] == ':')
            continue;

        Str f[5];
        usize nf = split_fields(raw, f);
        if (nf == 0)
            continue;
        Str spec = f[0];

        if (spec == "}") {
            r.skipping = false;
            continue;
        }
        if (r.skipping)
            continue;

        char lead = spec[0];

        if (lead == '{') {
            // A feature probe: if it fails, the block it opens is not this
            // engine's to answer and is skipped whole.
            Stats probe;
            r.skipping = !run_spec(r, probe, line, spec.substr(1), f, nf, false);
            if (r.skipping)
                st.skip++;
            continue;
        }
        if (lead == '?' || lead == '|') {
            if (lead == '?') {
                r.in_group   = true;
                r.group_done = false;
                r.verdict.clear();
            }
            if (!r.in_group || r.group_done)
                continue;
            Stats probe;
            if (run_spec(r, probe, line, spec.substr(1), f, nf, false)) {
                r.group_done = true;
                if (nf > 4)
                    r.verdict.put(f[4]);
            }
            continue;
        }
        if (lead == ';') {
            if (r.in_group && !r.group_done && nf > 1)
                r.verdict.put(f[1]);
            if (r.in_group && verdicts) {
                verdicts->put(r.verdict.str().size() ? r.verdict.str() : Str("EXPECTED"));
                verdicts->put('\n');
            }
            r.in_group = false;
            continue;
        }
        run_spec(r, st, line, spec, f, nf, loud);
    }
}

// ---------------------------------------------------------------- the suites

// The four files every conforming implementation must pass whole.
void conformance()
{
    struct {
        const char *name;
        Str text;
    } files[] = {
        { "basic.dat", BASIC },
        { "forcedassoc.dat", FORCEDASSOC },
        { "nullsubexpr.dat", NULLSUBEXPR },
        { "repetition.dat", REPETITION },
    };

    for (usize i = 0; i < sizeof files / sizeof files[0]; i++) {
        Stats st;
        run_file(files[i].name, files[i].text, st, true, nullptr);

        Buf<128> msg;
        msg.put("attregex ").put(Str(files[i].name)).put(": ").put(st.run).put(" run, ");
        msg.put(st.fail).put(" failed, ").put(st.dev).put(" deviations, ");
        msg.put(st.skip).put(" skipped");
        log(msg.str());
    }
}

// AT&T's associativity categorisation: an implementation passes one of the two
// files whole and fails the other whole. POSIX does not choose between them, so
// neither does this -- what is asserted is that the engine is one or the other,
// and which one it is.
void associativity()
{
    Stats left, right;

    run_file("leftassoc.dat", LEFTASSOC, left, false, nullptr);
    run_file("rightassoc.dat", RIGHTASSOC, right, false, nullptr);

    CHECK(left.run == 12 && right.run == 12);

    // Neither, as it happens: this engine is right-associative on eight of the
    // twelve and left on four, which is the same subexpression-assignment gap
    // forcedassoc.dat's deviations name. Pinned exactly so it cannot drift
    // unremarked -- passing either file whole would fail these.
    CHECK_EQ(left.fail, 8);
    CHECK_EQ(right.fail, 4);
}

// categorize.dat is a report rather than a suite: fourteen groups, each naming
// the category the engine falls into. Pinning the fourteen answers turns the
// report into an assertion, so a change in the engine names the axis it moved.
void categorisation()
{
    // What the fourteen groups answer for this engine, in file order. The
    // first four are the axes; the rest are AT&T's names for a wrong answer,
    // and an "-UNKNOWN" is the `;` fallback -- the engine is wrong in a way
    // upstream has no name for.
    const char *const WANT =
        "POSITION=leftmost\n"
        "ASSOCIATIVITY=right\n"
        "SUBEXPRESSION=grouping\n"
        "REPEAT_LONGEST=first\n"
        "BUG=alternation-order\n"
        "EXPECTED\n"
        "EXPECTED\n"
        "EXPECTED\n"
        "EXPECTED\n"
        "EXPECTED\n"
        "EXPECTED\n"
        "BUG=repeat-artifact\n"
        "BUG=repeat-artifact-nomatch\n"
        "EXPECTED\n";

    Stats st;
    Buf<1024> got;

    run_file("categorize.dat", CATEGORIZE, st, false, &got);
    if (got.str() == Str(WANT))
        return;

    // The block is longer than one report line, so it goes to the log whole.
    log("attregex categorize.dat, as the engine answers it now:");
    log(got.str());
    test_check(false, "categorize.dat moved -- see the log above", __FILE_NAME__, __LINE__);
}

} // namespace

void test_attregex()
{
    test_begin("attregex");

    conformance();
    associativity();
    categorisation();
}
