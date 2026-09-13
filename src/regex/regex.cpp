// A compact POSIX engine: recursive-descent parser into a node arena, then a
// backtracking matcher over an explicit continuation list.
//
// Leftmost-longest, not leftmost-first: accept() records the end and returns
// false, so every match at a start position is enumerated and the longest kept.
// Subexpressions are POSIX's as well: the parse a match was reached by is
// recorded as a trail of node instances, and among matches of one length the
// winner of trail_cmp_node() -- leftmost-longest applied outward -- is kept.
//
// Offsets are bytes. `.` and a bracket consume a whole UTF-8 sequence, so a
// match never ends mid-character. The subject is a region, not a C string:
// nothing reads a NUL as an end, so a NUL in it is a byte like any other.

#include "regex/regex.h"

#include "kernel/alloc.h"
#include "kernel/text.h"

namespace {

enum {
    OP_LIT,     // bytes[0..len) verbatim, or the rune folded under REG_ICASE
    OP_ANY,     // .
    OP_SET,     // [...]
    OP_BOL,     // ^
    OP_EOL,     // $
    OP_GROUP,   // ( ... )
    OP_CLOSE,   // the end of a group, so the capture closes on the way out
    OP_ALT,     // branches chained through `alt`
    OP_REP,     // child repeated min..max, max < 0 for unbounded
    OP_BACKREF, // \1 .. \9
};

struct Node {
    int op;
    int next;  // sibling in a sequence, -1 at the end
    int child; // GROUP/REP body, ALT first branch
    int alt;   // ALT next branch
    int min, max;
    int group; // GROUP/CLOSE/BACKREF; REP: first group in the body
    int set;   // SET
    int close; // GROUP: its OP_CLOSE node; CLOSE: its OP_GROUP; REP: last group
    int len;   // LIT: bytes; REP: 1 when it sits in a body with no group in it
    unsigned char bytes[4];
    unsigned int rune; // LIT: what bytes decode to, for REG_ICASE
};

// 12 POSIX classes, applied to runes past ASCII; ASCII lands in `bits`.
enum {
    CL_ALPHA  = 1,
    CL_DIGIT  = 2,
    CL_ALNUM  = 4,
    CL_UPPER  = 8,
    CL_LOWER  = 16,
    CL_SPACE  = 32,
    CL_BLANK  = 64,
    CL_PUNCT  = 128,
    CL_PRINT  = 256,
    CL_GRAPH  = 512,
    CL_CNTRL  = 1024,
    CL_XDIGIT = 2048,
};

struct Set {
    unsigned char bits[16]; // ASCII membership
    int neg;
    int rfirst, rcount; // into the range arena
    int classes;
};

struct Range {
    unsigned int lo, hi;
};

bool rune_in_class(unsigned int c, int bit)
{
    char32_t r = char32_t(c);

    switch (bit) {
    case CL_ALPHA:
        return rune_is_alpha(r);
    case CL_DIGIT:
        return rune_is_digit(r);
    case CL_ALNUM:
        return rune_is_alnum(r);
    case CL_UPPER:
        return rune_is_upper(r);
    case CL_LOWER:
        return rune_is_lower(r);
    case CL_SPACE:
        return rune_is_space(r);
    case CL_BLANK:
        return rune_is_blank(r);
    case CL_PUNCT:
        return rune_is_punct(r);
    case CL_PRINT:
        return rune_is_print(r);
    case CL_GRAPH:
        return rune_is_graph(r);
    case CL_CNTRL:
        return rune_is_cntrl(r);
    case CL_XDIGIT:
        return rune_is_xdigit(r);
    }
    return false;
}

// --------------------------------------------------------------- the arenas

struct Build {
    regex_t *re;
    const char *p;
    const char *pend;
    int ngroup;
    int err;
    bool bre; // no REG_EXTENDED
    bool icase;
};

Node *nodes(const regex_t *re)
{
    return (Node *)re->prog;
}
Set *sets(const regex_t *re)
{
    return (Set *)re->sets;
}
Range *ranges(const regex_t *re)
{
    return (Range *)re->ranges;
}

// heap_alloc has no realloc, so a full arena is copied into a doubled one.
void *grow(void *p, int have, int want, usize elem)
{
    void *q = heap_alloc(usize(want) * elem);

    if (!q)
        return nullptr;
    if (p) {
        __builtin_memcpy(q, p, usize(have) * elem);
        heap_free(p);
    }
    return q;
}

int new_node(Build *b, int op)
{
    regex_t *re = b->re;

    if (re->nnodes == re->cnodes) {
        int want = re->cnodes ? re->cnodes * 2 : 16;
        void *q  = grow(re->prog, re->nnodes, want, sizeof(Node));
        if (!q) {
            b->err = REG_ESPACE;
            return -1;
        }
        re->prog   = q;
        re->cnodes = want;
    }
    Node *n = &nodes(re)[re->nnodes];
    __builtin_memset(n, 0, sizeof(*n));
    n->op    = op;
    n->next  = -1;
    n->child = -1;
    n->alt   = -1;
    n->set   = -1;
    n->close = -1;
    n->max   = -1;
    return re->nnodes++;
}

int new_set(Build *b)
{
    regex_t *re = b->re;

    if (re->nsets == re->csets) {
        int want = re->csets ? re->csets * 2 : 4;
        void *q  = grow(re->sets, re->nsets, want, sizeof(Set));
        if (!q) {
            b->err = REG_ESPACE;
            return -1;
        }
        re->sets  = q;
        re->csets = want;
    }
    Set *s = &sets(re)[re->nsets];
    __builtin_memset(s, 0, sizeof(*s));
    s->rfirst = re->nranges;
    return re->nsets++;
}

int add_range(Build *b, unsigned int lo, unsigned int hi)
{
    regex_t *re = b->re;

    if (re->nranges == re->cranges) {
        int want = re->cranges ? re->cranges * 2 : 8;
        void *q  = grow(re->ranges, re->nranges, want, sizeof(Range));
        if (!q) {
            b->err = REG_ESPACE;
            return -1;
        }
        re->ranges  = q;
        re->cranges = want;
    }
    Range *r = &ranges(re)[re->nranges];
    r->lo    = lo;
    r->hi    = hi;
    return re->nranges++;
}

// ------------------------------------------------------------------- UTF-8

// Bytes in the sequence at s, or 1 for anything malformed or cut short.
int seqlen(const char *s, const char *end)
{
    unsigned char c = (unsigned char)*s;
    int have        = int(end - s);

    if (c < 0xC2)
        return 1;
    if (c < 0xE0)
        return (have >= 2 && (s[1] & 0xC0) == 0x80) ? 2 : 1;
    if (c < 0xF0)
        return (have >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) ? 3 : 1;
    if (c < 0xF5)
        return (have >= 4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
                (s[3] & 0xC0) == 0x80)
                   ? 4
                   : 1;
    return 1;
}

// The rune at s and its length; a malformed byte decodes as itself.
int decode(const char *s, const char *end, unsigned int *out)
{
    int n           = seqlen(s, end);
    unsigned char c = (unsigned char)*s;

    if (n == 1) {
        *out = c;
        return 1;
    }
    unsigned int v = c & (unsigned int)(0xFF >> (n + 1));
    for (int i = 1; i < n; i++)
        v = (v << 6) | ((unsigned char)s[i] & 0x3F);
    *out = v;
    return n;
}

unsigned int fold(unsigned int c)
{
    return (unsigned int)rune_lower(char32_t(c));
}

bool is_dig(char c)
{
    return c >= '0' && c <= '9';
}

// strlen and memcmp have no wasm instruction behind them, so they would be
// libc calls and there is no libc here.
usize str_len(const char *s)
{
    const char *p = s;

    while (*p)
        p++;
    return usize(p - s);
}

bool same_bytes(const char *a, const char *b, usize n)
{
    for (usize i = 0; i < n; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

// ------------------------------------------------------------------ parser

int parse_alt(Build *b);

void set_bit(Set *s, unsigned int c)
{
    if (c < 128)
        s->bits[c >> 3] |= (unsigned char)(1 << (c & 7));
}

int class_named(const char *name, int len)
{
    struct {
        const char *n;
        int len, bit;
    } static const T[] = {
        { "alpha", 5, CL_ALPHA }, { "digit", 5, CL_DIGIT }, { "alnum", 5, CL_ALNUM },
        { "upper", 5, CL_UPPER }, { "lower", 5, CL_LOWER }, { "space", 5, CL_SPACE },
        { "blank", 5, CL_BLANK }, { "punct", 5, CL_PUNCT }, { "print", 5, CL_PRINT },
        { "graph", 5, CL_GRAPH }, { "cntrl", 5, CL_CNTRL }, { "xdigit", 6, CL_XDIGIT },
    };

    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        if (T[i].len == len && same_bytes(T[i].n, name, (usize)len))
            return T[i].bit;
    }
    return 0;
}

// One endpoint of a bracket item. [.x.] and [=x=] name x; a name that is not
// one character is REG_ECOLLATE.
bool bracket_point(Build *b, unsigned int *out)
{
    if (b->p[0] == '[' && (b->p[1] == '.' || b->p[1] == '=')) {
        char kind     = b->p[1];
        const char *q = b->p + 2;

        while (*q && !(q[0] == kind && q[1] == ']'))
            q++;
        if (*q == '\0') {
            b->err = REG_EBRACK;
            return false;
        }
        if (q == b->p + 2 || b->p + 2 + decode(b->p + 2, q, out) != q) {
            b->err = REG_ECOLLATE;
            return false;
        }
        b->p = q + 2;
        return true;
    }
    if (*b->p == '\\' && b->p[1]) {
        b->p++;
        *out = (unsigned char)*b->p++;
        return true;
    }
    b->p += decode(b->p, b->pend, out);
    return true;
}

// [ has been consumed. Shared by both arms: a bracket is the same in each.
int parse_bracket(Build *b)
{
    int si = new_set(b);

    if (si < 0)
        return -1;
    if (*b->p == '^') {
        b->p++;
        sets(b->re)[si].neg = 1;
    }

    int first = 1;
    for (;;) {
        if (*b->p == '\0') {
            b->err = REG_EBRACK;
            return -1;
        }
        if (*b->p == ']' && !first)
            break;
        first = 0;

        // [:class:]
        if (b->p[0] == '[' && b->p[1] == ':') {
            const char *q = b->p + 2;
            while (*q && !(q[0] == ':' && q[1] == ']'))
                q++;
            if (*q == '\0') {
                b->err = REG_EBRACK;
                return -1;
            }
            int bit = class_named(b->p + 2, (int)(q - (b->p + 2)));
            if (!bit) {
                b->err = REG_ECTYPE;
                return -1;
            }
            Set *s = &sets(b->re)[si];
            s->classes |= bit;
            for (unsigned int c = 0; c < 128; c++)
                if (rune_in_class(c, bit))
                    set_bit(s, c);
            b->p = q + 2;
            continue;
        }

        unsigned int lo;
        if (!bracket_point(b, &lo))
            return -1;

        unsigned int hi = lo;
        if (b->p[0] == '-' && b->p[1] != ']' && b->p[1] != '\0') {
            b->p++;
            if (!bracket_point(b, &hi))
                return -1;
            if (hi < lo) {
                b->err = REG_ERANGE;
                return -1;
            }
        }

        Set *s = &sets(b->re)[si];
        if (hi < 128) {
            for (unsigned int c = lo; c <= hi; c++)
                set_bit(s, c);
        } else {
            for (unsigned int c = lo; c < 128 && c <= hi; c++)
                set_bit(s, c);
            if (add_range(b, lo < 128 ? 128 : lo, hi) < 0)
                return -1;
            sets(b->re)[si].rcount++;
        }
    }
    b->p++; // ]

    int ni = new_node(b, OP_SET);
    if (ni < 0)
        return -1;
    nodes(b->re)[ni].set = si;
    return ni;
}

// One \x escape as its byte.
int escape_byte(int c)
{
    switch (c) {
    case 'a':
        return '\a';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case 'v':
        return '\v';
    }
    return c;
}

int new_lit(Build *b, const unsigned char *bytes, int len, unsigned int rune)
{
    int ni = new_node(b, OP_LIT);

    if (ni < 0)
        return -1;
    Node *n = &nodes(b->re)[ni];
    for (int i = 0; i < len; i++)
        n->bytes[i] = bytes[i];
    n->len  = len;
    n->rune = rune;
    return ni;
}

// One byte, verbatim.
int lit_byte(Build *b, unsigned char c)
{
    return new_lit(b, &c, 1, c);
}

// The UTF-8 sequence at the cursor.
int lit_seq(Build *b)
{
    unsigned int r;
    int len = decode(b->p, b->pend, &r);
    unsigned char bytes[4];

    for (int i = 0; i < len; i++)
        bytes[i] = (unsigned char)b->p[i];
    b->p += len;
    return new_lit(b, bytes, len, r);
}

int new_backref(Build *b, int g)
{
    if (g > b->ngroup) {
        b->err = REG_ESUBREG;
        return -1;
    }
    int ni = new_node(b, OP_BACKREF);
    if (ni < 0)
        return -1;
    nodes(b->re)[ni].group = g;
    return ni;
}

// A group body: \( \) in a BRE, ( ) in an ERE.
int parse_group(Build *b)
{
    int g  = ++b->ngroup;
    int ni = new_node(b, OP_GROUP);

    if (ni < 0)
        return -1;
    int ci = new_node(b, OP_CLOSE);
    if (ci < 0)
        return -1;
    int inner = parse_alt(b);
    if (b->err)
        return -1;
    if (b->bre ? !(b->p[0] == '\\' && b->p[1] == ')') : *b->p != ')') {
        b->err = REG_EPAREN;
        return -1;
    }
    b->p += b->bre ? 2 : 1;
    Node *n     = nodes(b->re);
    n[ci].group = g;
    n[ci].close = ni; // back to the open, which is the trail entry to close
    n[ni].group = g;
    n[ni].child = inner;
    n[ni].close = ci;
    return ni;
}

// `$` is an anchor in a BRE only at the end of the RE or of a subexpression.
bool bre_dollar_ends(const Build *b)
{
    return b->p[1] == '\0' || (b->p[1] == '\\' && b->p[2] == ')');
}

int parse_atom_bre(Build *b, bool at_start)
{
    switch (*b->p) {
    case '\\':
        switch (b->p[1]) {
        case '\0':
            b->err = REG_EESCAPE;
            return -1;
        case '(':
            b->p += 2;
            return parse_group(b);
        case ')':
            b->err = REG_EPAREN;
            return -1;
        case '{':
            b->err = REG_BADRPT;
            return -1;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            int g = b->p[1] - '0';
            b->p += 2;
            return new_backref(b, g);
        }
        }
        b->p++;
        return lit_byte(b, (unsigned char)escape_byte((unsigned char)*b->p++));

    case '[':
        b->p++;
        return parse_bracket(b);
    case '.':
        b->p++;
        return new_node(b, OP_ANY);
    case '^':
        if (!at_start)
            break;
        b->p++;
        return new_node(b, OP_BOL);
    case '$':
        if (!bre_dollar_ends(b))
            break;
        b->p++;
        return new_node(b, OP_EOL);
    case '*':
        if (at_start)
            break; // nothing to repeat, so a literal
        b->err = REG_BADRPT;
        return -1;
    case '\0':
        b->err = REG_BADPAT;
        return -1;
    }
    return lit_seq(b);
}

int parse_atom_ere(Build *b)
{
    switch (*b->p) {
    case '(':
        b->p++;
        return parse_group(b);
    case '[':
        b->p++;
        return parse_bracket(b);
    case '.':
        b->p++;
        return new_node(b, OP_ANY);
    case '^':
        b->p++;
        return new_node(b, OP_BOL);
    case '$':
        b->p++;
        return new_node(b, OP_EOL);
    case ')':
        b->err = REG_EPAREN;
        return -1;
    case '|':
    case '\0':
        b->err = REG_BADPAT;
        return -1;
    case '*':
    case '+':
    case '?':
        b->err = REG_BADRPT;
        return -1;
    case '\\':
        switch (b->p[1]) {
        case '\0':
            b->err = REG_EESCAPE;
            return -1;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            int g = b->p[1] - '0';
            b->p += 2;
            return new_backref(b, g);
        }
        }
        b->p++;
        return lit_byte(b, (unsigned char)escape_byte((unsigned char)*b->p++));
    }
    return lit_seq(b);
}

