#include "harness.h"
#include "kernel/sysabi.h"
#include "proc/size.h"

namespace {

// A spec parsed and applied in one step, so a case reads as the command line
// it came from. `bad` is what a refusal reports.
constexpr u64 BAD = ~u64(0);

u64 at(Str spec, u64 cur)
{
    Result<SizeSpec> s = parse_size(spec);
    if (s.is_err())
        return BAD;
    Result<u64> n = size_apply(s.value(), cur);
    return n.is_err() ? BAD : n.value();
}

bool eq(Str spec, u64 cur, u64 want)
{
    return at(spec, cur) == want;
}

} // namespace

void test_size()
{
    // No modifier: the size named, whatever the file has now.
    CHECK(eq("0", 900, 0));
    CHECK(eq("100", 0, 100));
    CHECK(eq("100", 900, 100));

    // + and -, and a shrink past the start is empty rather than an error.
    CHECK(eq("+10", 90, 100));
    CHECK(eq("-10", 90, 80));
    CHECK(eq("-90", 90, 0));
    CHECK(eq("-91", 90, 0));

    // < is at most and > at least, so each moves in one direction only.
    CHECK(eq("<100", 900, 100));
    CHECK(eq("<100", 90, 90));
    CHECK(eq(">100", 90, 100));
    CHECK(eq(">100", 900, 900));

    // / rounds down to a multiple and % up; both leave an exact one alone.
    CHECK(eq("/512", 1000, 512));
    CHECK(eq("/512", 1024, 1024));
    CHECK(eq("/512", 100, 0));
    CHECK(eq("%512", 1000, 1024));
    CHECK(eq("%512", 1024, 1024));
    CHECK(eq("%512", 1, 512));
    CHECK(eq("%512", 0, 0));

    // A multiple of zero is not one.
    CHECK(eq("/0", 100, BAD));
    CHECK(eq("%0", 100, BAD));
    CHECK(eq("0", 100, 0)); // but a plain zero still empties it

    // K, M, G and T are 1024; the B forms are 1000. A unit rides a modifier.
    CHECK(eq("1K", 0, 1024));
    CHECK(eq("1KB", 0, 1000));
    CHECK(eq("2M", 0, 2ull * 1024 * 1024));
    CHECK(eq("2MB", 0, 2000000));
    CHECK(eq("1G", 0, 1024ull * 1024 * 1024));
    CHECK(eq("1GB", 0, 1000000000));
    CHECK(eq("1T", 0, 1024ull * 1024 * 1024 * 1024));
    CHECK(eq("1TB", 0, 1000000000000ull));
    CHECK(eq("+1K", 24, 1048));
    CHECK(eq("%1K", 1, 1024));

    // Not a size at all.
    CHECK(eq("", 0, BAD));
    CHECK(eq("+", 0, BAD));
    CHECK(eq("-", 0, BAD));
    CHECK(eq("x", 0, BAD));
    CHECK(eq("1x", 0, BAD));
    CHECK(eq("1KBB", 0, BAD));
    CHECK(eq("1 K", 0, BAD));
    CHECK(eq("K", 0, BAD));
    CHECK(eq("+ 1", 0, BAD));
    CHECK(eq("1.5K", 0, BAD));
    CHECK(eq("0x10", 0, BAD));

    // Nothing may exceed the wire's own limit, at the digits or at the unit.
    CHECK(eq("99999999999999999999999", 0, BAD));
    CHECK(eq("9999999T", 0, BAD));
    CHECK(eq("+1", SYS_SEEK_MAX, BAD));
    CHECK(eq("%512", SYS_SEEK_MAX, BAD));
    CHECK(at("+0", SYS_SEEK_MAX) == SYS_SEEK_MAX);

    // The modifier survives the parse on its own, since -o scales the number
    // afterwards and must not disturb it.
    Result<SizeSpec> s = parse_size("+2");
    CHECK(s.is_ok());
    CHECK(s.value().mod == SizeMod::Plus);
    CHECK_EQ(u32(s.value().n), 2u);
    CHECK_EQ(u32(SIZE_BLOCK), 512u);
}
