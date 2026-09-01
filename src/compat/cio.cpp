// Group B's stream half: FILE over proc/file.h.
#define BRAAM_COMPAT_BUILDING 1

#include "cio.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cerr.h"
#include "cmode.h"
#include "kernel/alloc.h"

namespace {

// The three standard streams, built on first use. Pointers: nothing at
// namespace scope may have a destructor.
CompatFile *g_std[3];

FileMode fmode_of(u32 flags)
{
    if ((flags & SYS_O_READ) && (flags & SYS_O_WRITE))
        return FileMode::Update;
    if (flags & SYS_O_APPEND)
        return FileMode::Append;
    if (flags & SYS_O_WRITE)
        return FileMode::Write;
    return FileMode::Read;
}

CompatFile *build_std(usize slot, u8 *storage, File &f)
{
    g_std[slot] = new (storage) CompatFile(f);
    return g_std[slot];
}

// What the stream met, as C tells the two apart: an end of input is not a
// failure and never reaches errno.
int fail_stream(Error e)
{
    if (e == Error::Closed)
        return EOF;
    fail_with(e);
    return EOF;
}

// Formats into a heap block. Null with errno set on failure.
char *fmt_block(const char *fmt, va_list ap, usize *len)
{
    usize cap = FILE_BUF;
    char *b   = static_cast<char *>(heap_alloc(cap));
    if (!b) {
        errno = ENOMEM;
        return nullptr;
    }

    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(b, cap, fmt, ap);
    if (n < 0) {
        heap_free(b);
        va_end(ap2);
        errno = EINVAL;
        return nullptr;
    }
    if (usize(n) >= cap) {
        heap_free(b);
        cap = usize(n) + 1;
        b   = static_cast<char *>(heap_alloc(cap));
        if (!b) {
            va_end(ap2);
            errno = ENOMEM;
            return nullptr;
        }
        vsnprintf(b, cap, fmt, ap2);
    }
    va_end(ap2);
    *len = usize(n);
    return b;
}

Task<int> write_block(FILE *f, char *b, usize n)
{
    Result<void> r = co_await f->at->write(Str(b, n));
    heap_free(b);
    if (r.is_err())
        co_return fail_with(r.error());
    co_return int(n);
}

Task<int> ready(int v)
{
    co_return v;
}

// Flushes, and closes the descriptor this FILE opened. 0, or EOF.
Task<int> shut(CompatFile *f)
{
    int ret        = 0;
    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = f->at->close())
        r = co_await t;
    if (r.is_err()) {
        fail_with(r.error());
        ret = EOF;
    }
    if (f->owned) {
        if (Task<void> t = close_fd(f->at->fd()))
            co_await t;
    }
    f->owned = false;
    f->back  = EOF;
    co_return ret;
}

// Everything b_fread and b_fgets owe b_ungetc.
int take_back(CompatFile *cf)
{
    int c    = cf->back;
    cf->back = EOF;
    return c;
}

} // namespace

// ------------------------------------------------------------------ streams

FILE *b_stdin()
{
    alignas(CompatFile) static u8 storage[sizeof(CompatFile)];
    return g_std[0] ? g_std[0] : build_std(0, storage, File::stdin());
}

FILE *b_stdout()
{
    alignas(CompatFile) static u8 storage[sizeof(CompatFile)];
    return g_std[1] ? g_std[1] : build_std(1, storage, File::stdout());
}

FILE *b_stderr()
{
    alignas(CompatFile) static u8 storage[sizeof(CompatFile)];
    return g_std[2] ? g_std[2] : build_std(2, storage, File::stderr());
}

Task<FILE *> b_fopen(const char *path, const char *mode)
{
    if (!path || !mode) {
        errno = EINVAL;
        co_return nullptr;
    }
    u32 flags = fmode_flags(Str(mode, strlen(mode)));
    if (!flags) {
        errno = EINVAL;
        co_return nullptr;
    }

    Result<i32> r = Err(Error::NoMemory);
    if (Task<Result<i32>> t = open_at(Str(path, strlen(path)), flags))
        r = co_await t;
    if (r.is_err()) {
        fail_with(r.error());
        co_return nullptr;
    }

    CompatFile *cf = heap_new<CompatFile>(File::of(u32(r.value()), fmode_of(flags)));
    if (!cf) {
        if (Task<void> t = close_fd(u32(r.value())))
            co_await t;
        errno = ENOMEM;
        co_return nullptr;
    }
    cf->owned = true;
    co_return cf;
}

