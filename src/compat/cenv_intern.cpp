// The memo behind getenv. Five ported packages each answered out of one
// `static char val[512]`, so two live results aliased and a long value was
// truncated; le kept the pointer in HOME and the next getenv rewrote it.
//
// One block per distinct name, never freed and never moved: a caller may hold
// a result across any number of later calls. Growing one realloc'd block would
// not do — realloc may move, and every pointer already handed out would dangle.
#include "cenv.h"

#include "kernel/alloc.h"

namespace {

struct Entry {
    Entry *next;
    usize name_len;
    // name bytes, then NUL, then value bytes, then NUL.
    char text[1];
};

// Trivially destructible, as a namespace-scope global must be.
Entry *head;

bool name_is(const Entry *e, Str name)
{
    if (e->name_len != name.size())
        return false;
    for (usize i = 0; i < name.size(); i++)
        if (e->text[i] != name[i])
            return false;
    return true;
}

} // namespace

char *env_intern(Str name, Str value)
{
    if (value.empty())
        return nullptr;

    for (Entry *e = head; e; e = e->next)
        if (name_is(e, name))
            return e->text + e->name_len + 1;

    usize bytes = sizeof(Entry) + name.size() + value.size() + 1;
    Entry *e    = static_cast<Entry *>(heap_alloc(bytes));
    if (!e)
        return nullptr;

    e->name_len = name.size();
    char *p     = e->text;
    for (usize i = 0; i < name.size(); i++)
        *p++ = name[i];
    *p++      = '\0';
    char *val = p;
    for (usize i = 0; i < value.size(); i++)
        *p++ = value[i];
    *p = '\0';

    e->next = head;
    head    = e;
    return val;
}