enum { DUP_MAX = 32767 };

// One bound. Accumulation stops at DUP_MAX so that a long run of digits cannot
// wrap into a small count; the result stays above it for the caller to refuse.
int scan_bound(const char **q)
{
    int v = 0;

    while (is_dig(**q)) {
        if (v <= DUP_MAX)
            v = v * 10 + (*(*q)++ - '0');
        else
            (*q)++;
    }
    return v;
}

// {m,n} in an ERE, \{m,n\} in a BRE; the brace has been seen.
int parse_interval(Build *b, int *mn, int *mx)
{
    const char *q = b->p + (b->bre ? 2 : 1);

    *mn = scan_bound(&q);
    if (*q == ',') {
        q++;
        *mx = is_dig(*q) ? scan_bound(&q) : -1;
    } else {
        *mx = *mn;
    }
    if (b->bre ? !(q[0] == '\\' && q[1] == '}') : *q != '}') {
        b->err = REG_EBRACE;
        return -1;
    }
    if (*mn > DUP_MAX || *mx > DUP_MAX) {
        b->err = REG_BADBR;
        return -1;
    }
    if (0 <= *mx && *mx < *mn) {
        b->err = REG_BADBR;
        return -1;
    }
    b->p = q + (b->bre ? 2 : 1);
    return 0;
}

