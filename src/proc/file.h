// A buffered stream over a descriptor, in place of stdio.h: a buffer, so a
// character is not a syscall; runes, so get() is a codepoint and not a byte;
// and a sticky error, checked once rather than per character.
//
// A buffered File owns its stream until close() or detach() — it reads ahead,
// past what the kernel's own pushback can see. A descriptor to be named in a
// spawn, or shared, wants Buffering::None.
//
// ~File does not flush: a destructor cannot co_await. Flushing is flush(),
// close(), or the exit hook stdout() and stderr() install.
#pragma once

#include "filebuf.h"
#include "io.h"
#include "kernel/text.h"

// What open() asks the filesystem for.
enum class FileMode {
    Read,   // SYS_O_READ
    Write,  // truncating, creating
    Append, // creating, positioned at the end
    Update, // read and write; a direction change costs a flush or a seek
};

enum class Buffering {
    None, // every operation is a syscall
    Line, // flushed when what was written holds a newline
    Full, // flushed when the buffer fills
    Auto, // Line on the console, Full otherwise; decided on the first flush
};

// The block a File takes when nothing asks for more: the allocator's top small
// size class.
constexpr usize FILE_BUF = 512;

struct File;

// The shared half of a buffered step. Either the buffer answered in
// await_ready, or this carries the slow path's frame, which the awaiter — and
// so the awaiting coroutine's frame — owns.
struct FileSlow {
    Task<void> slow;

    template <class P>
    std::coroutine_handle<> enter(std::coroutine_handle<P> caller) noexcept
    {
        auto h                   = slow.handle();
        h.promise().continuation = caller;
        h.promise().cancel       = caller.promise().cancel;
        return h;
    }
};

// One rune. Err(Closed) at end of input.
struct FileGet : FileSlow {
    File *f            = nullptr;
    Result<char32_t> v = Err(Error::Io);

    bool await_ready();

    template <class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> c) noexcept
    {
        return enter(c);
    }

    Result<char32_t> await_resume() const { return v; }
};

// One rune, encoded.
struct FilePut : FileSlow {
    File *f        = nullptr;
    char32_t ch    = 0;
    Result<void> v = Err(Error::Io);

    bool await_ready();

    template <class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> c) noexcept
    {
        return enter(c);
    }

    Result<void> await_resume() const { return v; }
};

// Bytes, not runes: a UTF-8 sequence may straddle two calls.
struct FileWrite : FileSlow {
    File *f = nullptr;
    Str s;
    Result<void> v = Err(Error::Io);

    bool await_ready();

    template <class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> c) noexcept
    {
        return enter(c);
    }

    Result<void> await_resume() const { return v; }
};

// As many bytes as are there, never more than the span. Err(Closed) at end of
// input, so a short read is never mistaken for one.
struct FileRead : FileSlow {
    File *f = nullptr;
    Span<char> into;
    Result<usize> v = Err(Error::Io);

    bool await_ready();

    template <class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> c) noexcept
    {
        return enter(c);
    }

    Result<usize> await_resume() const { return v; }
};

struct File {
    // Reads the concatenation an Input names. The Input must outlive the File.
    explicit File(Input &src) : src_(&src) {}

    File(File &&o);
    File &operator=(File &&o);
    File(const File &)            = delete;
    File &operator=(const File &) = delete;

    // Frees the buffer. It neither flushes nor closes.
    ~File();

    static Task<Result<File>> open(Str path, FileMode m = FileMode::Read);

    // Wraps a descriptor this File does not own and will not close.
    static File of(u32 fd, FileMode m);

    static File &stdin();  // fully buffered
    static File &stdout(); // Buffering::Auto
    static File &stderr(); // Buffering::None

    // ---- input ---------------------------------------------------------

    FileGet get() { return FileGet{ {}, this }; }

    // Puts a rune back, in front of what is buffered, so read() and getline()
    // see it too. False when there is no room in front.
    bool unget(char32_t c);

    FileRead read(Span<char> into) { return FileRead{ {}, this, into }; }

    // ok(true) with `out` set to the next line; ok(false) at end of input. A
    // final fragment with no newline is a line.
    Task<Result<bool>> getline(String &out, bool keep_nl = false);

    // ---- output --------------------------------------------------------

    FilePut put(char32_t c) { return FilePut{ {}, this, c }; }

    FileWrite write(Str s) { return FileWrite{ {}, this, s }; }

    Task<Result<void>> flush();

    // ---- state ---------------------------------------------------------

    // Discards the buffer.
    Task<Result<u64>> seek(i64 off, u32 whence);

    // Flushes, then closes if this File opened the descriptor.
    Task<Result<void>> close();

    // Gives the descriptor back. Unread bytes are wound off a seekable stream,
    // and are Err(Unsupported) on one that is not.
    Task<Result<u32>> detach();

    // The first error this stream met, or Error(0). Closed is an end of input.
    Error err() const { return err_; }
    bool clean() const { return u8(err_) == 0; }
    bool eof() const { return err_ == Error::Closed; }
    bool failed() const { return !clean() && !eof(); }
    void clear_err() { err_ = Error(0); }

    void set_buffering(Buffering b);

    // Asks for a larger block than FILE_BUF; SYS_READ_MAX is a span exactly.
    Result<void> reserve(usize n);

    u32 fd() const { return fd_; }

private:
    File() = default;

    friend struct FileGet;
    friend struct FilePut;
    friend struct FileWrite;
    friend struct FileRead;

    // True when the buffer alone answered.
    bool take_(Result<char32_t> &out);
    bool put_(char32_t c, Result<void> &out);
    bool write_(Str s, Result<void> &out);
    bool read_(Span<char> into, Result<usize> &out);

    Task<void> get_slow_(Result<char32_t> *out);
    Task<void> put_slow_(char32_t c, Result<void> *out);
    Task<void> write_slow_(Str s, Result<void> *out);
    Task<void> read_slow_(Span<char> into, Result<usize> *out);

    Task<Result<usize>> read_into_(Span<char> into);
    Task<Result<usize>> fill_();
    Task<Result<void>> drain_();
    Task<Result<void>> settle_(bool to_write);
    Task<void> probe_();

    bool block_ready_();
    bool readable_() const;
    bool writable_() const;
    Error fail_(Error e);

    FileBuf buf_;
    String chunk_;            // what an Input handed over; buf_ views it
    char *block_   = nullptr; // the block this File took
    usize want_    = FILE_BUF;
    Input *src_    = nullptr;
    File *tie_     = nullptr; // flushed before this one refills
    u32 fd_        = 0;
    FileMode mode_ = FileMode::Read;
    Buffering how_ = Buffering::Full;
    Error err_     = Error(0);
    bool own_fd_   = false;
    bool closed_   = false;
    bool writing_  = false; // the buffer holds output rather than input
};

// For a port that had getchar and putchar.
inline FileGet get_rune()
{
    return File::stdin().get();
}

inline FilePut put_rune(char32_t c)
{
    return File::stdout().put(c);
}

inline FileWrite write_out(Str s)
{
    return File::stdout().write(s);
}

inline FileWrite write_err(Str s)
{
    return File::stderr().write(s);
}
