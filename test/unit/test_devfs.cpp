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

    // The table is the whole directory, in vfs_list's byte order rather than
    // the table's, and a device measures 0 as it does on Linux.
    Result<Vec<Entry>> ls = run_now(vfs_list("/dev"));
    CHECK(ls.is_ok());
    CHECK_EQ(ls.value().size(), 4u);
    CHECK(ls.value()[0].name.str() == "null");
    CHECK_EQ(ls.value()[0].size, 0u);
    CHECK(ls.value()[1].name.str() == "random");
    CHECK_EQ(ls.value()[1].size, 0u);
    CHECK(ls.value()[2].name.str() == "urandom");
    CHECK_EQ(ls.value()[2].size, 0u);
    CHECK(ls.value()[3].name.str() == "zero");
    CHECK_EQ(ls.value()[3].size, 0u);

    const Str NAMES[] = { "/dev/null", "/dev/random", "/dev/urandom", "/dev/zero" };
    for (Str name : NAMES) {
        Result<Stat> st = run_now(vfs_stat(name));
        CHECK(st.is_ok());
        CHECK(st.value().kind == NodeKind::File);
        CHECK_EQ(st.value().size, 0u);
    }
    CHECK(run_now(vfs_stat("/dev/nosuch")).error() == Error::NotFound);
    CHECK(run_now(vfs_stat("/dev")).value().kind == NodeKind::Dir);

    // The namespace is shut whatever the devices take: no name is added,
    // removed, renamed or restamped, and O_CREATE creates none either.
    CHECK(run_now(vfs_open("/dev/nosuch", O_READ)).error() == Error::NotFound);
    CHECK(run_now(vfs_open("/dev/nosuch", O_WRITE | O_CREATE)).error() == Error::NotFound);
    CHECK(run_now(vfs_open("/dev", O_READ)).error() == Error::IsDir);
    CHECK(run_now(vfs_open("/dev", O_WRITE)).error() == Error::IsDir);
    CHECK(run_now(vfs_mkdir("/dev/sub")).error() == Error::Perm);
    CHECK(run_now(vfs_remove("/dev/random", false)).error() == Error::Perm);
    CHECK(run_now(vfs_remove("/dev/null", false)).error() == Error::Perm);
    CHECK(run_now(vfs_touch("/dev/null")).error() == Error::Perm);

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
    // A descriptor that cannot write cannot truncate: the VFS answers first.
    CHECK(vfs_truncate(fd.value(), 0).error() == Error::Perm);

    // A second descriptor gets a handle of its own (Concept.md §5.2), and
    // draws bytes of its own.
    Result<i32> other = run_now(vfs_open("/dev/random", O_READ));
    CHECK(other.is_ok());
    CHECK_EQ(draw(other.value(), 0, a, sizeof a), sizeof a);
    CHECK_EQ(draw(fd.value(), 0, b, sizeof b), sizeof b);
    CHECK(!same(a, b, sizeof a));

    vfs_close(other.value());
    CHECK_EQ(draw(fd.value(), 0, a, 16), 16u);

    // urandom answers the same way, out of a generator seeded on its first
    // read rather than a draw per read.
    Result<i32> u = run_now(vfs_open("/dev/urandom", O_READ));
    CHECK(u.is_ok());
    CHECK_EQ(draw(u.value(), 0, a, sizeof a), sizeof a);
    CHECK_EQ(draw(u.value(), sizeof a, b, sizeof b), sizeof b);
    CHECK(!same(a, b, sizeof a));
    CHECK_EQ(draw(u.value(), 1u << 30, a, 1u), 1u);
    CHECK_EQ(draw(u.value(), 0, a, 0), 0u);

    CHECK(vfs_size(u.value()).error() == Error::Unsupported);
    CHECK(vfs_truncate(u.value(), 0).error() == Error::Perm);

    // A second descriptor is a second handle, but the generator is the mount's,
    // so it sees the stream go on rather than repeat.
    Result<i32> u2 = run_now(vfs_open("/dev/urandom", O_READ));
    CHECK(u2.is_ok());
    CHECK_EQ(draw(u2.value(), 0, a, sizeof a), sizeof a);
    CHECK_EQ(draw(u.value(), 0, b, sizeof b), sizeof b);
    CHECK(!same(a, b, sizeof a));
    vfs_close(u2.value());

    // Two devices, two streams.
    CHECK_EQ(draw(fd.value(), 0, a, 64), 64u);
    CHECK_EQ(draw(u.value(), 0, b, 64), 64u);
    CHECK(!same(a, b, 64));

    // Every device takes a write and keeps none of it, as Linux does. There is
    // still no length to set: that is the EINVAL Linux answers.
    Result<i32> rw = run_now(vfs_open("/dev/random", O_READ | O_WRITE));
    CHECK(rw.is_ok());
    CHECK_EQ(vfs_write(rw.value(), 0, a, sizeof a).value(), sizeof a);
    CHECK(vfs_truncate(rw.value(), 0).error() == Error::Invalid);
    vfs_close(rw.value());

    vfs_close(u.value());
    vfs_close(fd.value());

    // Reading null is the end of input at once, whatever the offset.
    Result<i32> nr = run_now(vfs_open("/dev/null", O_READ));
    CHECK(nr.is_ok());
    CHECK_EQ(draw(nr.value(), 0, a, sizeof a), 0u);
    CHECK_EQ(draw(nr.value(), 1u << 30, a, 1u), 0u);
    CHECK(vfs_size(nr.value()).error() == Error::Unsupported);
    vfs_close(nr.value());

    // Two descriptors writing to one device at once. The table shares no handle
    // here, so neither refuses the other (Concept.md §5.2) — which is what a
    // pipeline's two stages redirecting to /dev/null need.
    Result<i32> n1 = run_now(vfs_open("/dev/null", O_WRITE | O_CREATE | O_TRUNC));
    CHECK(n1.is_ok());
    Result<i32> n2 = run_now(vfs_open("/dev/null", O_WRITE | O_CREATE | O_TRUNC));
    CHECK(n2.is_ok());
    CHECK_EQ(vfs_write(n1.value(), 0, a, sizeof a).value(), sizeof a);
    CHECK_EQ(vfs_write(n2.value(), 0, a, sizeof a).value(), sizeof a);
    vfs_close(n2.value());
    CHECK_EQ(vfs_write(n1.value(), 0, a, 1).value(), 1u); // the other still lives
    CHECK(vfs_truncate(n1.value(), 0).error() == Error::Invalid);
    vfs_close(n1.value());

    // zero meets a read in full and every byte of it is zero.
    Result<i32> z = run_now(vfs_open("/dev/zero", O_READ | O_WRITE));
    CHECK(z.is_ok());
    for (usize i = 0; i < sizeof a; i++)
        a[i] = 0xff;
    CHECK_EQ(draw(z.value(), 1u << 30, a, sizeof a), sizeof a);
    for (usize i = 0; i < sizeof a; i++)
        CHECK_EQ(a[i], 0u);
    CHECK_EQ(draw(z.value(), 0, a, 0), 0u);
    CHECK_EQ(vfs_write(z.value(), 0, a, sizeof a).value(), sizeof a);
    vfs_close(z.value());

    // A fresh mount is a fresh seed: the generator belongs to the DevFs the
    // mount holds, and nothing of it outlives one.
    vfs_reset();
    CHECK(vfs_mount("/dev", devfs_create()).is_ok());
    Result<i32> again = run_now(vfs_open("/dev/urandom", O_READ));
    CHECK(again.is_ok());
    CHECK_EQ(draw(again.value(), 0, a, 32), 32u);
    vfs_close(again.value());

    vfs_reset();
    CHECK(vfs_mount("/dev", devfs_create()).is_ok());
    Result<i32> once_more = run_now(vfs_open("/dev/urandom", O_READ));
    CHECK(once_more.is_ok());
    CHECK_EQ(draw(once_more.value(), 0, b, 32), 32u);
    vfs_close(once_more.value());
    CHECK(!same(a, b, 32));

    vfs_reset();
}