bool at_interval(const Build *b)
{
    if (b->bre)
        return b->p[0] == '\\' && b->p[1] == '{' && is_dig(b->p[2]);
    return b->p[0] == '{' && is_dig(b->p[1]);
}

int parse_rep(Build *b, bool at_start)
{
    int g0 = b->ngroup;
    int ai = b->bre ? parse_atom_bre(b, at_start) : parse_atom_ere(b);

    if (ai < 0)
        return -1;
    // A leading ^ is not something a BRE's * can repeat: ^* is a literal star.
    if (b->bre && at_start && nodes(b->re)[ai].op == OP_BOL)
        return ai;
    for (;;) {
        int mn, mx;

        if (*b->p == '*') {
            mn = 0;
            mx = -1;
            b->p++;
        } else if (!b->bre && *b->p == '+') {
            mn = 1;
            mx = -1;
            b->p++;
        } else if (!b->bre && *b->p == '?') {
            mn = 0;
            mx = 1;
            b->p++;
        } else if (at_interval(b)) {
            if (parse_interval(b, &mn, &mx) < 0)
                return -1;
        } else {
            return ai;
        }

        int ri = new_node(b, OP_REP);
        if (ri < 0)
            return -1;
        Node *n     = nodes(b->re);
        n[ri].child = ai;
        n[ri].min   = mn;
        n[ri].max   = mx;
        // The body's groups, cleared at the head of every turn; an empty range
        // when it has none. Stacked repeats (a**) share the one body.
        n[ri].group = g0 + 1;
        n[ri].close = b->ngroup;
        n[ai].next  = -1;
        ai          = ri;
    }
}

