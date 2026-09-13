// POSIX regular expressions. `braam::regex`, asked for by name:
//
//     braam_add_program(NAME grep SOURCES grep.cpp LIBS braam::regex)
//
// A PORT target reaches the same three functions as <regex.h> and needs no
// LIBS line, the kit carrying them.
//
// ERE with REG_EXTENDED, POSIX BRE without it, back-references in both. The
// match is leftmost-longest, as POSIX says and unlike the backtracking engines
// that stop at the first one, and the groups inside it are POSIX's too: the
// rule is applied outward, so a subexpression takes the longest the ones around
// it leave for it. Offsets are bytes; `.` and a bracket take a whole UTF-8
// sequence, so a match never ends mid-character.
//
// Not here: GNU's \| \+ \? in a BRE, collating elements ([.x.], [=x=]), and a
// locale — the classes are the codepoint's, kernel/text.h's rune_is_*, whose
// coverage is case plus the letter blocks that have none.
//
// A {m,n} bound is at most 32767; past that is REG_BADBR.
#pragma once

#include <stddef.h>

enum {
    REG_EXTENDED = 1, // ERE rather than BRE
    REG_ICASE    = 2, // fold case, to whatever rune_lower maps
    REG_NEWLINE  = 4, // ^ and $ match at a newline, which . and [^x] refuse
    REG_NOSUB    = 8, // success or failure alone, no pmatch
};

enum {
    REG_NOTBOL   = 1, // the subject does not begin a line
    REG_NOTEOL   = 2, // the subject does not end one
    REG_STARTEND = 4, // pmatch[0] delimits it; no NUL needed, and NUL is data
};

enum {
    REG_NOMATCH = 1,
    REG_BADPAT,
    REG_ECTYPE,  // [[:nosuch:]]
    REG_EESCAPE, // a trailing backslash
    REG_ESUBREG, // \1 with no such group
    REG_EBRACK,  // unmatched [
    REG_EPAREN,  // unmatched ( or )
    REG_EBRACE,  // unmatched { or }
    REG_BADBR,   // {2,1}
    REG_ERANGE,  // [z-a]
    REG_ESPACE,  // out of memory
    REG_BADRPT,  // *, + or ? with nothing to repeat
};

typedef ptrdiff_t regoff_t;

typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

// POD: a caller may keep one at namespace scope, where a destructor cannot
// run. regfree() is what releases it.
typedef struct {
    size_t re_nsub;

    void *prog;   // node arena
    void *sets;   // bracket arena
    void *ranges; // non-ASCII ranges the brackets name
    int nnodes, nsets, nranges;
    int cnodes, csets, cranges; // capacities, doubling
    int start;                  // first node, -1 when the pattern is empty
    int cflags;

    // What regexec skips start positions with.
    unsigned char first[32]; // bytes a match can begin with
    int have_first;
    int anchored; // ^ leads: only a line start can match
} regex_t;

int regcomp(regex_t *preg, const char *pattern, int cflags);
int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[],
            int eflags);
size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size);
void regfree(regex_t *preg);
