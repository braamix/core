// The half of the zip reader that needs a syscall, kept out of zip.cpp so that
// one compiles into tests.wasm.
#pragma once

#include "kernel/result.h"
#include "kernel/string.h"
#include "kernel/task.h"
#include "zip.h"

// A ZipSource over an open descriptor: a seek and a read per request, so the
// archive is never held whole.
struct FdZipSource : ZipSource {
    FdZipSource(u32 fd, u64 bytes) : fd_(fd), size_(bytes) {}

    u64 size() override { return size_; }

    Task<Result<void>> read(u64 off, Span<u8> out) override;

private:
    u32 fd_;
    u64 size_;
};

// An entry's bytes: stored ones straight out of the archive, deflated ones
// through Sys::Inflate, stopping at the declared size. An error from the
// operation or from any later read is fatal, and neither is a short read.
Task<Result<String>> zip_read(ZipSource &src, const ZipEntry &e);