bool at_cat_end(const Build *b)
{
    if (*b->p == '\0')
        return true;
    if (b->bre)
        return b->p[0] == '\\' && b->p[1] == ')';
    return *b->p == '|' || *b->p == ')';
}

// A concatenation, returned as the head of a `next` chain.
int parse_cat(Build *b)
{
    int head = -1, tail = -1;
    bool at_start = true;

    while (!at_cat_end(b)) {
        int ni = parse_rep(b, at_start);
        if (ni < 0)
            return -1;
        if (tail < 0)
            head = ni;
        else
            nodes(b->re)[tail].next = ni;
        tail = ni;
        // A leading ^ leaves the next atom still at the start.
        at_start = b->bre && nodes(b->re)[ni].op == OP_BOL;
    }
    return head;
}

// An empty branch is the empty sequence, which is -1 and would end the branch
// chain. A REP{0,0} matches empty and keeps the chain walkable.
int empty_branch(Build *b)
{
    int ni = new_node(b, OP_REP);

    if (ni < 0)
        return -1;
    Node *n     = nodes(b->re);
    n[ni].child = -1;
    n[ni].min   = 0;
    n[ni].max   = 0;
    return ni;
}

// A repeat with no group in its body has nothing to tell one turn from another,
// so only its own extent is traced and nothing under it is. The continuation
// runs inside the body's call, so this cannot be a depth counter at match time.
void mark_trace(regex_t *re, int ni, bool off)
{
    while (ni >= 0) {
        Node *n = &nodes(re)[ni];

        if (n->op == OP_ALT) {
            for (int b = n->child; b >= 0; b = nodes(re)[b].alt)
                mark_trace(re, b, off);
        } else if (n->op == OP_REP) {
            n->len = off;
            mark_trace(re, n->child, off || n->close < n->group);
        } else {
            mark_trace(re, n->child, off);
        }
        ni = n->next;
    }
}

// A BRE has no alternation, so this is one concatenation there.
int parse_alt(Build *b)
{
    int first = parse_cat(b);

    if (b->err)
        return -1;
    if (b->bre || *b->p != '|')
        return first;

    if (first < 0 && (first = empty_branch(b)) < 0)
        return -1;

    int ai = new_node(b, OP_ALT);
    if (ai < 0)
        return -1;
    nodes(b->re)[ai].child = first;

    int tail = first;
    while (*b->p == '|') {
        b->p++;
        int br = parse_cat(b);
        if (b->err)
            return -1;
        if (br < 0 && (br = empty_branch(b)) < 0)
            return -1;
        nodes(b->re)[tail].alt = br;
        tail                   = br;
    }
    return ai;
}

// ------------------------------------------------------------- the matcher

enum { NCAP = 10 };

struct Cont {
    int kind; // 0 sequence, 1 another turn of a repeat
    int node;
    int count;
    const char *from; // REP: where this turn began
    int rep, turn;    // REP: trail entries of the repeat and of this turn
    const Cont *up;
};

// One instance of a traced node in the parse being tried: a group, a repeat, or
// one turn of a repeat, whose `node` is the repeat's. `parent` indexes the
// instance it sits in, so the array is the parse tree in preorder.
struct Mark {
    int node;
    int parent;
    regoff_t so, eo;
};

// What a failed subtree puts back.
struct Trail {
    int n, parent;
};

enum { TRAIL_MAX = 4096 };

struct Exec {
    const regex_t *re;
    const char *base;  // offsets are from here
    const char *start; // the subject region
    const char *end;
    int eflags;
    bool newline;
    bool icase;
    long budget;
    int depth;

    // Where each turn of a simple repeat ended, so the backing off is a walk
    // down this rather than a return down the native stack.
    const char **marks;
    int nmarks, cmarks;

    regmatch_t *cap;
    int ncap;
    const char *best; // longest end seen at this start, or null
    regmatch_t *bestcap;

    // The parse being tried, and the one `best` was reached by. Off unless
    // captures were asked for, and off again past TRAIL_MAX.
    Mark *trail;
    int ntrail, ctrail, curparent;
    Mark *besttrail;
    int nbesttrail, cbesttrail;
    bool trace;
};

