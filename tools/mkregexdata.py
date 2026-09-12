#!/usr/bin/env python3
"""Reference matches for test/unit/test_regex.cpp, as test/unit/regex.data.

Hand-run, like the other publishers here; no build step calls it. The oracle is
the host's own POSIX <regex.h> through ctypes, which is what lifting the engine
out of editors/eh cost us: the differential harness there was native, and an
engine that reaches kernel/alloc.h cannot be compiled for the host any more. So
the cross product it drove is recorded here instead, and replayed in wasm.

POSIX names the cflags and does not number them, and the two families disagree:
glibc has REG_NEWLINE 4 and REG_NOSUB 8, the BSDs and macOS the other way
round. Getting it backwards asks for REG_NOSUB and every match comes back
empty, so the values are chosen by platform here:

  tools/mkregexdata.py > test/unit/regex.data
"""

import ctypes
import ctypes.util
import sys

REG_EXTENDED, REG_ICASE = 1, 2
if sys.platform == "darwin" or "bsd" in sys.platform:
    REG_NOSUB, REG_NEWLINE = 4, 8
else:
    REG_NEWLINE, REG_NOSUB = 4, 8
REG_NOTBOL, REG_NOTEOL = 1, 2

libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)


class Match(ctypes.Structure):
    _fields_ = [("rm_so", ctypes.c_longlong), ("rm_eo", ctypes.c_longlong)]


# Spelled out: without them ctypes narrows nmatch and regexec writes nothing.
libc.regcomp.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
libc.regexec.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t,
                         ctypes.POINTER(Match), ctypes.c_int]
libc.regfree.argtypes = [ctypes.c_char_p]


# regex_t is opaque and its size is the host's business; 512 bytes is room.
def compiled(pattern, cflags):
    re = ctypes.create_string_buffer(512)
    rc = libc.regcomp(re, pattern, cflags)
    return (re, rc)


# How many groups the pattern opens. re_nsub sits at a different offset in
# regex_t on each host, and a group is easier to count than to ask about.
def groups(pattern, bre):
    n, i = 0, 0
    while i < len(pattern):
        c = pattern[i : i + 1]
        if c == b"\\":
            if bre and pattern[i + 1 : i + 2] == b"(":
                n += 1
            i += 2
            continue
        if c == b"(" and not bre:
            n += 1
        i += 1
    return n


def oracle(pattern, subject, cflags, eflags):
    nsub = groups(pattern, not (cflags & REG_EXTENDED))
    re, rc = compiled(pattern, cflags)
    if rc != 0:
        return None
    m = (Match * (nsub + 1))()
    for i in range(nsub + 1):
        m[i].rm_so = m[i].rm_eo = -1
    rc = libc.regexec(re, subject, nsub + 1, m, eflags)
    libc.regfree(re)
    if rc != 0:
        return "-"

    out = ["%d:%d" % (m[0].rm_so, m[0].rm_eo)]
    for i in range(1, nsub + 1):
        out.append("-" if m[i].rm_so < 0 else "%d:%d" % (m[i].rm_so, m[i].rm_eo))
    while len(out) > 1 and out[-1] == "-":
        out.pop()
    return " ".join(out)


def literal(s):
    out = ['"']
    for b in s:
        if b == 0x22 or b == 0x5C:
            out.append("\\" + chr(b))
        elif b == 0x0A:
            out.append("\\n")
        elif b == 0x09:
            out.append("\\t")
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append("\\x%02x" % b)
    out.append('"')
    return "".join(out)


