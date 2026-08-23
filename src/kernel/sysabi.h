// The kernel↔process wire (Concept.md §4.3), spoken by the dispatcher in
// src/user/syscall.cpp and by the process runtime in src/proc/.
//
// A process's imports are `sys` and `sys_async`, bound by the host to one pid.
// The kernel answers them through two exports of the same names, which take the
// pid the host injects, so a process cannot name another.
//
// Declarations first, routines at the end.
#pragma once

#include "str.h"
#include "types.h"

// =========================================================== declarations

// The wasm custom section every process binary carries: the whole of what
// `exec` needs before it can instantiate one. Five u32s, so the parser needs no
// alignment care.
struct ProcMeta {
    u32 magic;
    u32 abi;
    u32 flags;
    u32 initial_pages;
    u32 max_pages;
};

constexpr Str PROC_SECTION   = "braam";
constexpr u32 PROC_MAGIC     = 0x6d617262; // "bram"
constexpr u32 PROC_ABI       = 18;
constexpr u32 PROC_PAGE      = 65536;
constexpr u32 PROC_MAX_PAGES = 256; // 16 MB, the ceiling the kernel imposes

static_assert(u32(PROC_MAX_PAGES) < 65536, "the page counts no longer fit in one word");

// The other thing `exec` will instantiate: a text file whose first line names an
// interpreter (Concept.md §4.3). The interpreter is absolute and the rest of the
// line is one argument, unsplit. Nothing here is on the wire.
constexpr usize PROC_SHEBANG_MAX = 128; // the first line must end inside this

// The outcome of one _start or _resume, as the host reports it.
enum class ProcStep : u32 {
    Exited = 0,
    Suspended,
    Trapped,
};

// Syscalls. The synchronous half answers inside the export and never parks; the
// asynchronous half records a request whose reply reaches the process through
// _resume. The synchronous half is closed at four (Concept.md §4.3).
enum class Sys : u32 {
    // sys(op, a0, a1, a2) -> i32
    Exit = 1, // a0 = exit status
    GetPid,   // -> pid
    Now,      // -> milliseconds since boot
    Stage,    // a0 = bytes the host is about to copy in; -> kernel address, or 0

    // sys_async(op, token, ptr, len). The reply is an i32 status then any data;
    // the op word's upper bits are the operation's argument.
    //
    // Descriptors and the names they come from. A number indexes the calling
    // process's own table and means nothing in another.
    Write = 16, // arg = fd;             payload = the bytes;  status = bytes written
    Read,       // arg = fd;  payload = empty, or u32 max;  data = the chunk, empty at end
                //   `max` clamps to SYS_CHUNK and never grows a read. What a
                //   short read leaves is kept on the descriptor, so a stream
                //   loses nothing and the next read serves it first.
    Open,       // arg = SYS_O_* flags;  payload = the path;   status = the fd
    Close,      // arg = fd
    Stat,       // arg bit 0 = do not follow a final link;  payload = the path;
                //                                       data = u32 kind, u64 size, u64 mtime
    List,       // payload = the path;   data = u32 count, then that many entries
    MkDir,      // payload = the path
    Remove,     // arg bit 0 = recursive; payload = the path
    Touch,      // payload = the path;   an existing file's mtime moved to now

    // The working directory the operations above resolve against: this
    // process's own, inherited from whoever spawned it.
    Chdir, // arg bit 0 = set, else just report; payload = the path; data = the cwd

    // A second descriptor for one open thing. One handle behind both, so a
    // file's offset is shared and the two streams interleave.
    Dup, // arg = fd;  status = the new fd

    // Symbolic links. Every operation above that names a path follows one;
    // neither of these two does. The payload carries two paths.
    Symlink = 27, // payload = u32 target_len, the target, the link's own path
    ReadLink,     // payload = the path;  data = the target, unresolved

    // One name for another, in Symlink's two-path shape and following neither.
    // Err(Unsupported) means "not here — copy instead"; every other error is
    // real.
    Rename, // payload = u32 from_len, the old path, the new one

