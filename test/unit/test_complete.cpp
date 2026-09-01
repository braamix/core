#include "cmd/sh/complete.h"
#include "harness.h"
#include "kernel/alloc.h"

namespace {

// Behind a pointer built on first use: a namespace-scope global must be
// trivially destructible or it pulls in __cxa_atexit.
struct CompBag {
    String scratch, held;
    Vec<String> names;
};

CompBag *g_bag;

CompBag &bag()
{
    if (!g_bag) {
        g_bag = heap_new<CompBag>();
        if (!g_bag)
            panic("test_complete: out of memory");
    }
    return *g_bag;
}

// A site as text: the kind, then the raw word it covers, then the quote and a
// `{` when one is open — so a whole classification reads on one line.
Str site(Str upto)
{
    CompSite s      = comp_site(upto);
    String &scratch = bag().scratch;
    scratch.clear();
    switch (s.kind) {
    case CompKind::None:
        scratch.append("none");
        break;
    case CompKind::Command:
        scratch.append("cmd");
        break;
    case CompKind::File:
        scratch.append("file");
        break;
    case CompKind::Var:
        scratch.append("var");
        break;
    }
    scratch.push(' ');
    scratch.append(upto.substr(s.at, s.len));
    if (s.quote)
        scratch.push(s.quote);
    if (s.braced)
        scratch.push('{');
    return scratch.str();
}

Str unquoted(Str raw)
{
    String &held = bag().held;
    return comp_unquote(raw, held) ? held.str() : "!"_s;
}

Str quoted(Str text, char q)
{
    String &held = bag().held;
    held.clear();
    return comp_quote(text, q, held) ? held.str() : "!"_s;
}

// A space-separated list into the candidate vector.
void set(Str list)
{
    Vec<String> &names = bag().names;
    names.clear();
    Str rest = list, one;
    while (!rest.empty()) {
        one = rest.split(' ', rest);
        String s;
        s.assign(one);
        names.push(move(s));
    }
}

Str joined()
{
    String &scratch = bag().scratch;
    scratch.clear();
    for (usize i = 0; i < bag().names.size(); i++) {
        if (i)
            scratch.push(' ');
        scratch.append(bag().names[i].str());
    }
    return scratch.str();
}

Str columns(Str list, u32 width)
{
    set(list);
    String &held = bag().held;
    held.clear();
    return comp_columns(bag().names, width, held) ? held.str() : "!"_s;
}

} // namespace

