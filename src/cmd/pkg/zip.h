// A package is a zip: §5.2 is parseZip's rules (web/fs.js) written down once,
// §5.1 the top-level dot-entry split (Package_Formats.md). unzip.h is the half
// that inflates; nothing here needs a syscall.
#pragma once

#include "kernel/result.h"
#include "kernel/span.h"
#include "kernel/str.h"
#include "kernel/string.h"
#include "kernel/sysabi.h"
#include "kernel/task.h"
#include "kernel/types.h"
#include "kernel/vec.h"

// ------------------------------------------------------------ §5.2, the zip

constexpr u32 ZIP_STORE   = 0;
constexpr u32 ZIP_DEFLATE = 8;

// Where a reader gets the archive's bytes. A zip is read back to front — the
// directory is at the end — so this is random access and not a stream. Keeping
// it abstract is what leaves zip.cpp with no syscall in it: /bin/unzip reads a
// descriptor, /bin/pkg and the unit suite read a buffer.
struct ZipSource {
    // The archive's length, known before anything is read.
    virtual u64 size() = 0;

    // Exactly out.size() bytes at `off`. Err(Invalid) past the end.
    virtual Task<Result<void>> read(u64 off, Span<u8> out) = 0;
};

// A ZipSource over bytes already in hand.
struct MemZipSource : ZipSource {
    explicit MemZipSource(Str bytes) : bytes_(bytes) {}

    u64 size() override { return bytes_.size(); }

    Task<Result<void>> read(u64 off, Span<u8> out) override;

private:
    Str bytes_;
};

struct ZipEntry {
    Str name;       // views the ZipDir this came from
    u64 at     = 0; // where the data begins, as the *local* header says
    u64 packed = 0; // its length there
    u64 size   = 0; // the declared uncompressed size: the inflate's ceiling
    u32 method = ZIP_STORE;
};

// The archive's directory, held together because a name views it.
struct ZipDir {
    String central;
    Vec<ZipEntry> entries;
};

enum class ZipRead {
    Ok,
    Malformed,   // not a zip this reader reads
    Unsupported, // zip64, an encrypted entry, or a method that is not 0 or 8
    NoMemory,
    Io, // the source would not answer
};

// The most an entry may be compressed to: Sys::Inflate stages its input, so a
// larger one cannot be read at all.
constexpr usize ZIP_PACKED_MAX = SYS_STAGE_MAX;

// The central directory, in its own order. A name ending in `/` is skipped.
// Two reads plus one small one per entry, whatever the archive weighs.
Task<ZipRead> zip_entries(ZipSource &src, ZipDir &out);

// An entry's bytes as the archive holds them, compressed or not.
Task<Result<String>> zip_packed(ZipSource &src, const ZipEntry &e);

// Method 0: the same bytes, once the declared size agrees.
Task<Result<String>> zip_stored(ZipSource &src, const ZipEntry &e);

// ---------------------------------------------------- §5.1, the dot-entries

// Only a name with no `/` can be metadata, so bin/.keep is payload.
enum class ZipMeta {
    Payload,
    PkgInfo,
    PreInstall,
    PostInstall,
    PreDeinstall,
    PostDeinstall,
    PreUpgrade,
    PostUpgrade,
    Trigger,
    Unknown, // a dot-entry that is none of these: the package is uninstallable
};

ZipMeta zip_meta(Str name);

// The entry name a kind is carried under, empty for Payload and Unknown.
Str zip_meta_name(ZipMeta meta);

// Whether the store directory keeps it: every dot-entry but .PKGINFO, which
// the §8.1 record supersedes. A kept entry is recorded like any payload file.
bool zip_meta_kept(ZipMeta meta);

// --------------------------------------------------------------- the ceiling

// An inflate's output side. Sys::Inflate caps its input and not its output, so
// the declared size is what stops a bomb. index.cpp bounds a fetched body with
// it too, and wants only the cap and not `complete`.
struct ZipSink {
    explicit ZipSink(u64 want) : want_(want) {}

    ZipSink(const ZipSink &)            = delete;
    ZipSink &operator=(const ZipSink &) = delete;

    // False when the chunk would pass the declared size, or would not fit.
    bool take(Str chunk);

    // True only when exactly the declared size arrived.
    bool complete() const { return out_.size() == want_; }

    String &text() { return out_; }

private:
    u64 want_;
    String out_;
};