    // A descriptor's read/write position, lseek(2)'s three forms. Past the end
    // is not an error. Anything that is not a file is Err(Unsupported), and so
    // are 0, 1 and 2.
    Seek = 30, // arg = fd;  payload = u32 whence, i64 offset;  data = u64 position

    // Time. The timer queue is the kernel's.
    Sleep = 32, // payload = u32 milliseconds

    // Host services. What the kernel publishes as text under /proc is read with
    // Open and Read instead. A stream of bytes comes back as a descriptor, so
    // Read, Write and Close serve it; a killed process drops them with its
    // handle table.
    Clock = 48, // data = u64 epoch_ms, i32 tz_min
    Storage,    // data = u64 quota, u64 usage, u32 flags
    Fetch,      // payload = u32 url_len, the url, the spec
                //   status = the body's fd; data = u32 http status, the headers
    WsOpen,     // payload = the url; status = the socket's fd
    ClipRead,   // arg bit 0 = wait for a paste instead;  data = the text
    ClipWrite,  // payload = the text
    Pick,       // status = the set's fd;  data = u32 count, then a name each
    PickOpen,   // arg = the set's fd; payload = u32 index; status = the file's fd
    Fexport,    // payload = u32 name_len, the name, the bytes

    // Ed25519. Status 0 is a good signature, Err(Perm) a bad one and
    // Err(Unsupported) a browser without the algorithm.
    Verify, // payload = u32 key_len, u32 sig_len, the key, the signature, the bytes

    // Raw deflate. The input is capped at SYS_STAGE_MAX; the output is not.
    Inflate, // payload = the compressed bytes;  status = the fd

    // The terminal. Cells, never a byte stream (§2.3): a full-screen program
    // paints a grid of its own and blits what changed. Both claims are held by
    // the kernel, on the process's record.
    KeyClaim = 64, // arg bit 0 = take, else give back;  data = u32 cols, u32 rows
    KeyRead,       // data = u32 code, u32 mods, u32 cols, u32 rows
    ScreenEnter,   // arg bit 0 = the alternate screen, else back; data = cols, rows
    ScreenBlit,    // payload = u32 x, y, w, h, cursor_x, cursor_y, cursor_on, then w*h Cells
    ScreenClear,   // blank the shell's screen and home its cursor

    // The cursor of the scrolling screen, the one a prompt lives in. Get and
    // set in one operation, as KeyClaim and Chdir are. A set is refused while
    // another process holds the alternate screen.
    Cursor, // arg bit 0 = set;  payload = u32 x, y, on
            //   data = u32 x, y, on, cols, rows

    // The colours the next Write paints with. Sticky grid state, and refused
    // while another process holds the alternate screen.
    Style, // arg = fg | bg << 8 | attrs << 16 (sys_style_pack)

    // A line editor's whole repaint in one operation: the anchor, a run per
    // colour, the bytes, and where to leave the cursor. `scrolled` reports what
    // the write scrolled up. Cursor's rules.
    Echo, // arg = SYS_ECHO_SHOW | SYS_ECHO_FRESH | SYS_ECHO_END
          //   payload = u32 x, y, cur, runs, then runs * (u32 style, u32 len),
          //             then the runs' bytes end to end
          //   data    = u32 x, y, on, cols, rows, scrolled

    // Whether a descriptor is the terminal, and how big it is.
    Tty, // arg = fd;  data = u32 flags, u32 cols, u32 rows

    // Processes. A descriptor named in a Spawn is *moved*: the parent's slot is
    // closed and the child owns it, which closes a pipe's write end and keeps
    // one process holding one end (channel.h).
    Pipe = 80, // data = u32 read fd, u32 write fd
    Spawn,     // arg bit 0 = an env blob follows, else the child inherits mine
               //   payload = u32 fd0, fd1, fd2, the argv blob, then the env blob
               //   status = the child's pid
               //   an fd below SYS_FD_MIN shares the stream this process was given
    Wait,      // arg = a pid, or SYS_WAIT_ANY;  status = the child's status; data = u32 pid
    Kill,      // arg = the pid, which must be a child of the caller
               //   payload = u32 signal, or empty for SIG_KILL

