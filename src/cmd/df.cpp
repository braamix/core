#include "kernel/fmt.h"
#include "kernel/text.h"
#include "proc/file.h"
#include "proc/usage.h"

// The mount table (Concept.md §5.3), as BSD prints it. The mounts come from
// /proc, which is text; the quota and the usage are a syscall, because ProcFs
// generates its files synchronously and asking the host is not.
//
// The backend and the durability are on the boot banner instead: neither is a
// per-mount fact, and boot starts nothing without OPFS.

namespace {

// BSD's column widths, which put a row in sixty columns and are the floor a
// column grows from. The capacity value is seven wide with four spaces after
// it; its header is nine wide with two.
constexpr usize W_NAME = 10, W_FIRST = 11, W_SIZE = 9, W_CAP = 7;

// Widest cell plus a space.
void widen(usize &w, Str s)
{
    if (s.size() + 1 > w)
        w = s.size() + 1;
}

// Kibibyte blocks.
u64 blocks_of(u64 bytes)
{
    return bytes / 1024;
}

// 10G, 121M, 512B — one decimal below ten.
void human(Buf<32> &b, u64 bytes)
{
    constexpr Str UNIT[] = { "B", "K", "M", "G", "T", "P" };
    usize u              = 0;
    u64 whole = bytes, rem = 0;
    while (whole >= 1024 && u + 1 < sizeof(UNIT) / sizeof(UNIT[0])) {
        rem = whole % 1024;
        whole /= 1024;
        u++;
    }
    b.put(whole);
    if (u && whole < 10) {
        u64 tenth = (rem * 10) / 1024;
        if (tenth)
            b.put('.').put(tenth);
    }
    b.put(UNIT[u]);
}

// One line of /proc/mounts, as df prints it.
struct Row {
    Str prefix, kind;
    Buf<32> first, used, avail, cap;
};

// The four cells of a row, or false when the line names no mount.
bool row_of(Str line, const StorageInfo &b, bool human_sizes, Row &r)
{
    r.prefix  = next_field(line);
    r.kind    = next_field(line);
    next_field(line); // rw|ro, which `mount` is the command for
    Str bytes = next_field(line);
    if (r.prefix.empty())
        return false;

    // An OPFS mount is part of the origin's usage and has no figure of its
    // own, so it reports the origin's. Any other filesystem answers for what
    // it holds, all of it in use.
    bool opfs = r.kind == "opfs";
    u64 total = 0, used = 0;
    bool known = true;
    if (opfs) {
        known = b.known;
        total = b.quota;
        used  = b.usage > b.quota ? b.quota : b.usage;
    } else {
        Option<u32> held = parse_u32(bytes);
        total = used = held ? held.value() : 0;
    }

    // Blocks, not bytes, decide what is left: the three columns add up however
    // the division rounded.
    u64 tb = blocks_of(total), ub = blocks_of(used);

    if (!known) {
        r.first.put('-');
        r.used.put('-');
        r.avail.put('-');
    } else if (human_sizes) {
        human(r.first, total);
        human(r.used, used);
        human(r.avail, total - used);
    } else {
        r.first.put(tb);
        r.used.put(ub);
        r.avail.put(tb - ub);
    }

    // Truncated, as BSD's is: 55.87% reads 55%. Nothing to divide by is not
    // nought per cent.
    if (!known || !tb)
        r.cap.put('-');
    else
        r.cap.put(ub * 100 / tb).put('%');
    return true;
}

constexpr Str USAGE =
    "Usage:\n"
    "    df [-h]\n"
    "Options:\n"
    "    -h    scale the sizes for a reader\n";

} // namespace

Task<i32> proc_main(Args args)
{
    // -h is this program's own, so only the long spelling asks.
    if (args.size() == 2 && args[1] == "--help")
        co_return co_await usage_asked(USAGE);

    bool human_sizes = false;
    if (args.size() > 2 || (args.size() == 2 && args[1] != "-h"))
        co_return co_await usage_error(USAGE);
    if (args.size() == 2)
        human_sizes = true;

    StorageInfo b;
    if (Task<Result<StorageInfo>> t = storage_of()) {
        Result<StorageInfo> r = co_await t;
        if (r.is_err() && r.error() == Error::Cancelled)
            co_return 130;
        if (r.is_ok())
            b = r.value();
    }

    Result<String> mounts = Err(Error::NoMemory);
    if (Task<Result<String>> t = read_file("/proc/mounts"))
        mounts = co_await t;
    if (mounts.is_err()) {
        if (mounts.error() == Error::Cancelled)
            co_return 130;
        if (Task<void> e = errln("df", "/proc/mounts", mounts.error()))
            co_await e;
        co_return 1;
    }

    // First pass: the widths, since a value wider than its column would run
    // into the one before it.
    Str first_head = human_sizes ? Str("Size") : Str("1K-blocks");
    usize w_name = W_NAME, w_first = W_FIRST, w_used = W_SIZE, w_avail = W_SIZE;
    {
        Str rest = mounts.value().str(), line;
        while (next_line(rest, line)) {
            Row r;
            if (!row_of(line, b, human_sizes, r))
                continue;
            widen(w_name, r.kind);
            widen(w_first, r.first.str());
            widen(w_used, r.used.str());
            widen(w_avail, r.avail.str());
        }
        widen(w_first, first_head);
        widen(w_used, "Used");
        widen(w_avail, "Avail");
    }

    Buf<128> head;
    head.put_left("Filesystem", w_name).put_right(first_head, w_first);
    head.put_right("Used", w_used).put_right("Avail", w_avail);
    head.put_right("Capacity", W_CAP + 2).put("  Mounted on\n");
    if ((co_await File::stdout().write(head.str())).is_err())
        co_return 1;

    Str rest = mounts.value().str(), line;
    while (next_line(rest, line)) {
        Buf<128> out;
        {
            Row r;
            if (!row_of(line, b, human_sizes, r))
                continue;
            out.put_left(r.kind, w_name).put_right(r.first.str(), w_first);
            out.put_right(r.used.str(), w_used).put_right(r.avail.str(), w_avail);
            out.put_right(r.cap.str(), W_CAP).put("    ").put(r.prefix).put('\n');
        }
        if ((co_await File::stdout().write(out.str())).is_err())
            co_return 1;
    }
    if ((co_await File::stdout().flush()).is_err())
        co_return 1;
    co_return 0;
}
