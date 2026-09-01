#include "complete.h"

#include "kernel/fmt.h"
#include "kernel/traits.h"
#include "tokenize.h"

namespace {

constexpr usize GAP = 2; // between columns, as ls's is

bool is_name_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_name(char c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

// Reserved words after which a command word still follows. `for`, `case` and
// `in` are not among them: a name and a word list come after those.
constexpr Str CMD_AFTER[] = { "!", "do", "elif", "else", "if", "then", "until", "while", "{" };

bool cmd_after(Str w)
{
    for (Str r : CMD_AFTER)
        if (r == w)
            return true;
    return false;
}

// A token after which the next word begins a command.
bool starts_command(Tok t)
{
    switch (t) {
    case Tok::Newline:
    case Tok::Semi:
    case Tok::DSemi:
    case Tok::Amp:
    case Tok::AndIf:
    case Tok::OrIf:
    case Tok::Pipe:
    case Tok::LParen:
        return true;
    default:
        return false;
    }
}

// The `=` of a `name=value` prefix, or npos. A name holds no quote, so the
// byte that ends one is unquoted by construction.
usize assign_at(Str w)
{
    if (w.empty() || !is_name_start(w[0]))
        return Str::npos;
    usize i = 0;
    while (i < w.size() && is_name(w[i]))
        i++;
    return i < w.size() && w[i] == '=' ? i : Str::npos;
}

bool needs_escape(char c)
{
    if (u8(c) < 0x20)
        return true;
    return Str(" \\'\"$`*?[](){};&|<>#!~").find(c) != Str::npos;
}

// A byte that is not a UTF-8 continuation starts a cell.
bool leads(char c)
{
    return (u8(c) & 0xc0) != 0x80;
}

usize disp_width(Str s)
{
    usize n = 0;
    for (usize i = 0; i < s.size(); i++)
        n += leads(s[i]);
    return n;
}

bool less(Str a, Str b)
{
    usize n = min(a.size(), b.size());
    for (usize i = 0; i < n; i++)
        if (u8(a[i]) != u8(b[i]))
            return u8(a[i]) < u8(b[i]);
    return a.size() < b.size();
}

// The kind of a raw word, given whether it sits in command position. Fills
// `at` and `len` afresh, since a Var and an assignment both narrow the site.
CompSite classify(Str raw, usize at, bool cmd)
{
    CompSite s;
    s.at  = at;
    s.len = raw.size();

    // The tail that could be a name, and the `$` or `${` in front of it.
    usize k = raw.size();
    while (k && is_name(raw[k - 1]))
        k--;
    usize d = Str::npos;
    if (k >= 1 && raw[k - 1] == '$')
        d = k - 1;
    else if (k >= 2 && raw[k - 1] == '{' && raw[k - 2] == '$') {
        d        = k - 2;
        s.braced = true;
    }

    // Quote state at the cursor, and whether that `$` is one — a backslash or
    // a single quote in front of it makes it a byte.
    bool live = false;
    for (usize i = 0; i < raw.size(); i++) {
        char c = raw[i];
        if (s.quote == '\'') {
            if (c == '\'')
                s.quote = 0;
            continue;
        }
        if (c == '\\' && i + 1 < raw.size() &&
            (s.quote != '"' || raw[i + 1] == '"' || raw[i + 1] == '\\' || raw[i + 1] == '$' ||
             raw[i + 1] == '`')) {
            i++;
            continue;
        }
        if (s.quote == '"') {
            if (c == '"')
                s.quote = 0;
        } else if (c == '\'' || c == '"') {
            s.quote = c;
        }
        if (i == d)
            live = true;
    }

    if (live) {
        s.kind = CompKind::Var;
        s.at += d + (s.braced ? 2 : 1);
        s.len = raw.size() - d - (s.braced ? 2 : 1);
        return s;
    }
    s.braced = false;

    // Nothing here expands, so a word carrying one is left alone rather than
    // completed against the wrong text.
    if (raw.find('$') != Str::npos || raw.find('`') != Str::npos)
        return s;

    if (cmd) {
        if (usize eq = assign_at(raw); eq != Str::npos) {
            s.kind = CompKind::File;
            s.at += eq + 1;
            s.len = raw.size() - eq - 1;
            return s;
        }
        s.kind = raw.find('/') == Str::npos ? CompKind::Command : CompKind::File;
        return s;
    }
    s.kind = CompKind::File;
    return s;
}

} // namespace

CompSite comp_site(Str upto)
{
    Lexer lx(upto);
    usize at = upto.size();
    bool cmd = true;

    for (;;) {
        Str w;
        Result<Tok> t = lx.next(w);

        // An unclosed quote or `${` is what half a line being typed looks
        // like, and begin() is still the start of that word.
        if (t.is_err()) {
            at = lx.begin();
            break;
        }
        if (t.value() == Tok::End)
            break;
        if (t.value() == Tok::Word && lx.pos() == upto.size()) {
            at = lx.begin();
            break;
        }
        if (t.value() == Tok::Word) {
            if (cmd && assign_at(w) == Str::npos && !cmd_after(w))
                cmd = false;
        } else
            cmd = starts_command(t.value());
    }

    return classify(upto.substr(at), at, cmd);
}

bool comp_unquote(Str raw, String &out)
{
    out.clear();
    char q = 0;

    for (usize i = 0; i < raw.size(); i++) {
        char c = raw[i];
        if (q == '\'') {
            if (c == '\'')
                q = 0;
            else if (!out.push(c))
                return false;
            continue;
        }
        if (c == '\\') {
            // A trailing one is a half-typed escape, and goes.
            if (i + 1 >= raw.size())
                continue;
            char n = raw[i + 1];
            if (q != '"' || n == '"' || n == '\\' || n == '$' || n == '`') {
                if (!out.push(n))
                    return false;
                i++;
                continue;
            }
        }
        if (q == '"') {
            if (c == '"') {
                q = 0;
                continue;
            }
        } else if (c == '\'' || c == '"') {
            q = c;
            continue;
        }
        if (!out.push(c))
            return false;
    }
    return true;
}

bool comp_quote(Str text, char quote, String &out)
{
    for (usize i = 0; i < text.size(); i++) {
        char c = text[i];
        if (quote == '\'') {
            // Nothing escapes inside one, so the quote is left and rejoined.
            if (c == '\'') {
                if (!out.append("'\\''"))
                    return false;
                continue;
            }
        } else if (quote == '"') {
            if ((c == '"' || c == '\\' || c == '$' || c == '`') && !out.push('\\'))
                return false;
        } else if (needs_escape(c) && !out.push('\\')) {
            return false;
        }
        if (!out.push(c))
            return false;
    }
    return true;
}

void comp_split(Str word, Str &dir, Str &leaf)
{
    usize i = word.size();
    while (i && word[i - 1] != '/')
        i--;
    dir  = word.substr(0, i);
    leaf = word.substr(i);
}

Str comp_common(const Vec<String> &names)
{
    if (names.empty())
        return Str();

    Str first = names[0].str();
    usize n   = first.size();
    for (usize i = 1; i < names.size(); i++) {
        Str s   = names[i].str();
        usize k = 0;
        while (k < n && k < s.size() && first[k] == s[k])
            k++;
        n = k;
    }

    // Back to a codepoint boundary: the shared bytes may stop inside one.
    while (n && n < first.size() && !leads(first[n]))
        n--;
    return first.substr(0, n);
}

void comp_sort(Vec<String> &v)
{
    for (usize i = 1; i < v.size(); i++)
        for (usize k = i; k && less(v[k].str(), v[k - 1].str()); k--)
            swap(v[k], v[k - 1]);

    for (usize i = 1; i < v.size();) {
        if (v[i].str() == v[i - 1].str())
            v.erase(i);
        else
            i++;
    }
}

bool comp_columns(const Vec<String> &names, u32 width, String &out)
{
    usize n   = names.size();
    bool more = n > COMP_LIST_MAX;
    if (more)
        n = COMP_LIST_MAX;
    if (!n)
        return true;
    if (!width)
        width = 80;

    usize w = 0;
    for (usize i = 0; i < n; i++)
        w = max(w, disp_width(names[i].str()));

    // One shared column width, and the shrink that leaves no column empty —
    // ls's arithmetic, over the same kind of listing.
    usize colw = w + GAP;
    usize cols = (width + GAP) / colw;
    if (cols < 1)
        cols = 1;
    if (cols > n)
        cols = n;
    usize rows = (n + cols - 1) / cols;
    cols       = (n + rows - 1) / rows;

    // Column-major, so a sorted listing is read down a column.
    for (usize y = 0; y < rows; y++) {
        for (usize x = 0; x < cols; x++) {
            usize i = x * rows + y;
            if (i >= n)
                continue;
            if (!out.append(names[i].str()))
                return false;
            // The last cell of a row is not padded.
            if (i + rows < n)
                for (usize p = disp_width(names[i].str()); p < colw; p++)
                    if (!out.push(' '))
                        return false;
        }
        if (!out.push('\n'))
            return false;
    }

    if (more) {
        Buf<32> b;
        b.put("... and ").put(u32(names.size() - n)).put(" more\n");
        if (!out.append(b.str()))
            return false;
    }
    return true;
}