    // Which process is in front of the console, and therefore what ^C reaches.
    // With nobody in front, ^C reaches whoever holds the raw keys as an ordinary
    // key. The caller must hold the raw-key claim or be what is in front.
    Fg, // arg = a child's pid, or 0 to take the console back

    // Which signals this process is told about rather than acted on: a bit set
    // is delivered, a bit clear runs the default action. Get and set together,
    // as KeyClaim and Chdir are. A bit outside SIG_CATCHABLE is Err(Invalid).
    // The mask is a payload: SIG_WINCH is bit 28 and the arg field is 24.
    SigAct = 85, // payload = u32 mask, or empty to ask;  data = u32, the mask before
};

// Signals (Concept.md §3.5), in Unix's numbers. A mask is 1u << n, so a number
// stays below 32.
constexpr u32 SIG_INT   = 2;  // ^C
constexpr u32 SIG_KILL  = 9;  // never delivered, never declined
constexpr u32 SIG_TERM  = 15; // what `kill` with no number sends
constexpr u32 SIG_CONT  = 18; // resumes a stopped process
constexpr u32 SIG_TSTP  = 20; // ^Z
constexpr u32 SIG_WINCH = 28; // the grid changed shape

constexpr u32 SIG_MAX = 32;

// What Sys::SigAct accepts. SIG_KILL and SIG_CONT are never declinable;
// SIG_TSTP joins the day something sends one.
constexpr u32 SIG_CATCHABLE = (1u << SIG_INT) | (1u << SIG_TERM) | (1u << SIG_WINCH);

// The header ScreenBlit's payload begins with, in u32s.
constexpr usize SYS_BLIT_HEAD = 7;

// The three descriptor words Sys::Spawn's payload begins with, in u32s.
constexpr usize SYS_SPAWN_HEAD = 3;

// Sys::Spawn's op-word argument: an env blob follows the argv one. Without it
// the child inherits the caller's environment.
constexpr u32 SYS_SPAWN_ENV = 1;

// The most an environment may be, and therefore what a descendant carries.
constexpr u32 SYS_ENV_MAX = 8192;

// The anchor, cursor offset and run count Sys::Echo's payload begins with, and
// the style and length of each run header after them, in u32s. Every header
// precedes every byte, so the whole shape is checkable in one pass.
constexpr usize SYS_ECHO_HEAD = 4;
constexpr usize SYS_ECHO_RUN  = 2;

// The most runs one Echo may carry. A prompt is four, and an unbounded count is
// a loop a hostile binary chooses.
constexpr u32 SYS_ECHO_RUNS_MAX = 8;

// Sys::Echo's op-word argument. FRESH anchors wherever the cursor is, on a row
// of its own; END leaves the cursor where the write ended rather than `cur`
// cells past the anchor.
constexpr u32 SYS_ECHO_SHOW  = 1;
constexpr u32 SYS_ECHO_FRESH = 2;
constexpr u32 SYS_ECHO_END   = 4;

// The style of a Sys::Echo run that names no colour: the sticky one stands.
// Outside sys_style_pack's range, whose top byte is zero.
constexpr u32 SYS_STYLE_KEEP = 0xffffffff;
static_assert(SYS_STYLE_KEEP > 0x00ffffff, "SYS_STYLE_KEEP collides with sys_style_pack");

// Sys::Wait's "whichever finishes first". Zero is never a pid.
constexpr u32 SYS_WAIT_ANY = 0;

// The largest pid there is. Above it are the scheduler's anonymous jobs, which
// this wire cannot name.
constexpr u32 SYS_PID_MAX = 999999;

// The op word's argument is 24 bits, and a pid must survive it whole.
static_assert(SYS_PID_MAX < (1u << 24), "a pid must fit the op word's argument");

// How many children a process may have at once, and how deep a chain of them
// may go.
constexpr usize SYS_CHILD_MAX = 16;
constexpr u32 SYS_PROC_DEPTH  = 16;

