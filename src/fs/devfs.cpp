#include "devfs.h"

#include "chacha.h"
#include "kernel/alloc.h"
#include "kernel/host.h"

namespace {

enum class DevKind : u8 { Null, Random, Urandom, Zero };

struct DevNode {
    Str name;
    DevKind kind;
};

// A name is a row; two names for one kind would be two rows.
constexpr DevNode DEVICES[] = {
    { "null", DevKind::Null },
    { "random", DevKind::Random },
    { "urandom", DevKind::Urandom },
    { "zero", DevKind::Zero },
};

const DevNode *node_of(Str name)
{
    for (const DevNode &d : DEVICES)
        if (d.name == name)
            return &d;
    return nullptr;
}

struct DevFs final : Fs {
    Str kind() const override { return "devfs"; }

    // The table takes no new name; a device still takes bytes.
    bool writable() const override { return false; }

    bool file_writable() const override { return true; }

    // No file behind a handle, so nothing to share.
    bool shares_handles() const override { return false; }

    // 0, as Linux reports for a character device.
    Task<Result<Stat>> stat(Str path) override
    {
        if (path == "/")
            co_return Stat{ NodeKind::Dir, 0 };
        if (!node_of(path.substr(1)))
            co_return Err(Error::NotFound);
        co_return Stat{ NodeKind::File, 0 };
    }

    Task<Result<Vec<Entry>>> list(Str path) override
    {
        if (path != "/")
            co_return Err(Error::NotDir);

        Vec<Entry> out;
        for (const DevNode &d : DEVICES) {
            Entry e;
            e.kind = NodeKind::File;
            if (!e.name.assign(d.name) || !out.push(move(e)))
                co_return Err(Error::NoMemory);
        }
        co_return move(out);
    }

    // A handle is its kind and nothing else; the slot holds kind + 1, so 0 is
    // free. O_TRUNC and O_APPEND are ignored, and O_CREATE creates no name.
    Task<Result<u32>> open(Str path, u32) override
    {
        if (path == "/")
            co_return Err(Error::IsDir);

        const DevNode *d = node_of(path.substr(1));
        if (!d)
            co_return Err(Error::NotFound);

        u8 slot = u8(u8(d->kind) + 1);
        for (usize h = 0; h < open_.size(); h++) {
            if (!open_[h]) {
                open_[h] = slot;
                co_return u32(h);
            }
        }
        if (!open_.push(slot))
            co_return Err(Error::NoMemory);
        co_return u32(open_.size() - 1);
    }

    Task<Result<void>> mkdir(Str) override { co_return Err(Error::Perm); }

    Task<Result<void>> remove(Str, bool) override { co_return Err(Error::Perm); }

    // The offset is ignored and the count is always met: a device is a stream.
    // random is a host draw per read; urandom is one draw ever, expanded; null
    // is the end of input at once.
    Result<usize> read(u32 h, u64, u8 *buf, usize n) override
    {
        if (h >= open_.size() || !open_[h])
            return Err(Error::Invalid);
        switch (DevKind(open_[h] - 1)) {
        case DevKind::Null:
            return usize(0);
        case DevKind::Zero:
            __builtin_memset(buf, 0, n);
            return n;
        case DevKind::Random:
            host_random(u32(reinterpret_cast<usize>(buf)), u32(n));
            return n;
        case DevKind::Urandom: {
            if (!prng_.seeded()) {
                u8 key[CHACHA_KEY];
                host_random(u32(reinterpret_cast<usize>(key)), u32(sizeof key));
                prng_.seed(key);
            }
            prng_.fill(buf, n);
            return n;
        }
        }
        return Err(Error::Invalid);
    }

    // Every device takes the whole of a write and keeps none of it.
    Result<usize> write(u32 h, u64, const u8 *, usize n) override
    {
        if (h >= open_.size() || !open_[h])
            return Err(Error::Invalid);
        return n;
    }

    // No size, rather than a size of nought: the read path clamps a request to
    // what a file has left only when this answers, and a device never ends.
    Result<u64> size(u32) override { return Err(Error::Unsupported); }

    // No length to set either, which is Linux's EINVAL on a character device.
    Result<void> truncate(u32, u64) override { return Err(Error::Invalid); }

    void close(u32 h) override
    {
        if (h < open_.size())
            open_[h] = 0;
    }

private:
    Vec<u8> open_;

    // The mount's, not a handle's: every reader advances the one generator.
    ChaCha prng_;
};

} // namespace

Fs *devfs_create()
{
    return heap_new<DevFs>();
}