enum { MAX_DEPTH = 2000 };

bool mseq(Exec *e, int ni, const char *s, const Cont *k);

bool mcont(Exec *e, const Cont *k, const char *s);

bool mrep(Exec *e, int ni, const char *s, int count, int ti, const Cont *k);

// --------------------------------------------------------------- the trail

Trail trail_mark(const Exec *e)
{
    return { e->ntrail, e->curparent };
}

void trail_reset(Exec *e, Trail t)
{
    e->ntrail    = t.n;
    e->curparent = t.parent;
}

// An instance opens and what follows nests inside it; -1 when untraced, which
// the caller carries to the close.
int trail_open(Exec *e, int node, const char *s)
{
    if (!e->trace)
        return -1;
    if (e->ntrail == e->ctrail) {
        int want = e->ctrail ? e->ctrail * 2 : 64;
        Mark *m  = TRAIL_MAX < want ? nullptr
                                    : (Mark *)grow(e->trail, e->ntrail, want, sizeof(*m));
        if (!m) {
            e->trace = false; // past the cap the incumbent stands
            return -1;
        }
        e->trail  = m;
        e->ctrail = want;
    }
    Mark *m      = &e->trail[e->ntrail];
    m->node      = node;
    m->parent    = e->curparent;
    m->so        = s - e->base;
    m->eo        = -1;
    e->curparent = e->ntrail;
    return e->ntrail++;
}

void trail_close(Exec *e, int i, const char *s)
{
    if (i < 0)
        return;
    e->trail[i].eo = s - e->base;
    e->curparent   = e->trail[i].parent;
}

// A group's own instance is the innermost open one: its body has closed.
void trail_close_group(Exec *e, int node, const char *s)
{
    int i = e->curparent;

    if (0 <= i && e->trail[i].node == node)
        trail_close(e, i, s);
}

// The trail is preorder, so a parent's subtree is contiguous and a shallower
// entry ends the walk over its children.
int trail_kid(const Mark *m, int n, int parent, int i)
{
    for (int j = i; j < n; j++) {
        if (m[j].parent == parent)
            return j;
        if (m[j].parent < parent)
            break;
    }
    return -1;
}

int trail_cmp_kids(const Exec *e, const Mark *A, int na, int a, const Mark *B, int nb, int b);

// -1 when A is POSIX's answer, 1 when B is, 0 when they do not differ.
int trail_cmp_node(const Exec *e, const Mark *A, int na, int a, const Mark *B, int nb, int b)
{
    // Which subexpression it is comes first: the one opening earlier in the
    // pattern is the one POSIX prefers to see take part. Then leftmost, then
    // longest, then the same applied to what is inside.
    if (A[a].node != B[b].node)
        return A[a].node < B[b].node ? -1 : 1;
    if (A[a].so != B[b].so)
        return A[a].so < B[b].so ? -1 : 1;
    if (A[a].eo != B[b].eo)
        return A[a].eo > B[b].eo ? -1 : 1;
    return trail_cmp_kids(e, A, na, a, B, nb, b);
}

int trail_cmp_kids(const Exec *e, const Mark *A, int na, int a, const Mark *B, int nb, int b)
{
    // A repeat's children are its turns. A turn carries the repeat's node too,
    // but its own child is the one atom the body is, so it never runs out here.
    bool turns = 0 <= a && nodes(e->re)[A[a].node].op == OP_REP;
    int i      = trail_kid(A, na, a, a + 1);
    int j      = trail_kid(B, nb, b, b + 1);

    for (int k = 0;; k++) {
        if (i < 0 && j < 0)
            return 0;
        // A repeat takes as few turns as it can, but one empty turn beats none;
        // anywhere else the subexpression that took part wins.
        if (i < 0 || j < 0) {
            if (k && turns)
                return i < 0 ? -1 : 1;
            return i < 0 ? 1 : -1;
        }
        int r = trail_cmp_node(e, A, na, i, B, nb, j);
        if (r)
            return r;
        i = trail_kid(A, na, a, i + 1);
        j = trail_kid(B, nb, b, j + 1);
    }
}

// The trail the best match was reached by, kept beside its captures.
void trail_keep(Exec *e)
{
    if (e->cbesttrail < e->ntrail) {
        Mark *m = (Mark *)grow(e->besttrail, 0, e->ntrail, sizeof(*m));
        if (!m) {
            e->trace = false;
            return;
        }
        e->besttrail  = m;
        e->cbesttrail = e->ntrail;
    }
    for (int i = 0; i < e->ntrail; i++)
        e->besttrail[i] = e->trail[i];
    e->nbesttrail = e->ntrail;
}

// Membership, negation not applied yet: REG_ICASE folds before [^...] inverts,
// so [^a] refuses 'A' as POSIX says.
bool set_hit(const Exec *e, const Set *st, unsigned int c)
{
    int hit = 0;

    if (c < 128) {
        hit = (st->bits[c >> 3] >> (c & 7)) & 1;
    } else {
        const Range *r = ranges(e->re);
        for (int i = 0; i < st->rcount; i++) {
            if (r[st->rfirst + i].lo <= c && c <= r[st->rfirst + i].hi) {
                hit = 1;
                break;
            }
        }
        if (!hit && st->classes) {
            for (int bit = 1; bit <= CL_XDIGIT; bit <<= 1)
                if ((st->classes & bit) && rune_in_class(c, bit)) {
                    hit = 1;
                    break;
                }
        }
    }
    return hit != 0;
}

bool in_set(const Exec *e, const Set *st, unsigned int c)
{
    bool hit = set_hit(e, st, c);

    if (!hit && e->icase)
        hit = set_hit(e, st, (unsigned int)rune_lower(char32_t(c))) ||
              set_hit(e, st, (unsigned int)rune_upper(char32_t(c)));
    if (!st->neg)
        return hit;
    // A negated bracket never takes a newline under REG_NEWLINE.
    if (e->newline && c == '\n')
        return false;
    return !hit;
}

