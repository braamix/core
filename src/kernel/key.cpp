#include "key.h"

#include "screen.h"
#include "traits.h"

namespace {

// Constant-initialised, and trivially destructible, so they need neither the
// static init nor the __cxa_atexit that --no-entry leaves unavailable.
Channel<Key> g_keys[TERM_MAX];

static_assert(is_trivially_destructible<Channel<Key>>, "a global must not need atexit");

} // namespace

Channel<Key> &keys(u32 term)
{
    return g_keys[term < TERM_MAX ? term : 0];
}
