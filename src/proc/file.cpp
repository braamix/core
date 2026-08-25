#include "file.h"

#include "kernel/alloc.h"
#include "kernel/text.h"

namespace {

// The standard streams, built on first use. Pointers: nothing at namespace
// scope may have a destructor.
File *g_std[3] = { nullptr, nullptr, nullptr };

u32 mode_flags(FileMode m)
{
    switch (m) {
    case FileMode::Read:
        return SYS_O_READ;
    case FileMode::Write:
        return SYS_O_WRITE | SYS_O_CREATE | SYS_O_TRUNC;
    case FileMode::Append:
        return SYS_O_WRITE | SYS_O_CREATE | SYS_O_APPEND;
    case FileMode::Update:
        return SYS_O_READ | SYS_O_WRITE;
    }
    return SYS_O_READ;
}

// What proc_at_exit runs.
Task<void> flush_std()
{
    for (usize i = 1; i < 3; i++)
        if (File *f = g_std[i])
            if (Task<Result<void>> t = f->flush())
                co_await t;
}

File *build_std(usize slot, u8 *storage, u32 fd, FileMode m, Buffering how)
{
    File *f = new (storage) File(File::of(fd, m));
    f->set_buffering(how);
    g_std[slot] = f;
    return f;
}

} // namespace

// ------------------------------------------------------------- lifetime

File::File(File &&o)
    : buf_(o.buf_), chunk_(move(o.chunk_)), block_(o.block_), want_(o.want_), src_(o.src_),
      tie_(o.tie_), fd_(o.fd_), mode_(o.mode_), how_(o.how_), err_(o.err_), own_fd_(o.own_fd_),
      closed_(o.closed_), writing_(o.writing_)
{
    o.block_  = nullptr;
    o.src_    = nullptr;
    o.own_fd_ = false;
    o.closed_ = true;
    o.buf_.adopt(nullptr, 0);
}

File &File::operator=(File &&o)
{
    if (this == &o)
        return *this;

    if (block_)
        heap_free(block_);

    buf_     = o.buf_;
    chunk_   = move(o.chunk_);
    block_   = o.block_;
    want_    = o.want_;
    src_     = o.src_;
    tie_     = o.tie_;
    fd_      = o.fd_;
    mode_    = o.mode_;
    how_     = o.how_;
    err_     = o.err_;
    own_fd_  = o.own_fd_;
    closed_  = o.closed_;
    writing_ = o.writing_;

    o.block_  = nullptr;
    o.src_    = nullptr;
    o.own_fd_ = false;
    o.closed_ = true;
    o.buf_.adopt(nullptr, 0);
    return *this;
}

File::~File()
{
    if (block_)
        heap_free(block_);
}

Task<Result<File>> File::open(Str path, FileMode m)
{
    Task<Result<i32>> t = open_at(path, mode_flags(m));
    if (!t)
        co_return Err(Error::NoMemory);
    Result<i32> r = co_await t;
    if (r.is_err())
        co_return Err(r.error());

    File f;
    f.fd_     = u32(r.value());
    f.mode_   = m;
    f.own_fd_ = true;
    co_return move(f);
}

File File::of(u32 fd, FileMode m)
{
    File f;
    f.fd_   = fd;
    f.mode_ = m;
    return f;
}

File &File::stdin()
{
    alignas(File) static u8 storage[sizeof(File)];
    if (!g_std[0]) {
        File *f = build_std(0, storage, SYS_STDIN, FileMode::Read, Buffering::Full);
        f->tie_ = &File::stdout();
    }
    return *g_std[0];
}

File &File::stdout()
{
    alignas(File) static u8 storage[sizeof(File)];
    if (!g_std[1]) {
        build_std(1, storage, SYS_STDOUT, FileMode::Write, Buffering::Auto);
        proc_at_exit(flush_std);
    }
    return *g_std[1];
}

File &File::stderr()
{
    alignas(File) static u8 storage[sizeof(File)];
    if (!g_std[2]) {
        build_std(2, storage, SYS_STDERR, FileMode::Write, Buffering::None);
        proc_at_exit(flush_std);
    }
    return *g_std[2];
}

// --------------------------------------------------------------- state

void File::set_buffering(Buffering b)
{
    how_ = b;
}