// The most a blit may carry, which is the largest grid there can be. Sys::Stage
// is capped at the same number, and so is what Sys::Verify can check.
constexpr u32 SYS_STAGE_MAX = 1u << 20;

// Ed25519's fixed sizes, in bytes.
constexpr u32 SYS_ED25519_KEY = 32;
constexpr u32 SYS_ED25519_SIG = 64;

// One entry of Sys::List's reply: u32 kind, u64 size, u64 mtime, u32 name_len,
// the name. `mtime` is milliseconds since the epoch, 0 when the filesystem keeps
// none. Restated rather than shared with fs.h, for SYS_O_*'s reason.
constexpr u32 SYS_KIND_FILE = 0;
constexpr u32 SYS_KIND_DIR  = 1;
constexpr u32 SYS_KIND_LINK = 2;

// A listing names what is in the directory, so a link reports itself and never
// what it points at; a tree walk descends on SYS_KIND_DIR alone.

// Sys::Stat's argument: report the link itself rather than what it points at.
constexpr u32 SYS_STAT_NOFOLLOW = 1;

// The flags word Sys::Storage answers with (Concept.md §5.3).
enum : u32 {
    SYS_STORE_OPFS      = 1,
    SYS_STORE_SYNC      = 2,
    SYS_STORE_PERSISTED = 4,
    SYS_STORE_KNOWN     = 8, // the host answered at all
};

// The flags word Sys::Tty answers with.
enum : u32 {
    SYS_TTY_CONSOLE = 1, // this descriptor is the cell grid
};

// The three stdio descriptors are the stage's Stdio; anything above indexes
// the open-file table the process record owns.
constexpr u32 SYS_STDIN  = 0;
constexpr u32 SYS_STDOUT = 1;
constexpr u32 SYS_STDERR = 2;
constexpr u32 SYS_FD_MIN = 3;

// Open flags, restated rather than shared with src/fs/fs.h: a process cannot see
// the VFS, and the numbers a binary compiled today speaks must not move because
// the filesystem's did. exec maps them.
constexpr u32 SYS_O_READ   = 1;
constexpr u32 SYS_O_WRITE  = 2;
constexpr u32 SYS_O_CREATE = 4;
constexpr u32 SYS_O_TRUNC  = 8;
constexpr u32 SYS_O_APPEND = 16;

// Sys::Seek's whence, Unix's numbers, restated for SYS_O_*'s reason.
constexpr u32 SYS_SEEK_SET = 0;
constexpr u32 SYS_SEEK_CUR = 1;
constexpr u32 SYS_SEEK_END = 2;

// The largest position there is: the wire's offset is a signed 64-bit.
constexpr u64 SYS_SEEK_MAX = (u64(1) << 63) - 1;

// Sys::Seek's payload, in u32s: the whence, then the offset low word and high.
constexpr usize SYS_SEEK_WORDS = 3;

// What one read yields when the caller names no length.
constexpr u32 SYS_CHUNK = 512;

// The most one may name. The reply is a four-byte status then the bytes, so
// this lands a full read on exactly one span (Concept.md §8.2).
constexpr u32 SYS_READ_MAX = 65536 - 4;

static_assert(SYS_READ_MAX + 4 <= 65536, "a full read no longer fits one span");
static_assert(SYS_CHUNK <= SYS_READ_MAX, "the default read is above the ceiling");

// `test -x` decides from one chunk (src/cmd/sh/condrun.cpp).
static_assert(PROC_SHEBANG_MAX <= SYS_CHUNK, "test -x could no longer see a whole #! line");

// Where a bare command name is looked for when the environment names nowhere.
// /bin first; /pkg/bin is a symbolic link to the live generation.
constexpr Str SYS_PATH_DEFAULT = "/bin:/pkg/bin";