void test_complete()
{
    test_begin("complete");

    // The start of a line, and every operator that ends a command, leave a
    // command word next.
    CHECK(site("") == "cmd ");
    CHECK(site("l") == "cmd l");
    CHECK(site("echo a; l") == "cmd l");
    CHECK(site("echo a | l") == "cmd l");
    CHECK(site("echo a && l") == "cmd l");
    CHECK(site("echo a & l") == "cmd l");
    CHECK(site("(l") == "cmd l");

    // A word after one is a file, and so is anything after a redirection.
    CHECK(site("ls") == "cmd ls");
    CHECK(site("ls ") == "file ");
    CHECK(site("ls fo") == "file fo");
    CHECK(site("ls fo bar") == "file bar");
    CHECK(site("cat <fi") == "file fi");
    CHECK(site("cat >fi") == "file fi");

    // A command word holding a slash is a path, as exec_resolve's is.
    CHECK(site("/bin/l") == "file /bin/l");
    CHECK(site("./sc") == "file ./sc");

    // Reserved words lex as words, and a command still follows them.
    CHECK(site("if l") == "cmd l");
    CHECK(site("while l") == "cmd l");
    CHECK(site("do l") == "cmd l");
    CHECK(site("! l") == "cmd l");
    CHECK(site("{ l") == "cmd l");
    // `for` and `in` are not among them: a name and a word list come after.
    CHECK(site("for f in fi") == "file fi");

    // An assignment prefix does not spend command position, and completing one
    // completes its value.
    CHECK(site("FOO=x l") == "cmd l");
    CHECK(site("FOO=/b") == "file /b");
    CHECK(site("PATH=") == "file ");

    // A variable, braced or not, and only where a `$` can be one.
    CHECK(site("echo $HO") == "var HO");
    CHECK(site("echo ${HO") == "var HO{");
    CHECK(site("echo ${") == "var {");
    CHECK(site("echo $") == "var ");
    CHECK(site("echo x$HO") == "var HO");
    CHECK(site("echo \"$HO") == "var HO\"");
    // In single quotes a `$` is a byte, and a byte is not completed against a
    // name — nothing here expands, so the word is left alone.
    CHECK(site("echo '$HO") == "none '$HO'");
    CHECK(site("echo \\$HO") == "none \\$HO");
    // Likewise anything this cannot expand at all.
    CHECK(site("ls $(ls fo") == "none $(ls fo");
    CHECK(site("ls `ls fo") == "none `ls fo");
    CHECK(site("ls ${x}/fo") == "none ${x}/fo");

    // An unclosed quote is the ordinary path: the lexer refuses the line and
    // begin() is still the start of that word.
    CHECK(site("ls 'my fi") == "file 'my fi'");
    CHECK(site("ls \"my fi") == "file \"my fi\"");
    CHECK(site("ls my\\ fi") == "file my\\ fi");
    CHECK(site("ls foo\\") == "file foo\\");
    CHECK(site("ls 'a' 'b") == "file 'b'");

    // Quote removal, which is what the prefix is matched as.
    CHECK(unquoted("fo") == "fo");
    CHECK(unquoted("'my fi") == "my fi");
    CHECK(unquoted("\"my fi") == "my fi");
    CHECK(unquoted("my\\ fi") == "my fi");
    CHECK(unquoted("'a'b\"c\"") == "abc");
    // A trailing backslash is a half-typed escape and goes.
    CHECK(unquoted("foo\\") == "foo");
    // Only four bytes escape inside double quotes.
    CHECK(unquoted("\"a\\$b") == "a$b");
    CHECK(unquoted("\"a\\nb") == "a\\nb");

    // Requoting, in each of the three contexts.
    CHECK(quoted("two words", 0) == "two\\ words");
    CHECK(quoted("a*b", 0) == "a\\*b");
    CHECK(quoted("a$b", 0) == "a\\$b");
    CHECK(quoted("two words", '"') == "two words");
    CHECK(quoted("a$b", '"') == "a\\$b");
    CHECK(quoted("two words", '\'') == "two words");
    CHECK(quoted("it's", '\'') == "it'\\''s");
    // A codepoint is bytes, and none of them is a metacharacter.
    CHECK(quoted("na\xc3\xafve", 0) == "na\xc3\xafve");

    // The split a path completion runs on.
    {
        Str dir, leaf;
        comp_split("fo", dir, leaf);
        CHECK(dir == "" && leaf == "fo");
        comp_split("a/b/fo", dir, leaf);
        CHECK(dir == "a/b/" && leaf == "fo");
        comp_split("/fo", dir, leaf);
        CHECK(dir == "/" && leaf == "fo");
        comp_split("a/", dir, leaf);
        CHECK(dir == "a/" && leaf == "");
    }

    // The longest common prefix.
    set("");
    CHECK(comp_common(bag().names) == "");
    set("abc");
    CHECK(comp_common(bag().names) == "abc");
    set("abc abd");
    CHECK(comp_common(bag().names) == "ab");
    set("abc xyz");
    CHECK(comp_common(bag().names) == "");
    // It must not stop inside a codepoint: these share `na` and a lead byte.
    set("na\xc3\xafve na\xc3\xa9");
    CHECK(comp_common(bag().names) == "na");

    // Sorted by byte, with adjacent duplicates dropped.
    set("test echo cd ls test echo");
    comp_sort(bag().names);
    CHECK(joined() == "cd echo ls test");

    // Columns, down a column and then across, at one shared width.
    CHECK(columns("aaa b cc", 80) == "aaa  b    cc\n");
    CHECK(columns("aaa b cc dddd e", 12) == "aaa   dddd\nb     e\ncc\n");
    // A name is cells, not bytes, so an accent does not skew the padding.
    CHECK(columns("na\xc3\xafve ab", 80) == "na\xc3\xafve  ab\n");
}