Task<FILE *> b_fdopen(int fd, const char *mode)
{
    if (fd < 0 || !mode) {
        errno = EINVAL;
        co_return nullptr;
    }
    u32 flags = fmode_flags(Str(mode, strlen(mode)));
    if (!flags) {
        errno = EINVAL;
        co_return nullptr;
    }

    CompatFile *cf = heap_new<CompatFile>(File::of(u32(fd), fmode_of(flags)));
    if (!cf) {
        errno = ENOMEM;
        co_return nullptr;
    }
    cf->owned = true;
    co_return cf;
}

// The same FILE, pointed at another file: a caller holding `stdout` still
// holds it. Closing first is C's, and happens whether or not the open does.
Task<FILE *> b_freopen(const char *path, const char *mode, FILE *f)
{
    if (!f || !path || !mode) {
        errno = EINVAL;
        co_return nullptr;
    }
    u32 flags = fmode_flags(Str(mode, strlen(mode)));
    if (!flags) {
        errno = EINVAL;
        co_return nullptr;
    }

    co_await shut(f);

    Result<i32> r = Err(Error::NoMemory);
    if (Task<Result<i32>> t = open_at(Str(path, strlen(path)), flags))
        r = co_await t;
    if (r.is_err()) {
        fail_with(r.error());
        co_return nullptr;
    }

    f->own   = File::of(u32(r.value()), fmode_of(flags));
    f->at    = &f->own;
    f->owned = true;
    f->back  = EOF;
    co_return f;
}

Task<int> b_fclose(FILE *f)
{
    if (!f) {
        errno = EBADF;
        co_return EOF;
    }

    int ret = co_await shut(f);
    // The three standard streams are storage this did not allocate.
    if (f != g_std[0] && f != g_std[1] && f != g_std[2])
        heap_delete(f);
    co_return ret;
}

Task<int> b_fflush(FILE *f)
{
    if (!f) {
        errno = EBADF;
        co_return EOF;
    }
    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = f->at->flush())
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error()) == 0 ? 0 : EOF;
    co_return 0;
}

bool BFgetc::await_ready()
{
    if (cf->back != EOF)
        return true;
    return FileRead::await_ready();
}

int BFgetc::await_resume() const
{
    if (cf->back != EOF)
        return take_back(cf);

    Result<usize> r = FileRead::await_resume();
    if (r.is_err())
        return fail_stream(r.error());
    if (r.value() == 0)
        return EOF;
    return int(u8(cf->one));
}

BFgetc b_fgetc(FILE *f)
{
    return BFgetc{ { {}, f->at, Span<char>(&f->one, 1) }, f };
}

int BFputc::await_resume() const
{
    Result<void> r = FileWrite::await_resume();
    if (r.is_err())
        return fail_stream(r.error());
    return int(u8(cf->one));
}

BFputc b_fputc(int c, FILE *f)
{
    f->one = char(c);
    return BFputc{ { {}, f->at, Str(&f->one, 1) }, f };
}

int b_ungetc(int c, FILE *f)
{
    if (!f || c == EOF || f->back != EOF)
        return EOF;
    f->back = int(u8(c));
    f->at->clear_err();
    return f->back;
}

Task<char *> b_fgets(char *s, int n, FILE *f)
{
    if (!s || n <= 0 || !f) {
        errno = EINVAL;
        co_return nullptr;
    }

    int at = 0;
    while (at < n - 1) {
        int c = co_await b_fgetc(f);
        if (c == EOF)
            break;
        s[at++] = char(c);
        if (c == '\n')
            break;
    }
    if (at == 0)
        co_return nullptr;
    s[at] = '\0';
    co_return s;
}

Task<int> b_fputs(const char *s, FILE *f)
{
    if (!s || !f) {
        errno = EINVAL;
        co_return EOF;
    }
    usize n        = strlen(s);
    Result<void> r = co_await f->at->write(Str(s, n));
    if (r.is_err())
        co_return fail_stream(r.error());
    co_return int(n);
}

