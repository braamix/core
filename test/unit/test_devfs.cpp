#include "fs/devfs.h"
#include "fs/vfs.h"
#include "harness.h"

namespace {

// One read of an open descriptor, the way the read syscall makes it.
usize draw(i32 fd, u64 off, u8 *buf, usize n)
{
    Result<usize> r = vfs_read(fd, off, buf, n);
    return r.is_ok() ? r.value() : 0;
}

bool same(const u8 *a, const u8 *b, usize n)
{
    for (usize i = 0; i < n; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

} // namespace

void test_devfs()
{
    test_begin("devfs");

    vfs_reset();
    Fs *dev = devfs_create();
    CHECK(dev != nullptr);
    CHECK(vfs_mount("/dev", dev).is_ok());

    // The table is the whole directory, and a device measures 0 as it does on
    // Linux.
    Result<Vec<Entry>> ls = run_now(vfs_list("/dev"));
    CHECK(ls.is_ok());
    CHECK_EQ(ls.value().size(), 1u);
    CHECK(ls.value()[0].name.str() == "random");
    CHECK_EQ(ls.value()[0].size, 0u);

    Result<Stat> st = run_now(vfs_stat("/dev/random"));
    CHECK(st.is_ok());
    CHECK(st.value().kind == NodeKind::File);
    CHECK_EQ(st.value().size, 0u);
    CHECK(run_now(vfs_stat("/dev/nosuch")).error() == Error::NotFound);
    CHECK(run_now(vfs_stat("/dev")).value().kind == NodeKind::Dir);

    // Read-only, and not a directory anyone may add to.
    CHECK(run_now(vfs_open("/dev/random", O_READ | O_WRITE)).error() == Error::Perm);
    CHECK(run_now(vfs_open("/dev/nosuch", O_READ)).error() == Error::NotFound);
    CHECK(run_now(vfs_open("/dev", O_READ)).error() == Error::IsDir);
    CHECK(run_now(vfs_mkdir("/dev/sub")).error() == Error::Perm);
    CHECK(run_now(vfs_remove("/dev/random", false)).error() == Error::Perm);

    Result<i32> fd = run_now(vfs_open("/dev/random", O_READ));
    CHECK(fd.is_ok());

    // Every read is met in full, whatever the offset, and no two are alike.
    u8 a[512], b[512];
    CHECK_EQ(draw(fd.value(), 0, a, sizeof a), sizeof a);
    CHECK_EQ(draw(fd.value(), sizeof a, b, sizeof b), sizeof b);
    CHECK(!same(a, b, sizeof a));
    CHECK_EQ(draw(fd.value(), 1u << 30, a, 1u), 1u);
    CHECK_EQ(draw(fd.value(), 0, a, 0), 0u);

    // No size at all, rather than a size of nought: that is what keeps the read
    // path from clamping a request to what a file has left (src/user/syscall.cpp).
    CHECK(vfs_size(fd.value()).error() == Error::Unsupported);
    CHECK(vfs_write(fd.value(), 0, a, sizeof a).error() == Error::Perm);
    CHECK(vfs_truncate(fd.value(), 0).error() == Error::Perm);

    // A second descriptor shares the one backend handle (Concept.md §5.2), and
    // still draws bytes of its own.
    Result<i32> other = run_now(vfs_open("/dev/random", O_READ));
    CHECK(other.is_ok());
    CHECK_EQ(draw(other.value(), 0, a, sizeof a), sizeof a);
    CHECK_EQ(draw(fd.value(), 0, b, sizeof b), sizeof b);
    CHECK(!same(a, b, sizeof a));

    vfs_close(other.value());
    CHECK_EQ(draw(fd.value(), 0, a, 16), 16u);
    vfs_close(fd.value());

    vfs_reset();
}