Result<void> File::reserve(usize n)
{
    want_ = n < FILE_BUF ? FILE_BUF : n;
    if (src_ || how_ == Buffering::None)
        return {};
    return block_ready_() ? Result<void>{} : Result<void>(Err(Error::NoMemory));
}

Error File::fail_(Error e)
{
    if (clean())
        err_ = e;
    return e;
}

bool File::readable_() const
{
    return src_ || mode_ == FileMode::Read || mode_ == FileMode::Update;
}

bool File::writable_() const
{
    return !src_ && mode_ != FileMode::Read;
}

bool File::block_ready_()
{
    if (src_)
        return true;
    if (block_ && buf_.cap() >= want_)
        return true;

    char *n = static_cast<char *>(heap_alloc(want_));
    if (!n)
        return false;

    usize have = buf_.size();
    if (have)
        __builtin_memcpy(n, buf_.held().data(), have);
    if (block_)
        heap_free(block_);
    block_ = n;
    buf_.adopt(block_, want_, have);
    return true;
}

bool File::unget(char32_t c)
{
    if (writing_ || !block_ready_())
        return false;
    return buf_.unget(c);
}

// ---------------------------------------------------------- fast halves

bool File::take_(Result<char32_t> &out)
{
    if (!clean()) {
        out = Err(err_);
        return true;
    }
    if (writing_ || !buf_.ready())
        return false;

    char32_t ch = 0;
    if (buf_.take(ch) != RuneStep::Got)
        return false;
    out = ch;
    return true;
}

bool File::read_(Span<char> into, Result<usize> &out)
{
    if (!clean()) {
        out = Err(err_);
        return true;
    }
    if (writing_ || buf_.empty())
        return false;

    usize n = buf_.size() < into.size() ? buf_.size() : into.size();
    if (n == 0) {
        out = usize(0);
        return true;
    }
    __builtin_memcpy(into.data(), buf_.held().data(), n);
    buf_.consume(n);
    out = n;
    return true;
}

bool File::put_(char32_t c, Result<void> &out)
{
    if (!clean()) {
        out = Err(err_);
        return true;
    }
    if (!writing_ || !buf_.ready() || how_ == Buffering::None || how_ == Buffering::Auto)
        return false;
    if (how_ == Buffering::Line && c == '\n')
        return false;
    if (buf_.append_rune(c) == 0)
        return false;

    out = Result<void>{};
    return true;
}

bool File::write_(Str s, Result<void> &out)
{
    if (!clean()) {
        out = Err(err_);
        return true;
    }
    if (!writing_ || !buf_.ready() || how_ == Buffering::None || how_ == Buffering::Auto)
        return false;
    if (s.size() > buf_.room())
        return false;
    if (how_ == Buffering::Line && s.find('\n') != Str::npos)
        return false;

    buf_.append(s);
    out = Result<void>{};
    return true;
}

// ------------------------------------------------------------ the wire

Task<Result<usize>> File::read_into_(Span<char> into)
{
    usize max = into.size() > SYS_READ_MAX ? SYS_READ_MAX : into.size();

    u8 want[4];
    sys_put_u32(want, u32(max));
    Result<SysReply> r =
        co_await sys_call(Sys::Read, fd_, Str(reinterpret_cast<const char *>(want), sizeof(want)));
    if (r.is_err())
        co_return Err(r.error());

    Str d = r.value().data;
    if (d.empty())
        co_return Err(Error::Closed);

    usize n = d.size() < into.size() ? d.size() : into.size();
    __builtin_memcpy(into.data(), d.data(), n);
    co_return n;
}