// argv is one blob rather than a pointer array, since it crosses an address
// space: u32 argc, then u32 length and bytes per word. The host copies it into
// the process with _alloc and passes it to _start.
//
// The environment is a word list in the same encoding, its words NAME=value. The
// two sit back to back — in _start's payload, and after Sys::Spawn's three
// descriptor words — and argv_bytes finds the join.

// =============================================================== routines

// ------------------------------------------------------- the process image

// `interp` and `arg` view `image`, and are written only on true.
inline bool exec_shebang(Str image, Str &interp, Str &arg)
{
    if (!image.starts_with("#!"))
        return false;

    // Space and tab alone; a newline ends the line rather than separating words.
    auto blank = [](char c) { return c == ' ' || c == '\t'; };

    // A line running past the cap is not a first line.
    usize cap = image.size() < PROC_SHEBANG_MAX ? image.size() : PROC_SHEBANG_MAX;
    usize eol = 2;
    while (eol < cap && image[eol] != '\n')
        eol++;
    if (eol == cap && cap < image.size())
        return false;

    Str line = image.substr(2, eol - 2);
    usize at = 0, end = line.size();
    while (end > at && (blank(line[end - 1]) || line[end - 1] == '\r'))
        end--;
    while (at < end && blank(line[at]))
        at++;
    if (at == end || line[at] != '/')
        return false;

    usize name = at;
    while (at < end && !blank(line[at]))
        at++;
    interp = line.substr(name, at - name);
    while (at < end && blank(line[at]))
        at++;
    arg = line.substr(at, end - at);
    return true;
}

// What a spawn request's `flags` word carries: the two page counts the host
// needs before it can make a Memory. One word, since `aux` is the pid.
inline u32 proc_pack(const ProcMeta &m)
{
    return m.initial_pages | (m.max_pages << 16);
}

inline u32 proc_initial(u32 flags)
{
    return flags & 0xffff;
}

inline u32 proc_max(u32 flags)
{
    return flags >> 16;
}

// ------------------------------------------------------------------ signals

inline u32 sig_bit(u32 sig)
{
    return sig < SIG_MAX ? 1u << sig : 0;
}

// The status of a process a signal it did not catch killed.
inline i32 sig_status(u32 sig)
{
    return i32(128 + sig);
}

// ----------------------------------------------------------------- the op word

inline u32 sys_op(Sys op, u32 arg = 0)
{
    return u32(op) | (arg << 8);
}

inline Sys sys_op_code(u32 op)
{
    return Sys(op & 0xff);
}

inline u32 sys_op_arg(u32 op)
{
    return op >> 8;
}

// The same field, named for what it holds at the descriptor operations.
inline u32 sys_op_fd(u32 op)
{
    return sys_op_arg(op);
}

// Sys::Style's argument: two palette indices and the ATTR_* bits of screen.h,
// which fit the op word's 24, so the operation carries no payload.
inline u32 sys_style_pack(u8 fg, u8 bg, u8 attrs)
{
    return u32(fg) | (u32(bg) << 8) | (u32(attrs) << 16);
}

inline u8 sys_style_fg(u32 arg)
{
    return u8(arg);
}

inline u8 sys_style_bg(u32 arg)
{
    return u8(arg >> 8);
}

inline u8 sys_style_attrs(u32 arg)
{
    return u8(arg >> 16);
}

// ------------------------------------------------------------- byte helpers