# What upstream's own tests reach, the kludges around them, and every shape the
# retired harness in editors/eh/test/regex_test.cpp drove.
ERE = [
    "^", "$", ".$", "^$", "line", "butt", "WOOT", "A:", "\\)", "\\}", "\t",
    # Leftmost-longest, which a leftmost-first backtracker gets wrong.
    "foo|foobar", "foobar|foo", "a|ab|abc", "(a|ab)(c|bcd)",
    # Repetition.
    "a*", "a+", "a?", "ab*c", "a.*b", "x*y*z*", "a{2}", "a{2,}", "a{1,3}",
    "(ab)*", "(a*)*b", "(a|b)+",
    # Anchors inside and around.
    "^a", "a$", "^a$", "^.*$", "^\n", "\n$", "^ab*$",
    # Brackets.
    "[abc]", "[^abc]", "[a-z]+", "[^a-z]+", "[]a]", "[^]a]", "[a-]", "[-a]",
    "[[:alpha:]]+", "[[:digit:]]+", "[[:space:]]", "[[:alnum:]_]+", "[^[:space:]]+",
    # Groups and captures.
    "(a)(b)(c)", "(a(b(c)))", "(a)|(b)", "((a)*)b", "(foo)?bar", "(a|b)*c",
    # Dot and newline under REG_NEWLINE.
    ".", ".*", "a.c", "[^x]*",
    # Nesting and alternation depth.
    "(a|b)(c|d)(e|f)", "((x|y)z)+", "a(b|c)*d",
]
# Back-references in an ERE are GNU's extension and the BSDs do not have them,
# so the host cannot be asked: the BRE spellings below carry that coverage, and
# test_regex.cpp asserts the ERE ones by hand.

# The same subjects, so a pattern's whole row is comparable.
SUBJECTS = [
    "", "a", "ab", "abc", "abcabc", "aaa", "foobar", "hello world",
    "hello\nworld", "\n", "\n\n", "a\nb\nc", "a\nb\nc\n", "-last line-",
    "this file might be a", "A: alpha", "x)y}z", "\tindented", "123 456",
    "the_name_42", "  spaced  ", "aXbXc", "abcdef", "xyzxyz",
]

# Every rule that makes a BRE not an ERE: \( \) grouping, \{m,n\} intervals,
# + ? | ( ) { } literal, * literal where there is nothing to repeat, ^ and $
# anchors only at the ends, and \1.
# \| is left out on purpose: POSIX has no alternation in a BRE, glibc reads it
# as one anyway, and the answer would be the host's rather than the spec's.
# test_regex.cpp asserts that one by hand.
BRE = [
    "a\\{2\\}", "a\\{2,\\}", "a\\{1,3\\}", "\\(ab\\)*", "\\(a\\)\\(b\\)",
    "\\(a*\\)b", "a+", "a?", "(a)", "{2}", "a{2}", "*a", "^*",
    "^a", "a$", "a^b", "a$b", "^\\(a\\)$", "\\(a\\)\\1", "\\(.\\)\\1",
    "\\(ab\\)\\1", "[abc]\\{2\\}", ".\\{3\\}", "\\(a\\)*b",
]

# Folding, which the header promises over case-mapped codepoints.
ICASE = [
    "abc", "ABC", "[a-z]+", "[A-Z]+", "a+", "A+", "(a)(B)", "hello world",
    "WOOT", "^A", "Z$", "[[:upper:]]+", "[[:lower:]]+", "[^a]", "[^A-Z]+",
]
ICASE_SUBJECTS = [
    "", "a", "A", "abc", "ABC", "AbC", "aA", "Hello World", "HELLO WORLD",
    "woot", "WOOT", "zZ", "The_Name_42",
]


def emit(name, patterns, subjects, cflags):
    print("static const RegexCase %s[] = {" % name)
    for p in patterns:
        pb = p.encode()
        if compiled(pb, cflags)[1] != 0:
            sys.exit("the host refuses /%s/ at cflags %d" % (p, cflags))
        for s in subjects:
            sb = s.encode()
            want = [oracle(pb, sb, cflags, e) for e in (0, REG_NOTBOL, REG_NOTEOL)]
            print("    { %s, %s, { %s } }," %
                  (literal(pb), literal(sb),
                   ", ".join(literal(w.encode()) for w in want)))
    print("};")
    print()


print("// Generated by tools/mkregexdata.py. Do not edit.")
print("// The pattern, the subject, and what the host's own <regex.h> matched")
print("// at eflags 0, REG_NOTBOL and REG_NOTEOL: the whole extent, then each")
print("// group that took part, or \"-\" for no match at all.")
print()
emit("ERE_CASES", ERE, SUBJECTS, REG_EXTENDED | REG_NEWLINE)
emit("BRE_CASES", BRE, SUBJECTS, REG_NEWLINE)
emit("ICASE_CASES", ICASE, ICASE_SUBJECTS, REG_EXTENDED | REG_NEWLINE | REG_ICASE)
