#include "alloc.h"

#include "host.h"

extern "C" u8 __heap_base; // supplied by wasm-ld

// Linear memory is carved into 64 KiB spans, and a side table says what each
// one holds. Three tiers:
//  - up to MAX_SMALL, a span serves one size class and a block has no header:
//    the class is `span_class[ptr >> 16]`;
//  - up to ARENA_MAX, an arena span holds blocks of any size behind a 16-byte
//    boundary tag, first fit over an address-ordered free list, neighbours
//    merged on free, and a span that empties goes back to the free runs;
//  - above that, a block is a whole run of spans.

namespace {

constexpr usize SPAN_SHIFT = 16;
constexpr usize SPAN_SIZE  = usize(1) << SPAN_SHIFT;
constexpr usize PAGE_SIZE  = 65536;
constexpr usize MAX_SPANS  = 4096; // 256 MiB of addressable heap

constexpr u16 SIZE_CLASS[]  = { 16, 32, 64, 128, 256, 512 };
constexpr usize NUM_CLASSES = sizeof(SIZE_CLASS) / sizeof(SIZE_CLASS[0]);
constexpr usize MAX_SMALL   = 512;

// The class of each 16-byte step up to MAX_SMALL, indexed by (n + 15) >> 4.
constexpr u8 CLASS_OF_STEP[MAX_SMALL / 16 + 1] = {
    0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

constexpr usize ARENA_MAX = SPAN_SIZE / 2;
constexpr u32 TAG_SIZE    = 16;
constexpr u32 TAG_LIVE    = 0xB10C11FE;
constexpr u32 TAG_FREE    = 0xB10CF4EE;

// The smallest block worth keeping on its own: the smallest arena request.
constexpr u32 ARENA_MIN = u32((MAX_SMALL + 1 + 15) & ~usize(15)) + TAG_SIZE;

constexpr u8 SPAN_UNUSED     = 0xFF;
constexpr u8 SPAN_FREE       = 0xFE; // head of a free run
constexpr u8 SPAN_FREE_CONT  = 0xFD; // interior of a free run
constexpr u8 SPAN_LARGE      = 0xFC; // head of a live multi-span block
constexpr u8 SPAN_LARGE_CONT = 0xFB;
constexpr u8 SPAN_ARENA      = 0xFA;

constexpr u32 NO_SPAN = 0xFFFFFFFF;

// The boundary tag in front of an arena block. `size` counts the tag, and
// `prev_size` is the block below in the same span, 0 for the first.
struct ArenaTag {
    u32 size;
    u32 prev_size;
    u32 state; // TAG_LIVE or TAG_FREE
    u32 pad;
};

// A free arena block, on the free list by address.
struct ArenaFree {
    ArenaTag tag;
    ArenaFree *next;
    ArenaFree *prev;
};

// Header of a free run, written into the run's first bytes.
struct FreeRun {
    u32 next;  // span index of the next free run, address-ordered
    u32 count; // spans in this run
};

struct Heap {
    u8 span_class[MAX_SPANS];
    u32 span_run[MAX_SPANS]; // span count, valid on a SPAN_LARGE head

    void *free_list[NUM_CLASSES];
    u8 *bump[NUM_CLASSES];
    u8 *bump_end[NUM_CLASSES];

    ArenaFree *arena_free; // lowest free arena block
    u32 arena_empty;       // arena spans wholly free and kept

    u32 free_head;  // first free run, or NO_SPAN
    u32 next_span;  // lowest span never yet claimed
    u32 span_limit; // one past the highest usable span
    u32 first_span; // where the heap begins

    bool ready;
    HeapStats stats;
};

Heap h;

u32 span_of(const void *p)
{
    return u32(reinterpret_cast<usize>(p) >> SPAN_SHIFT);
}

u8 *span_addr(u32 i)
{
    return reinterpret_cast<u8 *>(usize(i) << SPAN_SHIFT);
}

// n is at least 1 and at most MAX_SMALL.
usize class_of(usize n)
{
    return CLASS_OF_STEP[(n + 15) >> 4];
}

// A request's arena block, tag included.
u32 arena_size(usize n)
{
    return u32((n + 15) & ~usize(15)) + TAG_SIZE;
}

// Extends linear memory so that `spans` spans starting at h.next_span exist.
bool grow_to(u32 spans)
{
    usize want_end = (usize(h.next_span) + spans) << SPAN_SHIFT;
    usize have_end = usize(__builtin_wasm_memory_size(0)) * PAGE_SIZE;
    if (want_end <= have_end)
        return true;
    usize pages = (want_end - have_end + PAGE_SIZE - 1) / PAGE_SIZE;
    if (__builtin_wasm_memory_grow(0, pages) == usize(-1))
        return false;
    h.stats.grows++; // counted here, not on entry: the early-out above grows nothing
    return true;
}

FreeRun *run_at(u32 s)
{
    return reinterpret_cast<FreeRun *>(span_addr(s));
}

// Address-ordered insertion with coalescing, so adjacent frees rejoin.
void free_run_insert(u32 start, u32 count)
{
    u32 prev = NO_SPAN;
    u32 cur  = h.free_head;
    while (cur != NO_SPAN && cur < start) {
        prev = cur;
        cur  = run_at(cur)->next;
    }

    for (u32 i = 0; i < count; i++)
        h.span_class[start + i] = i == 0 ? SPAN_FREE : SPAN_FREE_CONT;

    FreeRun *self = run_at(start);
    self->next    = cur;
    self->count   = count;
    if (prev == NO_SPAN)
        h.free_head = start;
    else
        run_at(prev)->next = start;

    if (cur != NO_SPAN && start + count == cur) {
        FreeRun *next = run_at(cur);
        self->count += next->count;
        self->next        = next->next;
        h.span_class[cur] = SPAN_FREE_CONT;
    }

    if (prev != NO_SPAN) {
        FreeRun *p = run_at(prev);
        if (prev + p->count == start) {
            p->count += self->count;
            p->next             = self->next;
            h.span_class[start] = SPAN_FREE_CONT;
        }
    }
}

// First fit over the free runs, then fresh spans.
u32 span_run_take(u32 count)
{
    u32 *link = &h.free_head;
    u32 cur   = h.free_head;
    while (cur != NO_SPAN) {
        FreeRun *r = run_at(cur);
        if (r->count >= count) {
            u32 next  = r->next;
            u32 extra = r->count - count;
            if (extra > 0) {
                u32 rest           = cur + count;
                FreeRun *tail      = run_at(rest);
                tail->next         = next;
                tail->count        = extra;
                h.span_class[rest] = SPAN_FREE;
                next               = rest;
            }
            *link = next;
            return cur;
        }
        link = &r->next;
        cur  = r->next;
    }

    if (usize(h.next_span) + count > h.span_limit)
        return NO_SPAN;
    if (!grow_to(count))
        return NO_SPAN;

    u32 start = h.next_span;
    h.next_span += count;
    h.stats.spans += count;
    h.stats.bytes_reserved += usize(count) << SPAN_SHIFT;
    return start;
}

void *alloc_small(usize c)
{
    if (void *p = h.free_list[c]) {
        h.free_list[c] = *reinterpret_cast<void **>(p);
        return p;
    }

    usize size = SIZE_CLASS[c];
    if (h.bump[c] + size > h.bump_end[c]) {
        u32 s = span_run_take(1);
        if (s == NO_SPAN)
            return nullptr;
        h.span_class[s] = u8(c);
        h.bump[c]       = span_addr(s);
        h.bump_end[c]   = span_addr(s) + SPAN_SIZE;
    }

    u8 *p = h.bump[c];
    h.bump[c] += size;
    return p;
}

ArenaTag *tag_of(const void *p)
{
    return reinterpret_cast<ArenaTag *>(reinterpret_cast<usize>(p) - TAG_SIZE);
}

ArenaTag *tag_next(ArenaTag *t)
{
    usize at = reinterpret_cast<usize>(t) + t->size;
    return (at & (SPAN_SIZE - 1)) == 0 ? nullptr : reinterpret_cast<ArenaTag *>(at);
}

ArenaTag *tag_prev(ArenaTag *t)
{
    if (t->prev_size == 0)
        return nullptr;
    return reinterpret_cast<ArenaTag *>(reinterpret_cast<usize>(t) - t->prev_size);
}

void arena_link(ArenaFree *f)
{
    ArenaFree *prev = nullptr;
    ArenaFree *cur  = h.arena_free;
    while (cur && cur < f) {
        prev = cur;
        cur  = cur->next;
    }
    f->prev = prev;
    f->next = cur;
    if (cur)
        cur->prev = f;
    if (prev)
        prev->next = f;
    else
        h.arena_free = f;
}

void arena_unlink(ArenaFree *f)
{
    if (f->prev)
        f->prev->next = f->next;
    else
        h.arena_free = f->next;
    if (f->next)
        f->next->prev = f->prev;
}

// `to` takes `from`'s place on the list; nothing free lies between them.
void arena_relink(ArenaFree *from, ArenaFree *to)
{
    to->next = from->next;
    to->prev = from->prev;
    if (to->next)
        to->next->prev = to;
    if (to->prev)
        to->prev->next = to;
    else
        h.arena_free = to;
}

ArenaTag *alloc_arena(usize n)
{
    u32 need     = arena_size(n);
    ArenaFree *f = h.arena_free;
    while (f && f->tag.size < need)
        f = f->next;

    if (!f) {
        u32 s = span_run_take(1);
        if (s == NO_SPAN)
            return nullptr;
        h.span_class[s]  = SPAN_ARENA;
        f                = reinterpret_cast<ArenaFree *>(span_addr(s));
        f->tag.size      = SPAN_SIZE;
        f->tag.prev_size = 0;
        f->tag.state     = TAG_FREE;
        arena_link(f);
        h.arena_empty++;
    }

    ArenaTag *t = &f->tag;
    if (t->size == SPAN_SIZE)
        h.arena_empty--;
    if (t->size - need >= ARENA_MIN) {
        auto *rest          = reinterpret_cast<ArenaFree *>(reinterpret_cast<u8 *>(t) + need);
        rest->tag.size      = t->size - need;
        rest->tag.prev_size = need;
        rest->tag.state     = TAG_FREE;
        if (ArenaTag *after = tag_next(&rest->tag))
            after->prev_size = rest->tag.size;
        arena_relink(f, rest);
        t->size = need;
    } else {
        arena_unlink(f);
    }
    t->state = TAG_LIVE;
    return t;
}

void free_arena(ArenaTag *t)
{
    t->state      = TAG_FREE;
    auto *f       = reinterpret_cast<ArenaFree *>(t);
    bool listed   = false;
    ArenaTag *nxt = tag_next(t);
    if (nxt && nxt->state == TAG_FREE) {
        t->size += nxt->size;
        arena_relink(reinterpret_cast<ArenaFree *>(nxt), f);
        listed = true;
    }
    ArenaTag *prv = tag_prev(t);
    if (prv && prv->state == TAG_FREE) {
        prv->size += t->size;
        if (listed)
            arena_unlink(f);
        t      = prv;
        f      = reinterpret_cast<ArenaFree *>(prv);
        listed = true;
    }
    if (!listed)
        arena_link(f);
    if (ArenaTag *after = tag_next(t))
        after->prev_size = t->size;

    if (t->size == SPAN_SIZE) {
        if (h.arena_empty == 0) {
            h.arena_empty++;
        } else {
            arena_unlink(f);
            free_run_insert(span_of(t), 1);
        }
    }
}

ArenaTag *live_tag(const void *p, Str who)
{
    ArenaTag *t = tag_of(p);
    if ((reinterpret_cast<usize>(p) & 15) != 0 || t->state != TAG_LIVE)
        panic(who);
    return t;
}
} // namespace

void heap_init(u32 base)
{
    usize start = base ? base : reinterpret_cast<usize>(&__heap_base);
    start       = (start + SPAN_SIZE - 1) & ~(SPAN_SIZE - 1);

    for (usize i = 0; i < MAX_SPANS; i++)
        h.span_class[i] = SPAN_UNUSED;
    for (usize c = 0; c < NUM_CLASSES; c++) {
        h.free_list[c] = nullptr;
        h.bump[c]      = nullptr;
        h.bump_end[c]  = nullptr;
    }

    h.arena_free  = nullptr;
    h.arena_empty = 0;
    h.free_head   = NO_SPAN;
    h.next_span   = u32(start >> SPAN_SHIFT);
    h.first_span  = h.next_span;
    h.span_limit  = MAX_SPANS;
    h.stats       = HeapStats{};
    h.ready       = true;

    if (h.next_span >= MAX_SPANS)
        panic("heap_init: base above the span table");
}

void *heap_alloc(usize n)
{
    if (!h.ready)
        panic("heap_alloc before heap_init");
    if (n == 0)
        n = 1;

    void *p;
    usize accounted;
    if (n <= MAX_SMALL) {
        usize c   = class_of(n);
        p         = alloc_small(c);
        accounted = SIZE_CLASS[c];
    } else if (n <= ARENA_MAX) {
        ArenaTag *t = alloc_arena(n);
        p           = t ? reinterpret_cast<u8 *>(t) + TAG_SIZE : nullptr;
        accounted   = t ? t->size : 0;
    } else {
        u32 count = u32((n + SPAN_SIZE - 1) >> SPAN_SHIFT);
        u32 s     = span_run_take(count);
        if (s == NO_SPAN) {
            h.stats.fails++;
            return nullptr;
        }
        for (u32 i = 0; i < count; i++)
            h.span_class[s + i] = i == 0 ? SPAN_LARGE : SPAN_LARGE_CONT;
        h.span_run[s] = count;
        p             = span_addr(s);
        accounted     = usize(count) << SPAN_SHIFT;
    }

    if (!p) {
        h.stats.fails++;
        return nullptr;
    }
    h.stats.allocs++;
    h.stats.bytes_in_use += accounted;
    return p;
}

void heap_free(void *p)
{
    if (!p)
        return;

    u32 s = span_of(p);
    if (s >= MAX_SPANS)
        panic("heap_free: pointer outside the heap");

    u8 c = h.span_class[s];
    if (c < NUM_CLASSES) {
        *reinterpret_cast<void **>(p) = h.free_list[c];
        h.free_list[c]                = p;
        h.stats.bytes_in_use -= SIZE_CLASS[c];
    } else if (c == SPAN_LARGE) {
        u32 count = h.span_run[s];
        h.stats.bytes_in_use -= usize(count) << SPAN_SHIFT;
        free_run_insert(s, count);
    } else if (c == SPAN_ARENA) {
        ArenaTag *t = live_tag(p, "heap_free: not an allocation");
        h.stats.bytes_in_use -= t->size;
        free_arena(t);
    } else {
        panic("heap_free: not an allocation");
    }
    h.stats.frees++;
}

HeapStats heap_stats()
{
    return h.stats;
}

usize heap_origin()
{
    return usize(h.first_span) << SPAN_SHIFT;
}

usize heap_block_size(usize n)
{
    if (n == 0)
        n = 1;
    if (n <= MAX_SMALL)
        return SIZE_CLASS[class_of(n)];
    if (n <= ARENA_MAX)
        return arena_size(n) - TAG_SIZE;
    return ((n + SPAN_SIZE - 1) >> SPAN_SHIFT) << SPAN_SHIFT;
}

usize heap_usable_size(const void *p)
{
    if (!p)
        return 0;

    u32 s = span_of(p);
    if (s >= MAX_SPANS)
        panic("heap_usable_size: pointer outside the heap");

    u8 c = h.span_class[s];
    if (c < NUM_CLASSES)
        return SIZE_CLASS[c];
    if (c == SPAN_LARGE)
        return usize(h.span_run[s]) << SPAN_SHIFT;
    if (c == SPAN_ARENA)
        return live_tag(p, "heap_usable_size: not an allocation")->size - TAG_SIZE;
    panic("heap_usable_size: not an allocation");
}

// Coroutine frames allocate through these (Concept.md §8.2). With
// -fno-exceptions a failed allocation returns null; callers must check.
void *operator new(usize n)
{
    return heap_alloc(n);
}

// The form a Task's frame uses, because TaskPromise declares
// get_return_object_on_allocation_failure. Null is a value here, not a fault.
// Nothing else reaches it, so `frames` is exactly the coroutine-frame count.
void *operator new(usize n, const std::nothrow_t &) noexcept
{
    h.stats.frames++;
    return heap_alloc(n);
}

void *operator new[](usize n)
{
    return heap_alloc(n);
}

void operator delete(void *p) noexcept
{
    heap_free(p);
}

void operator delete[](void *p) noexcept
{
    heap_free(p);
}

void operator delete(void *p, usize) noexcept
{
    heap_free(p);
}

void operator delete[](void *p, usize) noexcept
{
    heap_free(p);
}
