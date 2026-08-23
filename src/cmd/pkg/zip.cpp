#include "zip.h"

namespace {

constexpr u32 EOCD_SIG    = 0x06054b50;
constexpr u32 CENTRAL_SIG = 0x02014b50;
constexpr u32 LOCAL_SIG   = 0x04034b50;

constexpr usize EOCD_BYTES    = 22;
constexpr usize CENTRAL_BYTES = 46;
constexpr usize LOCAL_BYTES   = 30;

constexpr usize COMMENT_MAX = 0xffff;

u32 u16at(Str s, usize at)
{
    return u32(u8(s[at])) | (u32(u8(s[at + 1])) << 8);
}

u32 u32at(Str s, usize at)
{
    return u16at(s, at) | (u16at(s, at + 2) << 16);
}

constexpr bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// A name out of an archive is not a path until it has been looked at.
bool name_ok(Str name)
{
    if (name.empty() || name[0] == '/')
        return false;
    if (name.size() >= 2 && is_alpha(name[0]) && name[1] == ':')
        return false;
    for (char c : name)
        if (c == '\\')
            return false;

    Str rest = name;
    while (!rest.empty()) {
        Str part = rest.split('/', rest);
        if (part == "." || part == "..")
            return false;
    }
    return true;
}

// The last EOCD in `window`, the tail of the archive, or npos.
usize find_eocd(Str window)
{
    if (window.size() < EOCD_BYTES)
        return Str::npos;
    for (usize at = window.size() - EOCD_BYTES + 1; at-- > 0;)
        if (u32at(window, at) == EOCD_SIG)
            return at;
    return Str::npos;
}

// `n` bytes at `off`, as a String of exactly that length.
Task<Result<String>> src_read(ZipSource &src, u64 off, u64 n)
{
    if (n > src.size() || off > src.size() - n)
        co_return Err(Error::Invalid);

    // Reserved first, so the fill cannot fail and the read has somewhere to go.
    String out;
    if (!out.reserve(usize(n)))
        co_return Err(Error::NoMemory);
    for (u64 i = 0; i < n; i++)
        out.push(0);

    if (n) {
        Span<u8> into(reinterpret_cast<u8 *>(out.data()), usize(n));
        Result<void> r = Err(Error::NoMemory);
        if (Task<Result<void>> t = src.read(off, into))
            r = co_await t;
        if (r.is_err())
            co_return Err(r.error());
    }
    co_return move(out);
}

} // namespace

Task<Result<void>> MemZipSource::read(u64 off, Span<u8> out)
{
    if (out.size() > bytes_.size() || off > bytes_.size() - out.size())
        co_return Err(Error::Invalid);
    for (usize i = 0; i < out.size(); i++)
        out[i] = u8(bytes_[usize(off) + i]);
    co_return {};
}