inline u32 sys_get_u32(const u8 *p)
{
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

inline void sys_put_u32(u8 *p, u32 v)
{
    p[0] = u8(v);
    p[1] = u8(v >> 8);
    p[2] = u8(v >> 16);
    p[3] = u8(v >> 24);
}

// --------------------------------------------------------------- Sys::Read

// How much one read may take. An absent or zero length is SYS_CHUNK, and no
// length grows one past SYS_READ_MAX.
inline u32 sys_read_want(const u8 *payload, usize len)
{
    if (len < 4)
        return SYS_CHUNK;
    u32 max = sys_get_u32(payload);
    if (max == 0)
        return SYS_CHUNK;
    return max > SYS_READ_MAX ? SYS_READ_MAX : max;
}

// --------------------------------------------------------------- Sys::Seek

// Where a seek lands. False on an unknown whence, a result before the start, or
// one past SYS_SEEK_MAX; past the end is neither, and reads as an end of input.
inline bool sys_seek_to(u64 cur, u64 end, u32 whence, i64 off, u64 &out)
{
    u64 base;
    if (whence == SYS_SEEK_SET)
        base = 0;
    else if (whence == SYS_SEEK_CUR)
        base = cur;
    else if (whence == SYS_SEEK_END)
        base = end;
    else
        return false;

    // Negated in u64: -INT64_MIN does not fit an i64.
    if (off < 0) {
        u64 back = u64(-(off + 1)) + 1;
        if (back > base)
            return false;
        out = base - back;
        return true;
    }
    if (u64(off) > SYS_SEEK_MAX - base)
        return false;
    out = base + u64(off);
    return true;
}

inline void sys_seek_put(u8 *p, u32 whence, i64 off)
{
    sys_put_u32(p, whence);
    sys_put_u32(p + 4, u32(u64(off)));
    sys_put_u32(p + 8, u32(u64(off) >> 32));
}

inline void sys_seek_get(const u8 *p, u32 &whence, i64 &off)
{
    whence = sys_get_u32(p);
    off    = i64(u64(sys_get_u32(p + 4)) | (u64(sys_get_u32(p + 8)) << 32));
}

// ------------------------------------------------------- the argv and the env

inline usize argv_size(const Str *v, usize n)
{
    usize total = 4;
    for (usize i = 0; i < n; i++)
        total += 4 + v[i].size();
    return total;
}

inline void argv_encode(const Str *v, usize n, u8 *out)
{
    sys_put_u32(out, u32(n));
    usize at = 4;
    for (usize i = 0; i < n; i++) {
        sys_put_u32(out + at, u32(v[i].size()));
        at += 4;
        for (usize k = 0; k < v[i].size(); k++)
            out[at + k] = u8(v[i][k]);
        at += v[i].size();
    }
}

inline usize argv_count(const u8 *p, usize len)
{
    return len < 4 ? 0 : sys_get_u32(p);
}

// The bytes one blob occupies, so a second may follow it. 0 is malformed: the
// smallest well-formed blob is the four of an empty word list.
inline usize argv_bytes(const u8 *p, usize len)
{
    if (len < 4)
        return 0;
    usize n  = sys_get_u32(p);
    usize at = 4;
    for (usize k = 0; k < n; k++) {
        if (at + 4 > len)
            return 0;
        usize size = sys_get_u32(p + at);
        at += 4;
        if (at + size > len)
            return 0;
        at += size;
    }
    return at;
}

// The i'th word, or an empty Str if the blob is short.
inline Str argv_at(const u8 *p, usize len, usize i)
{
    usize n = argv_count(p, len);
    if (i >= n)
        return Str();
    usize at = 4;
    for (usize k = 0; k < n; k++) {
        if (at + 4 > len)
            return Str();
        usize size = sys_get_u32(p + at);
        at += 4;
        if (at + size > len)
            return Str();
        if (k == i)
            return Str(reinterpret_cast<const char *>(p + at), size);
        at += size;
    }
    return Str();
}

// The value `name` has in an env blob, whose words are NAME=value. False leaves
// `out` alone: absent and empty are different answers.
inline bool env_value(const u8 *p, usize len, Str name, Str &out)
{
    usize n = argv_count(p, len);
    for (usize i = 0; i < n; i++) {
        Str w    = argv_at(p, len, i);
        usize eq = w.find('=');
        if (eq == Str::npos || w.substr(0, eq) != name)
            continue;
        out = w.substr(eq + 1);
        return true;
    }
    return false;
}

// The next directory of a PATH value, cut off `rest`. An empty component is
// skipped rather than meaning the current directory. False when none are left.
inline bool env_path_next(Str &rest, Str &dir)
{
    while (!rest.empty()) {
        dir = rest.split(':', rest);
        if (!dir.empty())
            return true;
    }
    return false;
}