Task<Result<usize>> File::fill_()
{
    // The tie: a prompt is out before what answers it is read.
    if (tie_ && tie_->writing_ && !tie_->buf_.empty())
        if (Task<Result<void>> t = tie_->flush())
            co_await t;

    if (src_) {
        // What a rune straddling two chunks left behind: three bytes at most.
        char carry[4];
        usize cn = buf_.size();
        if (cn > sizeof(carry))
            co_return Err(Error::Invalid);
        if (cn)
            __builtin_memcpy(carry, buf_.held().data(), cn);

        Task<Result<String>> t = src_->read();
        if (!t)
            co_return Err(Error::NoMemory);
        Result<String> r = co_await t;
        if (r.is_err())
            co_return Err(r.error());

        chunk_ = move(r.value());
        if (cn && !chunk_.insert(0, Str(carry, cn)))
            co_return Err(Error::NoMemory);
        if (chunk_.size() <= cn)
            co_return Err(Error::Closed);

        buf_.adopt(chunk_.data(), chunk_.size(), chunk_.size());
        co_return chunk_.size() - cn;
    }

    if (!block_ready_())
        co_return Err(Error::NoMemory);
    buf_.compact();
    if (buf_.room() == 0)
        co_return Err(Error::Invalid);

    Task<Result<usize>> t = read_into_({ buf_.tail(), buf_.room() });
    if (!t)
        co_return Err(Error::NoMemory);
    Result<usize> r = co_await t;
    if (r.is_err())
        co_return Err(r.error());

    buf_.filled(r.value());
    co_return r.value();
}

Task<Result<void>> File::drain_()
{
    while (!buf_.empty()) {
        Result<SysReply> r = co_await sys_call(Sys::Write, fd_, buf_.held());
        if (r.is_err())
            co_return Err(r.error());

        usize n = usize(r.value().status);
        if (n == 0)
            co_return Err(Error::Io);
        buf_.consume(n);
    }
    co_return {};
}

Task<void> File::probe_()
{
    how_ = Buffering::Full;
    if (Task<Result<TtyInfo>> t = tty_of(fd_)) {
        Result<TtyInfo> r = co_await t;
        if (r.is_ok() && r.value().console)
            how_ = Buffering::Line;
    }
}

Task<Result<void>> File::flush()
{
    if (!writing_ || buf_.empty())
        co_return {};

    Task<Result<void>> d = drain_();
    if (!d)
        co_return Err(fail_(Error::NoMemory));
    Result<void> r = co_await d;
    if (r.is_err())
        co_return Err(fail_(r.error()));
    co_return {};
}

Task<Result<void>> File::settle_(bool to_write)
{
    if (how_ == Buffering::Auto) {
        Task<void> p = probe_();
        if (p)
            co_await p;
        else
            how_ = Buffering::Line;
    }

    if (writing_ == to_write)
        co_return {};

    if (writing_) {
        Task<Result<void>> t = flush();
        if (!t)
            co_return Err(Error::NoMemory);
        Result<void> r = co_await t;
        if (r.is_err())
            co_return Err(r.error());
        writing_ = false;
        co_return {};
    }

    // Read to write: the read-ahead goes back to the descriptor.
    if (!buf_.empty()) {
        if (src_)
            co_return Err(Error::Unsupported);
        Task<Result<u64>> t = seek_fd(fd_, -i64(buf_.size()), SYS_SEEK_CUR);
        if (!t)
            co_return Err(Error::NoMemory);
        if ((co_await t).is_err())
            co_return Err(Error::Unsupported);
        buf_.reset();
    }
    writing_ = true;
    co_return {};
}

// ---------------------------------------------------------- slow halves

Task<void> File::get_slow_(Result<char32_t> *out)
{
    if (!readable_()) {
        *out = Err(fail_(Error::Invalid));
        co_return;
    }
    if (writing_) {
        Task<Result<void>> s = settle_(false);
        if (!s) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        Result<void> r = co_await s;
        if (r.is_err()) {
            *out = Err(fail_(r.error()));
            co_return;
        }
    }

    for (;;) {
        char32_t ch = 0;
        if (buf_.ready() && buf_.take(ch) == RuneStep::Got) {
            *out = ch;
            co_return;
        }

        Task<Result<usize>> t = fill_();
        if (!t) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        Result<usize> r = co_await t;
        if (r.is_err()) {
            // A sequence end of input cut short.
            if (r.error() == Error::Closed && buf_.ready() && !buf_.empty()) {
                *out = buf_.take_broken();
                co_return;
            }
            *out = Err(fail_(r.error()));
            co_return;
        }
    }
}