// Bytes a literal takes here, or 0 for no match.
int lit_width(const Exec *e, const Node *n, const char *s)
{
    if (!e->icase) {
        if (n->len > e->end - s)
            return 0;
        for (int i = 0; i < n->len; i++)
            if ((unsigned char)s[i] != n->bytes[i])
                return 0;
        return n->len;
    }
    if (s == e->end)
        return 0;

    unsigned int c;
    int len = decode(s, e->end, &c);
    return fold(c) == fold(n->rune) ? len : 0;
}

// The end of the whole pattern: record and refuse, so the search goes on and
// the longest match wins. Among ends that tie, the POSIX parse wins.
bool accept(Exec *e, const char *s)
{
    bool take = !e->best || e->best < s;

    if (!take && e->best == s && e->trace) {
        e->budget -= e->ntrail;
        take = trail_cmp_kids(e, e->trail, e->ntrail, -1, e->besttrail, e->nbesttrail, -1) < 0;
    }
    if (!take)
        return false;
    e->best = s;
    for (int i = 0; i < e->ncap; i++)
        e->bestcap[i] = e->cap[i];
    if (e->trace)
        trail_keep(e);
    return false;
}

bool mcont(Exec *e, const Cont *k, const char *s)
{
    if (!k)
        return accept(e, s);
    if (k->kind == 0)
        return mseq(e, k->node, s, k->up);

    Trail t = trail_mark(e);
    bool r;

    trail_close(e, k->turn, s);
    if (s == k->from) {
        // A turn that consumed nothing would repeat for ever, so stop
        // expanding. It stands for the turns still owed to min as well, which
        // are the same empty turn in the same place; mrep's fall-through
        // enumerates the parse without it, and the two are compared.
        trail_close(e, k->rep, s);
        r = mcont(e, k->up, s);
    } else {
        r = mrep(e, k->node, s, k->count, k->rep, k->up);
    }
    if (!r)
        trail_reset(e, t);
    return r;
}

bool mrep(Exec *e, int ni, const char *s, int count, int ti, const Cont *k)
{
    const Node *n = &nodes(e->re)[ni];

    if (n->max < 0 || count < n->max) {
        regmatch_t save[NCAP];
        Cont kk = { 1, ni, count + 1, s, ti, -1, k };
        Trail t = trail_mark(e);

        for (int i = 0; i < e->ncap; i++)
            save[i] = e->cap[i];
        // A turn is worth telling from the next only when a group is in it.
        kk.turn = n->group <= n->close ? trail_open(e, ni, s) : -1;
        // A turn reports what it matched, not what an earlier one did.
        for (int g = n->group; g <= n->close && g < e->ncap; g++)
            e->cap[g].rm_so = e->cap[g].rm_eo = -1;
        if (mseq(e, n->child, s, &kk))
            return true;
        trail_reset(e, t);
        for (int i = 0; i < e->ncap; i++)
            e->cap[i] = save[i];
    }
    if (count >= n->min) {
        Trail t = trail_mark(e);

        trail_close(e, ti, s);
        if (mcont(e, k, s))
            return true;
        trail_reset(e, t);
    }
    return false;
}

// How many bytes one turn of a body that is a single LIT, ANY or SET takes
// here, or 0 for none.
int body_width(const Exec *e, const Node *c, const char *s)
{
    unsigned int ch;
    int len;

    switch (c->op) {
    case OP_LIT:
        return lit_width(e, c, s);
    case OP_ANY:
        if (s == e->end)
            return 0;
        len = decode(s, e->end, &ch);
        return e->newline && ch == '\n' ? 0 : len;
    case OP_SET:
        if (s == e->end)
            return 0;
        len = decode(s, e->end, &ch);
        return in_set(e, &sets(e->re)[c->set], ch) ? len : 0;
    }
    return 0;
}

bool push_mark(Exec *e, const char *s)
{
    if (e->nmarks == e->cmarks) {
        int want       = e->cmarks ? e->cmarks * 2 : 256;
        const char **m = (const char **)grow(e->marks, e->nmarks, want, sizeof(*m));
        if (!m)
            return false;
        e->marks  = m;
        e->cmarks = want;
    }
    e->marks[e->nmarks++] = s;
    return true;
}

// A body that is one LIT, ANY or SET has no groups and a fixed shape, so the
// turns can be taken in a loop and backed off down a mark stack. That keeps
// `.*` on a long line off the native stack, where one frame per character
// would trap.
bool mrep_simple(Exec *e, int ni, const char *s, int ti, const Cont *k)
{
    const Node *n = &nodes(e->re)[ni];
    const Node *c = &nodes(e->re)[n->child];
    int base      = e->nmarks;
    int count     = 0;

    if (!push_mark(e, s))
        return false;
    while (n->max < 0 || count < n->max) {
        int w = body_width(e, c, s);

        if (w == 0 || --e->budget < 0)
            break;
        // The mark before the count, so a failed push leaves no unwritten slot.
        if (!push_mark(e, s + w))
            break;
        s += w;
        count++;
    }

    bool ok = false;
    for (int i = count; i >= n->min; i--) {
        Trail t = trail_mark(e);

        trail_close(e, ti, e->marks[base + i]);
        if (mcont(e, k, e->marks[base + i])) {
            ok = true;
            break;
        }
        trail_reset(e, t);
    }
    e->nmarks = base;
    return ok;
}

bool rep_is_simple(const Exec *e, const Node *n)
{
    if (n->child < 0)
        return false;
    const Node *c = &nodes(e->re)[n->child];
    if (c->next >= 0)
        return false;
    return c->op == OP_LIT || c->op == OP_ANY || c->op == OP_SET;
}