Task<ZipRead> zip_entries(ZipSource &src, ZipDir &out)
{
    u64 total = src.size();
    if (total < EOCD_BYTES)
        co_return ZipRead::Malformed;

    // The last EOCD_BYTES are the end record itself when there is no comment,
    // which is the usual archive; only one that has one costs the window.
    u64 span            = EOCD_BYTES;
    Result<String> tail = Err(Error::NoMemory);
    if (Task<Result<String>> t = src_read(src, total - span, span))
        tail = co_await t;
    if (tail.is_err())
        co_return tail.error() == Error::NoMemory ? ZipRead::NoMemory : ZipRead::Io;

    usize found = find_eocd(tail.value().str());
    if (found == Str::npos) {
        span = EOCD_BYTES + COMMENT_MAX;
        if (span > total)
            span = total;
        if (Task<Result<String>> t = src_read(src, total - span, span))
            tail = co_await t;
        else
            co_return ZipRead::NoMemory;
        if (tail.is_err())
            co_return tail.error() == Error::NoMemory ? ZipRead::NoMemory : ZipRead::Io;
        found = find_eocd(tail.value().str());
    }
    if (found == Str::npos)
        co_return ZipRead::Malformed;

    Str end     = tail.value().str().substr(found);
    u32 count   = u16at(end, 10);
    u64 catalog = u32at(end, 16);
    u64 eocd_at = total - span + found;
    // Zip64 announces itself by saturating these.
    if (count == 0xffff || catalog == 0xffffffff)
        co_return ZipRead::Unsupported;
    if (catalog > eocd_at)
        co_return ZipRead::Malformed;

    // And one read of the directory, which every name goes on viewing.
    Result<String> dir = Err(Error::NoMemory);
    if (Task<Result<String>> t = src_read(src, catalog, eocd_at - catalog))
        dir = co_await t;
    if (dir.is_err())
        co_return dir.error() == Error::NoMemory ? ZipRead::NoMemory : ZipRead::Io;
    out.central = move(dir.value());

    Str central = out.central.str();
    usize at    = 0;
    for (u32 i = 0; i < count; i++) {
        if (at > central.size() || central.size() - at < CENTRAL_BYTES ||
            u32at(central, at) != CENTRAL_SIG)
            co_return ZipRead::Malformed;

        u32 flags     = u16at(central, at + 8);
        u32 method    = u16at(central, at + 10);
        u64 packed    = u32at(central, at + 20);
        u64 size      = u32at(central, at + 24);
        usize namelen = u16at(central, at + 28);
        u64 local     = u32at(central, at + 42);
        if (central.size() - at - CENTRAL_BYTES < namelen)
            co_return ZipRead::Malformed;
        Str name = central.substr(at + CENTRAL_BYTES, namelen);
        at += CENTRAL_BYTES + namelen + u16at(central, at + 30) + u16at(central, at + 32);

        if (flags & 1)
            co_return ZipRead::Unsupported; // encrypted
        if (name.ends_with("/"))
            continue; // a directory entry; the paths imply it anyway
        if (!name_ok(name))
            co_return ZipRead::Malformed;

        // Re-read the local header: taking the central directory's offset for
        // the data is the classic way to get this wrong.
        if (local > total || total - local < LOCAL_BYTES)
            co_return ZipRead::Malformed;
        Result<String> head = Err(Error::NoMemory);
        if (Task<Result<String>> t = src_read(src, local, LOCAL_BYTES))
            head = co_await t;
        if (head.is_err())
            co_return head.error() == Error::NoMemory ? ZipRead::NoMemory : ZipRead::Io;
        Str lh = head.value().str();
        if (u32at(lh, 0) != LOCAL_SIG)
            co_return ZipRead::Malformed;

        u64 from = local + LOCAL_BYTES + u16at(lh, 26) + u16at(lh, 28);
        if (from > total || total - from < packed)
            co_return ZipRead::Malformed;

        // Last, as parseZip judges it: the order the two readers refuse in is
        // part of what they agree on.
        if (method != ZIP_STORE && method != ZIP_DEFLATE)
            co_return ZipRead::Unsupported;

        ZipEntry e;
        e.name   = name;
        e.at     = from;
        e.packed = packed;
        e.method = method;
        e.size   = size;
        if (!out.entries.push(e))
            co_return ZipRead::NoMemory;
    }
    co_return ZipRead::Ok;
}

Task<Result<String>> zip_packed(ZipSource &src, const ZipEntry &e)
{
    co_return co_await src_read(src, e.at, e.packed);
}

Task<Result<String>> zip_stored(ZipSource &src, const ZipEntry &e)
{
    if (e.method != ZIP_STORE || e.packed != e.size)
        co_return Err(Error::Invalid);
    co_return co_await zip_packed(src, e);
}

namespace {

struct Named {
    Str name;
    ZipMeta meta;
};

// One table, read both ways, so the name and the kind cannot drift apart.
constexpr Named META[] = {
    { ".PKGINFO", ZipMeta::PkgInfo },
    { ".pre-install", ZipMeta::PreInstall },
    { ".post-install", ZipMeta::PostInstall },
    { ".pre-deinstall", ZipMeta::PreDeinstall },
    { ".post-deinstall", ZipMeta::PostDeinstall },
    { ".pre-upgrade", ZipMeta::PreUpgrade },
    { ".post-upgrade", ZipMeta::PostUpgrade },
    { ".trigger", ZipMeta::Trigger },
};

} // namespace

ZipMeta zip_meta(Str name)
{
    if (name.empty() || name[0] != '.' || name.contains("/"))
        return ZipMeta::Payload;
    for (const Named &n : META)
        if (n.name == name)
            return n.meta;
    return ZipMeta::Unknown;
}

Str zip_meta_name(ZipMeta meta)
{
    for (const Named &n : META)
        if (n.meta == meta)
            return n.name;
    return Str();
}

bool zip_meta_kept(ZipMeta meta)
{
    return meta != ZipMeta::Payload && meta != ZipMeta::PkgInfo && meta != ZipMeta::Unknown;
}

bool ZipSink::take(Str chunk)
{
    if (chunk.size() > want_ - out_.size())
        return false;
    return out_.append(chunk);
}