Task<void> File::read_slow_(Span<char> into, Result<usize> *out)
{
    if (!readable_()) {
        *out = Err(fail_(Error::Invalid));
        co_return;
    }
    if (writing_) {
        Task<Result<void>> s = settle_(false);
        if (!s) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        Result<void> r = co_await s;
        if (r.is_err()) {
            *out = Err(fail_(r.error()));
            co_return;
        }
    }
    if (into.empty()) {
        *out = usize(0);
        co_return;
    }

    // Nothing in hand, and a span bigger than the buffer: read into it.
    if (buf_.empty() && !src_ && (how_ == Buffering::None || into.size() >= want_)) {
        Task<Result<usize>> t = read_into_(into);
        if (!t) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        Result<usize> r = co_await t;
        *out            = r.is_err() ? Result<usize>(Err(fail_(r.error()))) : r;
        co_return;
    }

    if (buf_.empty()) {
        Task<Result<usize>> t = fill_();
        if (!t) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        Result<usize> r = co_await t;
        if (r.is_err()) {
            *out = Err(fail_(r.error()));
            co_return;
        }
    }

    usize n = buf_.size() < into.size() ? buf_.size() : into.size();
    __builtin_memcpy(into.data(), buf_.held().data(), n);
    buf_.consume(n);
    *out = n;
}

Task<void> File::put_slow_(char32_t c, Result<void> *out)
{
    if (!writable_()) {
        *out = Err(fail_(Error::Invalid));
        co_return;
    }
    Task<Result<void>> s = settle_(true);
    if (!s) {
        *out = Err(fail_(Error::NoMemory));
        co_return;
    }
    if (Result<void> r = co_await s; r.is_err()) {
        *out = Err(fail_(r.error()));
        co_return;
    }

    if (how_ == Buffering::None) {
        char e[4];
        usize n              = utf8_encode(c, e);
        Task<Result<void>> w = write_all(fd_, Str(e, n));
        if (!w) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        Result<void> r = co_await w;
        *out           = r.is_err() ? Result<void>(Err(fail_(r.error()))) : r;
        co_return;
    }

    if (!block_ready_()) {
        *out = Err(fail_(Error::NoMemory));
        co_return;
    }
    if (buf_.append_rune(c) == 0) {
        Task<Result<void>> f = flush();
        if (!f) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        if (Result<void> r = co_await f; r.is_err()) {
            *out = Err(r.error());
            co_return;
        }
        if (buf_.append_rune(c) == 0) {
            *out = Err(fail_(Error::Invalid));
            co_return;
        }
    }

    if (how_ == Buffering::Line && c == '\n') {
        Task<Result<void>> f = flush();
        if (!f) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        if (Result<void> r = co_await f; r.is_err()) {
            *out = Err(r.error());
            co_return;
        }
    }
    *out = Result<void>{};
}

Task<void> File::write_slow_(Str s, Result<void> *out)
{
    if (!writable_()) {
        *out = Err(fail_(Error::Invalid));
        co_return;
    }
    Task<Result<void>> st = settle_(true);
    if (!st) {
        *out = Err(fail_(Error::NoMemory));
        co_return;
    }
    if (Result<void> r = co_await st; r.is_err()) {
        *out = Err(fail_(r.error()));
        co_return;
    }

    // Longer than the buffer: through it rather than into it.
    if (how_ == Buffering::None || s.size() >= want_) {
        Task<Result<void>> f = flush();
        if (!f) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        if (Result<void> r = co_await f; r.is_err()) {
            *out = Err(r.error());
            co_return;
        }
        Task<Result<void>> w = write_all(fd_, s);
        if (!w) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        Result<void> r = co_await w;
        *out           = r.is_err() ? Result<void>(Err(fail_(r.error()))) : r;
        co_return;
    }

    if (!block_ready_()) {
        *out = Err(fail_(Error::NoMemory));
        co_return;
    }
    if (s.size() > buf_.room()) {
        Task<Result<void>> f = flush();
        if (!f) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        if (Result<void> r = co_await f; r.is_err()) {
            *out = Err(r.error());
            co_return;
        }
    }
    buf_.append(s);

    if (how_ == Buffering::Line && s.find('\n') != Str::npos) {
        Task<Result<void>> f = flush();
        if (!f) {
            *out = Err(fail_(Error::NoMemory));
            co_return;
        }
        if (Result<void> r = co_await f; r.is_err()) {
            *out = Err(r.error());
            co_return;
        }
    }
    *out = Result<void>{};
}

