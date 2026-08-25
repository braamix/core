#include "devfs.h"

#include "kernel/alloc.h"
#include "kernel/host.h"

namespace {

enum class DevKind : u8 { Random };

struct DevNode {
    Str name;
    DevKind kind;
};

// A name is a row; two names for one kind would be two rows.
constexpr DevNode DEVICES[] = { { "random", DevKind::Random } };

const DevNode *node_of(Str name)
{
    for (const DevNode &d : DEVICES)
        if (d.name == name)
            return &d;
    return nullptr;
}

struct DevFs final : Fs {
    Str kind() const override { return "devfs"; }

    bool writable() const override { return false; }

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
    // free.
    Task<Result<u32>> open(Str path, u32 flags) override
    {
        if (flags & (O_WRITE | O_CREATE | O_TRUNC | O_APPEND))
            co_return Err(Error::Perm);
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

    // The offset is ignored and the count is always met: a device is a stream,
    // and every byte of it is one the host just drew.
    Result<usize> read(u32 h, u64, u8 *buf, usize n) override
    {
        if (h >= open_.size() || !open_[h])
            return Err(Error::Invalid);
        switch (DevKind(open_[h] - 1)) {
        case DevKind::Random:
            host_random(u32(reinterpret_cast<usize>(buf)), u32(n));
            return n;
        }
        return Err(Error::Invalid);
    }

    Result<usize> write(u32, u64, const u8 *, usize) override { return Err(Error::Perm); }

    // No size, rather than a size of nought: the read path clamps a request to
    // what a file has left only when this answers, and a device never ends.
    Result<u64> size(u32) override { return Err(Error::Unsupported); }

    Result<void> truncate(u32, u64) override { return Err(Error::Perm); }

    void close(u32 h) override
    {
        if (h < open_.size())
            open_[h] = 0;
    }

private:
    Vec<u8> open_;
};

} // namespace

Fs *devfs_create()
{
    return heap_new<DevFs>();
}