// Bytes the capture takes again here, or -1 for no match. Under REG_ICASE the
// two runs need not be the same length, so the subject's is what is returned.
int backref_width(Exec *e, const Node *n, const char *s)
{
    if (n->group >= e->ncap)
        return -1;

    regoff_t so = e->cap[n->group].rm_so, eo = e->cap[n->group].rm_eo;
    if (so < 0 || eo < 0)
        return -1; // the group did not participate, \(a\1\) included

    const char *p = e->base + so, *q = e->base + eo;
    e->budget -= q - p;
    if (e->budget < 0)
        return -1;

    if (!e->icase) {
        usize len = usize(q - p);
        if (usize(e->end - s) < len || !same_bytes(s, p, len))
            return -1;
        return int(len);
    }

    const char *t = s;
    while (p != q) {
        if (t == e->end)
            return -1;
        unsigned int a, bch;
        p += decode(p, q, &a);
        t += decode(t, e->end, &bch);
        if (fold(a) != fold(bch))
            return -1;
    }
    return int(t - s);
}

bool mone(Exec *e, int ni, const char *s, const Cont *k)
{
    const Node *n = &nodes(e->re)[ni];

    if (--e->budget < 0)
        return false;

    switch (n->op) {
    case OP_LIT: {
        int w = lit_width(e, n, s);
        return w ? mcont(e, k, s + w) : false;
    }

    case OP_ANY: {
        unsigned int c;
        if (s == e->end)
            return false;
        int len = decode(s, e->end, &c);
        if (e->newline && c == '\n')
            return false;
        return mcont(e, k, s + len);
    }

    case OP_SET: {
        unsigned int c;
        if (s == e->end)
            return false;
        int len = decode(s, e->end, &c);
        if (!in_set(e, &sets(e->re)[n->set], c))
            return false;
        return mcont(e, k, s + len);
    }

    case OP_BOL:
        if (s == e->start)
            return (e->eflags & REG_NOTBOL) ? false : mcont(e, k, s);
        if (e->newline && s[-1] == '\n')
            return mcont(e, k, s);
        return false;

    case OP_EOL:
        if (s == e->end)
            return (e->eflags & REG_NOTEOL) ? false : mcont(e, k, s);
        if (e->newline && *s == '\n')
            return mcont(e, k, s);
        return false;

    case OP_GROUP: {
        regoff_t was = n->group < e->ncap ? e->cap[n->group].rm_so : -1;
        if (n->group < e->ncap)
            e->cap[n->group].rm_so = s - e->base;
        Trail t = trail_mark(e);
        Cont kk = { 0, n->close, 0, nullptr, -1, -1, k };
        trail_open(e, ni, s);
        if (mseq(e, n->child, s, &kk))
            return true;
        trail_reset(e, t);
        if (n->group < e->ncap)
            e->cap[n->group].rm_so = was;
        return false;
    }

    case OP_CLOSE: {
        regoff_t was = n->group < e->ncap ? e->cap[n->group].rm_eo : -1;
        if (n->group < e->ncap)
            e->cap[n->group].rm_eo = s - e->base;
        Trail t = trail_mark(e);
        trail_close_group(e, n->close, s);
        if (mcont(e, k, s))
            return true;
        trail_reset(e, t);
        if (n->group < e->ncap)
            e->cap[n->group].rm_eo = was;
        return false;
    }

    case OP_BACKREF: {
        int w = backref_width(e, n, s);
        return w < 0 ? false : mcont(e, k, s + w);
    }

    case OP_ALT:
        // A branch is a `next` chain of its own; running out of nodes drops it
        // into k, which is the continuation past the whole alternation.
        for (int b = n->child; b >= 0; b = nodes(e->re)[b].alt)
            if (mseq(e, b, s, k))
                return true;
        return false;

    case OP_REP: {
        // The extent is traced whatever the body is: it is what leaves `.*` in
        // `.*(.*)` the longer of the two.
        Trail t = trail_mark(e);
        int ti  = n->child < 0 || n->len ? -1 : trail_open(e, ni, s);
        bool r  = rep_is_simple(e, n) ? mrep_simple(e, ni, s, ti, k) : mrep(e, ni, s, 0, ti, k);

        if (!r)
            trail_reset(e, t);
        return r;
    }
    }
    return false;
}

bool mseq(Exec *e, int ni, const char *s, const Cont *k)
{
    if (ni < 0)
        return mcont(e, k, s);
    if (MAX_DEPTH <= e->depth)
        return false;
    Cont kk = { 0, nodes(e->re)[ni].next, 0, nullptr, -1, -1, k };
    e->depth++;
    bool r = mone(e, ni, s, &kk);
    e->depth--;
    return r;
}

// --------------------------------------------------------------- prefilter

// Both return whether the thing can match empty -- in which case whatever
// follows it also contributes a first byte. *done turns the filter off, for a
// node that can start with anything.
bool first_bytes(Build *b, int ni, int *done);

bool first_of_seq(Build *b, int ni, int *done)
{
    for (; ni >= 0; ni = nodes(b->re)[ni].next) {
        if (!first_bytes(b, ni, done))
            return false;
        if (*done)
            return true;
    }
    return true; // ran out: the sequence matches empty
}

void mark(regex_t *re, unsigned int c)
{
    re->first[(c & 0xFF) >> 3] |= (unsigned char)(1 << (c & 7));
}

// The lead byte of a rune, which case folding can move.
void mark_rune(regex_t *re, unsigned int r)
{
    char out[4];

    utf8_encode(char32_t(r), out);
    mark(re, (unsigned char)out[0]);
}

bool first_bytes(Build *b, int ni, int *done)
{
    const Node *n = &nodes(b->re)[ni];

    switch (n->op) {
    case OP_LIT:
        mark(b->re, n->bytes[0]);
        if (b->icase) {
            mark_rune(b->re, (unsigned int)rune_lower(char32_t(n->rune)));
            mark_rune(b->re, (unsigned int)rune_upper(char32_t(n->rune)));
        }
        return false;
    case OP_ANY:
    case OP_SET:
    case OP_BACKREF:
        *done = 1;
        return false;
    case OP_BOL:
    case OP_EOL:
    case OP_CLOSE:
        return true;
    case OP_GROUP:
        return first_of_seq(b, n->child, done);
    case OP_ALT: {
        bool empty = false;
        for (int br = n->child; br >= 0; br = nodes(b->re)[br].alt)
            if (first_of_seq(b, br, done))
                empty = true;
        return empty;
    }
    case OP_REP:
        return first_of_seq(b, n->child, done) || n->min == 0;
    }
    *done = 1;
    return false;
}

} // namespace