// ------------------------------------------------------------ the rest

Task<Result<bool>> File::getline(String &out, bool keep_nl)
{
    out.clear();
    if (!clean())
        co_return eof() ? Result<bool>(false) : Result<bool>(Err(err_));
    if (!readable_())
        co_return Err(fail_(Error::Invalid));

    usize seen = 0;
    for (;;) {
        if (buf_.ready() && !buf_.empty()) {
            usize was   = buf_.size();
            LineStep st = buf_.take_line(out, keep_nl);
            seen += was - buf_.size();
            if (st == LineStep::NoMemory)
                co_return Err(fail_(Error::NoMemory));
            if (st == LineStep::Done)
                co_return true;
        }

        Task<Result<usize>> t = fill_();
        if (!t)
            co_return Err(fail_(Error::NoMemory));
        Result<usize> r = co_await t;
        if (r.is_err()) {
            // A final fragment with no newline is a line.
            if (r.error() == Error::Closed) {
                fail_(Error::Closed);
                co_return seen != 0;
            }
            co_return Err(fail_(r.error()));
        }
    }
}

Task<Result<u64>> File::seek(i64 off, u32 whence)
{
    if (writing_) {
        Task<Result<void>> f = flush();
        if (!f)
            co_return Err(fail_(Error::NoMemory));
        if (Result<void> r = co_await f; r.is_err())
            co_return Err(r.error());
    } else if (whence == SYS_SEEK_CUR) {
        // The descriptor is as far ahead as the read-ahead in hand.
        off -= i64(buf_.size());
    }
    buf_.reset();
    writing_ = false;

    Task<Result<u64>> t = seek_fd(fd_, off, whence);
    if (!t)
        co_return Err(fail_(Error::NoMemory));
    Result<u64> r = co_await t;
    if (r.is_err())
        co_return Err(fail_(r.error()));

    if (eof())
        clear_err();
    co_return r.value();
}

Task<Result<void>> File::close()
{
    if (closed_)
        co_return {};
    closed_ = true;

    Result<void> res{};
    Task<Result<void>> f = flush();
    if (!f)
        res = Err(fail_(Error::NoMemory));
    else if (Result<void> r = co_await f; r.is_err())
        res = Err(r.error());

    if (own_fd_) {
        if (Task<void> c = close_fd(fd_))
            co_await c;
        own_fd_ = false;
    }
    buf_.reset();
    fail_(Error::Closed);
    co_return res;
}

Task<Result<u32>> File::detach()
{
    if (src_ || closed_)
        co_return Err(Error::Unsupported);

    if (writing_) {
        Task<Result<void>> f = flush();
        if (!f)
            co_return Err(fail_(Error::NoMemory));
        if (Result<void> r = co_await f; r.is_err())
            co_return Err(r.error());
    } else if (!buf_.empty()) {
        Task<Result<u64>> t = seek_fd(fd_, -i64(buf_.size()), SYS_SEEK_CUR);
        if (!t)
            co_return Err(fail_(Error::NoMemory));
        if ((co_await t).is_err())
            co_return Err(Error::Unsupported);
    }

    buf_.reset();
    closed_ = true;
    own_fd_ = false;
    fail_(Error::Closed);
    co_return fd_;
}

// -------------------------------------------------------------- awaiters

bool FileGet::await_ready()
{
    if (f->take_(v))
        return true;
    slow = f->get_slow_(&v);
    if (!slow) {
        v = Err(f->fail_(Error::NoMemory));
        return true;
    }
    return false;
}

bool FilePut::await_ready()
{
    if (f->put_(ch, v))
        return true;
    slow = f->put_slow_(ch, &v);
    if (!slow) {
        v = Err(f->fail_(Error::NoMemory));
        return true;
    }
    return false;
}

bool FileWrite::await_ready()
{
    if (f->write_(s, v))
        return true;
    slow = f->write_slow_(s, &v);
    if (!slow) {
        v = Err(f->fail_(Error::NoMemory));
        return true;
    }
    return false;
}

bool FileRead::await_ready()
{
    if (f->read_(into, v))
        return true;
    slow = f->read_slow_(into, &v);
    if (!slow) {
        v = Err(f->fail_(Error::NoMemory));
        return true;
    }
    return false;
}