Task<int> b_puts(const char *s)
{
    if (Task<int> t = b_fputs(s, b_stdout()); !t || co_await t == EOF)
        co_return EOF;
    if (int c = co_await b_fputc('\n', b_stdout()); c == EOF)
        co_return EOF;
    co_return 0;
}

Task<size_t> b_fread(void *p, size_t size, size_t n, FILE *f)
{
    if (!p || !f || size == 0 || n == 0)
        co_return 0;

    char *out  = static_cast<char *>(p);
    usize want = size * n;
    usize got  = 0;

    if (f->back != EOF)
        out[got++] = char(take_back(f));

    while (got < want) {
        Result<usize> r = co_await f->at->read(Span<char>(out + got, want - got));
        if (r.is_err()) {
            if (r.error() != Error::Closed)
                fail_with(r.error());
            break;
        }
        if (r.value() == 0)
            break;
        got += r.value();
    }
    co_return got / size;
}

Task<size_t> b_fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    if (!p || !f || size == 0 || n == 0)
        co_return 0;

    Result<void> r = co_await f->at->write(Str(static_cast<const char *>(p), size * n));
    if (r.is_err()) {
        fail_with(r.error());
        co_return 0;
    }
    co_return n;
}

Task<int> b_fseek(FILE *f, long off, int whence)
{
    if (!f) {
        errno = EBADF;
        co_return -1;
    }
    f->back = EOF;
    f->at->clear_err();

    Result<u64> r = Err(Error::NoMemory);
    if (Task<Result<u64>> t = f->at->seek(i64(off), u32(whence)))
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error());
    f->at->clear_err();
    co_return 0;
}

Task<long> b_ftell(FILE *f)
{
    if (!f) {
        errno = EBADF;
        co_return -1;
    }
    // File::seek answers the logical position, read-ahead subtracted; it costs
    // the buffer, and the next read refills from where this left the stream.
    Result<u64> r = Err(Error::NoMemory);
    if (Task<Result<u64>> t = f->at->seek(0, SEEK_CUR))
        r = co_await t;
    if (r.is_err())
        co_return fail_with(r.error());
    co_return long(r.value()) - (f->back != EOF ? 1 : 0);
}

Task<void> b_rewind(FILE *f)
{
    if (Task<int> t = b_fseek(f, 0, SEEK_SET))
        co_await t;
}

Task<int> b_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    usize n = 0;
    char *b = fmt_block(fmt, ap, &n);
    if (!b)
        return ready(-1);
    return write_block(f, b, n);
}

// Not a coroutine: a variadic function cannot be one, and the caller's
// arguments are gone by the time a suspended body would read them. The
// formatting is done here and the Task only writes.
Task<int> b_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Task<int> t = b_vfprintf(b_stdout(), fmt, ap);
    va_end(ap);
    return t;
}

Task<int> b_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    Task<int> t = b_vfprintf(f, fmt, ap);
    va_end(ap);
    return t;
}

Task<void> b_perror(const char *s)
{
    int e = errno;

    File *e2 = b_stderr()->at;
    if (s && *s) {
        co_await e2->write(Str(s, strlen(s)));
        co_await e2->write(": ");
    }
    const char *m = strerror(e);
    co_await e2->write(Str(m, strlen(m)));
    co_await e2->write("\n");
}

int b_feof(FILE *f)
{
    return f && f->back == EOF && f->at->eof();
}

int b_ferror(FILE *f)
{
    return f && f->at->failed();
}

void b_clearerr(FILE *f)
{
    if (f)
        f->at->clear_err();
}

int b_fileno(FILE *f)
{
    if (!f) {
        errno = EBADF;
        return -1;
    }
    return int(f->at->fd());
}

int b_setvbuf(FILE *f, int mode, size_t size)
{
    if (!f)
        return fail_with(Error::Invalid);

    switch (mode) {
    case _IONBF:
        f->at->set_buffering(Buffering::None);
        return 0;
    case _IOLBF:
        f->at->set_buffering(Buffering::Line);
        break;
    case _IOFBF:
        f->at->set_buffering(Buffering::Full);
        break;
    default:
        return fail_with(Error::Invalid);
    }
    if (size > FILE_BUF && f->at->reserve(size).is_err())
        return fail_with(Error::NoMemory);
    return 0;
}