// ------------------------------------------------------------------ public

int regcomp(regex_t *preg, const char *pattern, int cflags)
{
    Build b;

    __builtin_memset(preg, 0, sizeof(*preg));
    preg->cflags = cflags;
    preg->start  = -1;

    b.re     = preg;
    b.p      = pattern;
    b.pend   = pattern + str_len(pattern);
    b.ngroup = 0;
    b.err    = 0;
    b.bre    = (cflags & REG_EXTENDED) == 0;
    b.icase  = (cflags & REG_ICASE) != 0;

    preg->start = parse_alt(&b);
    if (!b.err && *b.p != '\0')
        b.err = REG_EPAREN; // a stray ) or \)
    if (b.err) {
        regfree(preg);
        return b.err;
    }
    preg->re_nsub = (cflags & REG_NOSUB) ? 0 : (size_t)b.ngroup;

    mark_trace(preg, preg->start, false);

    // A pattern that can match empty matches everywhere; no filter can help.
    int done = 0;
    if (first_of_seq(&b, preg->start, &done))
        done = 1;
    preg->have_first = !done;

    // ^ at the head of every branch pins the match to a line start.
    if (preg->start >= 0) {
        const Node *n  = &nodes(preg)[preg->start];
        preg->anchored = n->op == OP_BOL;
    }
    return 0;
}

void regfree(regex_t *preg)
{
    heap_free(preg->prog);
    heap_free(preg->sets);
    heap_free(preg->ranges);
    __builtin_memset(preg, 0, sizeof(*preg));
    preg->start = -1;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
    static const char *const MSG[] = {
        "No error",           "No match",
        "Invalid pattern",    "Unknown character class",
        "Trailing backslash", "No such subexpression",
        "Unmatched [",        "Unmatched (",
        "Unmatched {",        "Invalid repetition count",
        "Invalid range end",  "Out of memory",
        "Nothing to repeat",  "Invalid collating element",
    };
    const char *m = 0 <= errcode && errcode < (int)(sizeof(MSG) / sizeof(MSG[0])) ? MSG[errcode]
                                                                                  : "Unknown error";
    size_t n      = str_len(m) + 1;

    (void)preg;
    if (errbuf_size > 0) {
        size_t k = n < errbuf_size ? n - 1 : errbuf_size - 1;
        __builtin_memcpy(errbuf, m, k);
        errbuf[k] = '\0';
    }
    return n;
}

int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
    regmatch_t cap[NCAP], bestcap[NCAP];
    Exec e;

    e.re    = preg;
    e.base  = string;
    e.start = string;
    if ((eflags & REG_STARTEND) && nmatch > 0) {
        e.start = string + pmatch[0].rm_so;
        e.end   = string + pmatch[0].rm_eo;
    } else {
        e.end = string + str_len(string);
    }
    if (preg->cflags & REG_NOSUB)
        nmatch = 0;

    e.eflags  = eflags;
    e.newline = (preg->cflags & REG_NEWLINE) != 0;
    e.icase   = (preg->cflags & REG_ICASE) != 0;
    e.depth   = 0;
    e.marks   = nullptr;
    e.nmarks  = 0;
    e.cmarks  = 0;
    e.cap     = cap;
    e.bestcap = bestcap;
    e.ncap    = NCAP;
    // With no group reported there is nothing to choose between parses, which
    // is what keeps grep paying nothing for this.
    e.trail      = nullptr;
    e.ctrail     = 0;
    e.besttrail  = nullptr;
    e.cbesttrail = 0;
    e.trace      = nmatch > 1 && preg->re_nsub > 0;
    // One budget for the call, not per start position: a pathological pattern
    // reports no match rather than hanging, and there is no co_await in here to
    // interrupt it with.
    e.budget = 20000000;

    int rc = REG_NOMATCH;

    for (const char *s = e.start;; s++) {
        // Skip start positions the pattern cannot begin at.
        if (preg->anchored) {
            bool bol = s == e.start ? !(eflags & REG_NOTBOL) : e.newline && s[-1] == '\n';
            if (!bol) {
                const char *nl = nullptr;
                if (e.newline)
                    for (const char *t = s; t != e.end; t++)
                        if (*t == '\n') {
                            nl = t;
                            break;
                        }
                if (!nl)
                    break;
                s = nl; // the s++ below lands on the start of the next line
                continue;
            }
        } else if (preg->have_first) {
            while (s != e.end && !((preg->first[((unsigned char)*s) >> 3] >> (*s & 7)) & 1))
                s++;
        }

        for (int i = 0; i < NCAP; i++) {
            cap[i].rm_so = cap[i].rm_eo = -1;
            bestcap[i].rm_so = bestcap[i].rm_eo = -1;
        }
        e.best       = nullptr;
        e.ntrail     = 0;
        e.nbesttrail = 0;
        e.curparent  = -1;

        (void)mseq(&e, preg->start, s, nullptr);

        if (e.best) {
            if (nmatch > 0) {
                pmatch[0].rm_so = s - string;
                pmatch[0].rm_eo = e.best - string;
                for (size_t i = 1; i < nmatch; i++)
                    pmatch[i] = i < NCAP ? bestcap[i] : regmatch_t{ -1, -1 };
            }
            rc = 0;
            break;
        }
        if (s == e.end)
            break;
    }

    heap_free(e.marks);
    heap_free(e.trail);
    heap_free(e.besttrail);
    return rc;
}
