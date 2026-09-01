#include "completerun.h"

#include "builtin.h"
#include "complete.h"
#include "job.h"
#include "kernel/alloc.h"
#include "kernel/traits.h"
#include "proc/io.h"
#include "var.h"

namespace {

// The walk's state, off the coroutine frame: a frame past 512 bytes costs a
// whole 64 KiB span.
struct CompWork {
    Vec<String> names;
    Vec<DirEntry> ents;
    String prefix; // the literal the site stands for
    Str dir, leaf; // views into prefix, for a path
    u32 kind = SYS_KIND_FILE;
};

// A prefix as a path to list: "" is the cwd, and the trailing '/' goes.
Str dir_of(Str p)
{
    if (p.empty())
        return ".";
    if (p.size() == 1)
        return p;
    return p.substr(0, p.size() - 1);
}

bool keep(Str name, Str want)
{
    // A leading dot has to be asked for, as a glob's does.
    if (name.size() && name[0] == '.' && (want.empty() || want[0] != '.'))
        return false;
    return name.starts_with(want);
}

bool take(Vec<String> &into, Str name)
{
    String s;
    return s.assign(name) && into.push(move(s));
}

// Every name in `path` that carries the prefix. `dirs` false drops them, which
// is what a PATH element wants.
Task<Result<void>> from_dir(CompWork &w, Str path, Str want, bool dirs)
{
    Result<Vec<DirEntry>> r = Err(Error::NotFound);
    if (Task<Result<Vec<DirEntry>>> t = list_dir(path))
        r = co_await t;
    if (r.is_err()) {
        if (r.error() == Error::Cancelled)
            co_return Err(Error::Cancelled);
        co_return {}; // not a directory, or gone: it matches nothing
    }

    w.ents = move(r.value());
    for (const DirEntry &e : w.ents) {
        if (!dirs && e.kind == SYS_KIND_DIR)
            continue;
        if (!keep(e.name.str(), want))
            continue;
        if (!take(w.names, e.name.str()))
            co_return Err(Error::NoMemory);
        w.kind = e.kind;
    }
    co_return {};
}

// Functions, then builtins, then each PATH element — the order a command word
// resolves in. Both tables are copied before the first await, so nothing can
// move one under a Str.
Task<Result<void>> commands(CompWork &w, Str want)
{
    for (usize i = 0; i < func_count(); i++)
        if (keep(func_at(i), want) && !take(w.names, func_at(i)))
            co_return Err(Error::NoMemory);
    for (usize i = 0; i < builtin_count(); i++)
        if (keep(builtin_at(i)->name, want) && !take(w.names, builtin_at(i)->name))
            co_return Err(Error::NoMemory);

    Str rest = sh_path(), dir;
    while (env_path_next(rest, dir)) {
        Task<Result<void>> t = from_dir(w, dir, want, false);
        Result<void> r       = t ? co_await t : Err(Error::NoMemory);
        if (r.is_err())
            co_return Err(r.error());
    }
    co_return {};
}

bool variables(CompWork &w, Str want)
{
    for (usize i = 0; i < var_count(); i++) {
        const VarEntry *e = var_at(i);
        if (e->name.str().starts_with(want) && !take(w.names, e->name.str()))
            return false;
    }
    return true;
}

// Whether the one match is a directory. A listing never resolves a link, so
// this is where the stat is paid — once, for the name that won.
Task<bool> is_dir(CompWork &w)
{
    if (w.kind == SYS_KIND_DIR)
        co_return true;
    if (w.kind != SYS_KIND_LINK)
        co_return false;

    String full;
    if (!full.assign(w.dir) || !full.append(w.names[0].str()))
        co_return false;
    if (Task<Result<FileInfo>> t = stat_of(full.str()))
        if (Result<FileInfo> r = co_await t; r.is_ok())
            co_return r.value().kind == SYS_KIND_DIR;
    co_return false;
}

} // namespace

Task<Result<CompReply>> complete_line(Str upto, u32 width, bool show)
{
    CompReply reply;
    CompSite site = comp_site(upto);
    if (site.kind == CompKind::None)
        co_return move(reply);

    CompWork *w = heap_new<CompWork>();
    if (!w)
        co_return Err(Error::NoMemory);
    struct Free {
        ~Free() { heap_delete(p); }

        CompWork *p;
    } guard{ w };

    if (!comp_unquote(upto.substr(site.at, site.len), w->prefix))
        co_return Err(Error::NoMemory);

    // A path is completed against its last component; the rest is already typed.
    Str want = w->prefix.str();
    if (site.kind == CompKind::File) {
        comp_split(w->prefix.str(), w->dir, w->leaf);
        want = w->leaf;
    }

    Result<void> got = {};
    switch (site.kind) {
    case CompKind::File:
        if (Task<Result<void>> t = from_dir(*w, dir_of(w->dir), want, true))
            got = co_await t;
        else
            got = Err(Error::NoMemory);
        break;
    case CompKind::Command:
        if (Task<Result<void>> t = commands(*w, want))
            got = co_await t;
        else
            got = Err(Error::NoMemory);
        break;
    case CompKind::Var:
        got = variables(*w, want) ? Result<void>() : Err(Error::NoMemory);
        break;
    case CompKind::None:
        break;
    }
    if (got.is_err())
        co_return Err(got.error());

    comp_sort(w->names);
    reply.count = w->names.size();
    if (!reply.count)
        co_return move(reply);

    Str tail = comp_common(w->names).substr(want.size());
    if (!comp_quote(tail, site.quote, reply.insert))
        co_return Err(Error::NoMemory);

    if (reply.count == 1) {
        // A variable is usually the head of a longer word, so it takes neither
        // a space nor the closing quote — only the brace it was opened with.
        if (site.kind == CompKind::Var) {
            if (site.braced && !reply.insert.push('}'))
                co_return Err(Error::NoMemory);
        } else {
            bool dir = false;
            if (site.kind == CompKind::File)
                if (Task<bool> t = is_dir(*w))
                    dir = co_await t;
            if (site.quote && !reply.insert.push(site.quote))
                co_return Err(Error::NoMemory);
            if (!reply.insert.push(dir ? '/' : ' '))
                co_return Err(Error::NoMemory);
        }
    } else if (show && reply.insert.empty()) {
        if (!comp_columns(w->names, width, reply.list))
            co_return Err(Error::NoMemory);
    }

    co_return move(reply);
}
