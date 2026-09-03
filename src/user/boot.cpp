#include "boot.h"

#include "console.h"
#include "exec.h"
#include "fs/devfs.h"
#include "fs/hostfs.h"
#include "fs/opfsfs.h"
#include "fs/vfs.h"
#include "io.h"
#include "kernel/alloc.h"
#include "kernel/fmt.h"
#include "kernel/key.h"
#include "kernel/sched.h"
#include "kernel/screen.h"
#include "kernel/traits.h"
#include "kernel/version.h"
#include "procfs.h"
#include "svc/svc.h"
#include "tty.h"

namespace {

// Deaths in quick succession before init stops replacing the shell, and how
// long one has to last to count as having got going.
constexpr u32 RESPAWN_TRIES    = 3;
constexpr f64 RESPAWN_FLOOR_MS = 1000;

// The host's description of itself, asked for once. A pointer rather than a
// String, because a namespace-scope global must be trivially destructible.
String *g_host = nullptr;

// The program init runs on terminal 0, read once out of INIT. A pointer for
// the reason g_host is one. Null is an archive that named none, which is SHELL.
String *g_init = nullptr;

// On a row of its own: a shell that died left its prompt on this one.
void say(Term &term, Str s)
{
    if (screen(term).cursor_x != 0)
        screen_newline(term);
    screen_write(term, s);
    screen_newline(term);
}

// The version line, which main.cpp writes on terminal 0 before this task runs.
// A terminal the host made later gets it here instead, without the uptime.
void banner(Term &term)
{
    Buf<64> line;
    line.put("braam ").put(BRAAM_VERSION);
    say(term, line.str());
}

// Yes or no on the grid, before there is a shell to ask with. The keyboard's
// receiver is the console pump, which init spawns before this task (main.cpp),
// so a claim through it is how anything reads a key — and nothing else holds
// one at boot. Raw keys are not echoed, so the answer is written here.
//
// Anything but y is no, ^C included: it arrives as an ordinary key, since
// nothing has armed a foreground.
Task<bool> ask(Term &term, Str question, const u32 &pid)
{
    Buf<96> line;
    line.put(question).put(" [y/N] ");
    if (screen(term).cursor_x != 0)
        screen_newline(term);
    // Bright white for the question, back to the default for the answer.
    screen_style(term, COLOR_WHITE | COLOR_BRIGHT, COLOR_BLACK, 0);
    screen_write(term, line.str());
    screen_style(term, COLOR_WHITE, COLOR_BLACK, 0);

    KeyInput keys(term, pid);
    if (!keys.ok()) {
        screen_write(term, "n\n");
        co_return false;
    }

    for (;;) {
        Result<Key> k = co_await keys.next();
        if (k.is_err()) {
            if (k.error() == Error::Again)
                continue;
            screen_write(term, "n\n");
            co_return false;
        }
        bool yes = k.value().code == 'y' || k.value().code == 'Y';
        if (!yes && k.value().code != 'n' && k.value().code != 'N' && k.value().code != KEY_ENTER &&
            !(k.value().mods & MOD_CTRL))
            continue;
        screen_write(term, yes ? "y\n" : "n\n");
        co_return yes;
    }
}

// The stored image against this kernel's. Absent means an empty store, which
// is unpacked without asking — there is nothing there to overwrite and no
// prompt anyone could answer usefully. A mismatch is the user's call: the old
// /bin may be what they want kept, and a stale one says so for itself when
// exec_resolve refuses it.
//
// Returns the count rather than printing it: whether boot's own news is wanted
// is not known until /etc/init has been read, which needs this to have run. An
// error or a question is the other kind, and goes out either way.
Task<u32> unpack_if_stale(Term &term, const u32 &pid)
{
    String stored;
    if (Task<Result<String>> t = read_file(VERSION_PATH)) {
        Result<String> r = co_await t;
        if (r.is_ok())
            stored = move(r.value());
    }

    if (stored.str() == BRAAM_VERSION)
        co_return 0;

    if (!stored.empty()) {
        Buf<128> line;
        line.put("the stored image is ")
            .put(stored.str())
            .put(" and this kernel is ")
            .put(BRAAM_VERSION);
        say(term, line.str());
        bool yes = false;
        if (Task<bool> t = ask(term, "replace /bin and /etc?", pid))
            yes = co_await t;
        if (!yes) {
            say(term, "keeping the stored image");
            co_return 0;
        }
    }

    Result<u32> r = Err(Error::NoMemory);
    if (Task<Result<u32>> t = storage_unpack(BRAAM_VERSION))
        r = co_await t;
    if (r.is_err()) {
        say(term, "the root archive would not unpack — /bin is empty and there is no shell");
        co_return 0;
    }

    co_return r.value();
}

// The directories the store is expected to have and the archive does not
// carry. Exists is the ordinary answer on every boot but the first.
Task<void> make_dirs(Term &term)
{
    // Where `fimport` puts what the file picker hands over (Concept.md §5.4):
    // /import, at the root.
    // A directory like any other: the files arrive as bytes, and nothing about
    // them is a filesystem.
    constexpr Str DIRS[] = { "/home", "/import" };
    for (Str d : DIRS) {
        if (Task<Result<void>> t = vfs_mkdir(d)) {
            Result<void> r = co_await t;
            if (r.is_err() && r.error() != Error::Exists) {
                Buf<96> line;
                line.put(d).put(": ").put(error_name(r.error()));
                say(term, line.str());
            }
        }
    }
}

// /tmp is temporary against the store's persistence, not the tab's: OPFS keeps
// what is written to it, so what makes a temporary directory temporary now is
// emptying it here.
Task<void> wipe_tmp()
{
    if (Task<Result<void>> t = vfs_remove("/tmp", true))
        co_await t;
    if (Task<Result<void>> t = vfs_mkdir("/tmp"))
        co_await t;
}

// The greeting, on the grid the prompt is about to appear in. A coroutine of
// its own rather than four lines of init_task, for the reason boot_filesystem
// is one: it holds the whole file, and init's frame is on the boot path.
//
// Silent when there is no motd. A boot archive without a greeting is not a
// broken one, and the line saying so would itself be noise at every boot.
Task<void> show_motd(Term &term)
{
    Task<Result<String>> t = read_file(MOTD);
    if (!t)
        co_return;
    Result<String> r = co_await t;
    if (r.is_err() || r.value().empty())
        co_return;

    // Green, and back to the default afterwards: the style is sticky grid
    // state, so what the prompt writes next would inherit it. The defaults are
    // named here rather than saved, because nothing has changed them — this is
    // the kernel's only use of colour, and boot is the only writer at boot.
    //
    // screen_write turns the file's own newlines into rows (screen.cpp), so
    // the only thing left to arrange is that the prompt starts on a line of
    // its own — which a file not ending in a newline would take away.
    screen_style(term, COLOR_GREEN, COLOR_BLACK, 0);
    screen_write(term, r.value().str());
    if (r.value().str()[r.value().size() - 1] != '\n')
        screen_newline(term);
    screen_style(term, COLOR_WHITE, COLOR_BLACK, 0);
}

// INIT's first line, trimmed. Read once, on the boot path. Missing, empty or
// unreadable all leave g_init null, which is SHELL. The shape of the path is
// exec_resolve's business, not this one's.
Task<void> learn_init_prog()
{
    // Cleared first, so a second boot in one instance answers the store it has
    // now rather than the one it had.
    heap_delete(g_init);
    g_init = nullptr;

    Task<Result<String>> t = read_file(INIT);
    if (!t)
        co_return;
    Result<String> r = co_await t;
    if (r.is_err())
        co_return;

    String line = move(r.value());
    usize nl    = line.str().find('\n');
    if (nl != Str::npos)
        line.truncate(nl);
    while (!line.empty() && (line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t' ||
                             line[line.size() - 1] == '\r'))
        line.pop();
    usize at = 0;
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
        at++;
    line.erase(0, at);
    if (line.empty())
        co_return;

    if (String *held = heap_new<String>(move(line)))
        g_init = held;
}

// What to call it on the grid: "the shell" for SHELL, which is what every line
// about it has said and what the system suite reads back; a path otherwise.
Str called(Str prog)
{
    return prog == SHELL ? Str("the shell") : prog;
}

// The half of the description above the blank line: the fields short enough to
// put on the boot grid. The rest — the locale and the agent string, which wraps
// a row on its own — waits in /proc/host for anyone who asks.
Str banner_half(Str s)
{
    for (usize i = 0; i + 1 < s.size(); i++)
        if (s[i] == '\n' && s[i + 1] == '\n')
            return s.substr(0, i + 1);
    return s;
}

// Where the value starts on a banner row, which is where the table the host
// and procfs.cpp write puts it too.
constexpr usize LABEL_COL = 9;

// One `name value` row of the description, written as `name: value`. The colon
// is presentation and lives only here: /proc/host stays a plain table, which is
// what lets `uname` read a field out of it with next_field.
void write_labelled(Term &term, Str line)
{
    usize name = 0;
    while (name < line.size() && line[name] != ' ')
        name++;
    usize value = name;
    while (value < line.size() && line[value] == ' ')
        value++;

    Buf<160> out;
    out.put(line.substr(0, name)).put(':');

    // At least one space, so a name that fills the column still separates.
    usize col = name + 1;
    do {
        out.put(' ');
        col++;
    } while (col < LABEL_COL);
    out.put(line.substr(value));

    screen_write(term, out.str());
    screen_newline(term);
}

// What the system is running on, asked once. It is here rather than in main.cpp
// because that export cannot await, and asking the host is the only way to know
// any of this. Cached whether or not it is printed: /proc/host answers out of
// g_host, and `uname` out of /proc/host.
//
// An answer that does not arrive is not an error: the store is what boot cannot
// do without, and this is the motd's kind of line, not the OPFS one.
Task<void> learn_host()
{
    if (Task<Result<String>> t = host_info()) {
        Result<String> r = co_await t;
        if (r.is_ok() && !r.value().empty()) {
            if (String *held = heap_new<String>(move(r.value()))) {
                heap_delete(g_host);
                g_host = held;
            }
        }
    }
}

// Those facts on the grid, under the version line main.cpp already wrote.
void show_host(Term &term, const StorageBackend &b)
{
    Str rest = banner_half(host_facts());
    while (!rest.empty()) {
        usize nl = rest.find('\n');
        Str line = nl == Str::npos ? rest : rest.substr(0, nl);
        rest     = nl == Str::npos ? Str() : rest.substr(nl + 1);
        if (!line.empty())
            write_labelled(term, line);
    }

    // The geometry is not here: it is on the screen being reported, and
    // /proc/host answers it live for anyone who wants the number.
    //
    // How much room there is, and not how much is left: the usage is the half
    // that moves, and a figure that was true at boot is the wrong one to leave
    // on the screen all session. `df` is where to ask.
    //
    // The durability is a property of the store, not of a mount, so it is here
    // rather than in df's table — as boot read it, a late persist() answer
    // correcting the store and not this line (§5.3).
    Buf<96> line;
    line.put("store:   OPFS, ");
    if (b.quota)
        line.put(u64(b.quota / 1000000)).put(" MB");
    else
        line.put("quota unknown");
    line.put(b.persisted ? Str(", persistent") : Str(", best-effort"));
    say(term, line.str());
}

// Why the program init runs would not resolve, on one line. Three different
// repairs hide behind one Result: a store without the program in it, one built
// against another kernel, and one whose bytes are damaged.
void no_program(Term &term, Str prog, Error e)
{
    Buf<160> line;
    line.put(prog).put(": ");
    switch (e) {
    case Error::NotFound:
        line.put("not in the store");
        break;
    case Error::Unsupported:
        line.put("built for another process ABI — this kernel speaks ")
            .put(u32(PROC_ABI))
            .put(", so the stored image is stale");
        break;
    case Error::Invalid:
        line.put("not a program — no braam section, or a damaged image");
        break;
    case Error::NoMemory:
        line.put("out of memory reading the image");
        break;
    default:
        line.put(error_name(e));
        break;
    }
    say(term, line.str());
}

} // namespace

Str host_facts()
{
    return g_host ? g_host->str() : Str();
}

Task<bool> boot_filesystem(Term &term, const u32 &pid)
{
    // Idempotent. A second boot in a test finds the mounts already there and
    // leaves them, rather than reporting every one of them as an error — but
    // still reads INIT, which is the store's to answer and may have changed.
    if (!vfs_mounts().empty()) {
        if (Task<void> t = learn_init_prog())
            co_await t;
        co_return true;
    }

    // Concept.md §5.2: OPFS is absent in Safari private browsing, and the sync
    // access handles the fast path needs exist only in a worker. There is no
    // second store to fall back to any more — everything is in this one.
    StorageBackend b;
    if (Task<Result<StorageBackend>> t = storage_info()) {
        Result<StorageBackend> r = co_await t;
        if (r.is_ok())
            b = r.value();
    }
    if (!b.opfs || !b.sync) {
        say(term, "this browser has no OPFS — Braam cannot run here");
        say(term, "private browsing usually explains it; try an ordinary window");
        co_return false;
    }

    // Asked before the store, since it is a round trip and nothing below waits
    // on it. Whether it reaches the grid is settled after the unpack.
    if (Task<void> t = learn_host())
        co_await t;

    Fs *root = heap_new<OpfsFs>();
    if (!root || vfs_mount("/", root).is_err()) {
        say(term, "no root filesystem");
        co_return false;
    }

    // The scheduler and the heap, readable with cat and grep. The job table is
    // not here any more: it is the shell's own memory, and the shell is a
    // process like any other (Concept.md §5.1).
    if (Fs *proc = procfs_create())
        if (vfs_mount("/proc", proc).is_err())
            say(term, "/proc would not mount");

    // The character devices. No mkdir: a mount point shows in its parent's
    // listing whether or not the store has a directory of that name.
    if (Fs *dev = devfs_create())
        if (vfs_mount("/dev", dev).is_err())
            say(term, "/dev would not mount");

    u32 unpacked = 0;
    if (Task<u32> t = unpack_if_stale(term, pid))
        unpacked = co_await t;

    // INIT is a file in the store, so this is the first point it can be read.
    if (Task<void> t = learn_init_prog())
        co_await t;

    // An archive that names its own program owns the grid from the version line
    // down: what the host is, and what came out of the archive, are braam's own
    // news in front of somebody else's. /proc/host still answers for both.
    if (!g_init) {
        show_host(term, b);
        if (unpacked) {
            Buf<96> line;
            line.put("unpacked ").put(unpacked).put(" files");
            say(term, line.str());
        }
    }

    if (Task<void> t = make_dirs(term))
        co_await t;
    if (Task<void> t = wipe_tmp())
        co_await t;

    // Landing in /home is what makes `echo hi > notes` persist by default. It
    // is the kernel's own directory now, and what /bin/sh inherits at exec.
    if (Task<Result<void>> t = vfs_chdir("/home"))
        co_await t;
    co_return true;
}

// What init hands the shell. No TERM, since the terminal is a cell grid with no
// terminfo entry that could describe it.
bool base_env(String &out)
{
    constexpr Str PATH_WORD = "PATH=/bin:/pkg/bin";
    static_assert(PATH_WORD.substr(5) == SYS_PATH_DEFAULT,
                  "init's PATH and the kernel's default must name the same list");

    // SHELL is the shell, not whatever INIT named: /bin/sh is still what a
    // script and an `sh -c` get.
    constexpr Str WORDS[] = { PATH_WORD, "HOME=/home", "SHELL=/bin/sh" };
    constexpr usize N     = sizeof(WORDS) / sizeof(WORDS[0]);

    usize n = argv_size(WORDS, N);
    if (!out.reserve(n))
        return false;
    for (usize i = 0; i < n; i++)
        out.push(0);
    argv_encode(WORDS, N, reinterpret_cast<u8 *>(out.data()));
    return true;
}

namespace {

// One terminal's session: `prog`, and another when that one dies. `greet` is
// terminal 0's — the motd is the system's, not the terminal's. `prog` lives in
// this frame, so &prog is stable for the session, which Args below needs.
Task<i32> shell_session(Term &term, Str prog, const u32 &pid, bool greet)
{
    bool restored = false;
    u32 tries     = 0;
    i32 status    = 1;

    // Encoded once: every shell this loop starts is entered with the same one.
    String env;
    if (!base_env(env))
        co_return 1;

    for (;;) {
        // Resolved every time round: the image was moved into the instance, so
        // the Executable cannot be reused.
        Executable exe;
        Result<void> found = Err(Error::NoMemory);
        if (Task<Result<void>> t = exec_resolve(prog, exe))
            found = co_await t;
        if (found.is_err()) {
            // Said on the grid rather than through a Stream, because there is
            // nobody left to print it: a store that will not give up this
            // program is a system with nothing running, and the reason has to
            // reach the screen.
            no_program(term, prog, found.error());

            // /bin is writable now, so `rm /bin/sh` is reachable from the
            // prompt — and the stamp would still match, so nothing would ever
            // put it back. This offer is what keeps the archive the thing the
            // store can be recovered from. Only for the shell: the archive is
            // /bin and /etc, so an unpack is no repair for a program INIT
            // named. Once: a second failure after a fresh unpack is not
            // something another unpack fixes.
            if (prog == SHELL && !restored) {
                restored = true;
                bool yes = false;
                if (Task<bool> t = ask(term, "restore /bin and /etc from the archive?", pid))
                    yes = co_await t;
                if (yes) {
                    Result<u32> r = Err(Error::NoMemory);
                    if (Task<Result<u32>> t = storage_unpack(BRAAM_VERSION))
                        r = co_await t;
                    if (r.is_ok())
                        continue;
                    say(term, "the root archive would not unpack");
                }
            }
            say(term, prog == SHELL
                          ? Str("there is no prompt — reload once the store is repaired")
                          : Str("there is nothing to run — reload once the store has it"));
            co_return 1;
        }
        // Init's own, and every replacement takes it again: the record under it
        // is removed and the host is told before this loop comes round, since
        // ~End calls proc_remove and proc_kill, and proc_kill is a host_svc
        // with nothing to await.
        exe.pid = pid;

        // Only now, with a shell that is going to run: a system with no prompt
        // coming should show why, not a greeting above a dead terminal. Once,
        // however many shells follow.
        if (greet) {
            greet = false;
            if (Task<void> t = show_motd(term))
                co_await t;
        }

        f64 started = sched_now();
        bool died   = true;
        Args args{ Span<const Str>(&prog, 1) };

        // Scoped, so this shell's record — removed by a destructor in there —
        // is gone before the next one, which answers to the same pid.
        {
            Task<i32> t =
                exec_process(exe, args, stdio_console(term), term, Str(), env.str(), &died);
            if (!t) {
                Buf<160> why;
                why.put(prog).put(" would not start");
                say(term, why.str());
                co_return 1;
            }
            status = co_await t;
        }

        // A shell that exited is the end of the session; so is a cancelled one,
        // which is the kernel going away rather than a shell to replace.
        if (!died || status == 130)
            break;

        // A shell that died is replaced (Concept.md §4) — but not for ever, and
        // a shell that lived a while is not a crash loop, so its death starts
        // the count again. A host with no worker to give is not a death: the
        // spawn waits for one rather than returning.
        tries = sched_now() - started >= RESPAWN_FLOOR_MS ? 1 : tries + 1;
        if (tries >= RESPAWN_TRIES) {
            Buf<160> why;
            why.put(called(prog)).put(" will not stay up — reload to start again");
            say(term, why.str());
            co_return status;
        }

        // The foreground the dead shell armed, or ^C at the next prompt reaches
        // pids that are gone (console.h). The keyboard and screen claims need
        // nothing: ~Proc drops both.
        console_fg_clear(term);

        Buf<160> line;
        line.put(called(prog)).put(" died (status ").put(status).put(") — starting another");
        say(term, line.str());
    }

    // The user's own `exit`, and there is no getty to start another. Say so
    // rather than leave a prompt that never comes back, and carry the status.
    Buf<160> line;
    line.put(called(prog)).put(" exited (status ").put(status).put(") — reload to start again");
    say(term, line.str());
    co_return status;
}

// A terminal the host made after boot gets a session of its own, whose pid is
// its own: only terminal 0's shell answers to init's.
u32 g_sh_pid[TERM_MAX];

Channel<u32, TERM_MAX> g_new_terms;

static_assert(is_trivially_destructible<Channel<u32, TERM_MAX>>, "a global must not need atexit");

// Started once the store is up, so a terminal made while it came up is served
// from the queue rather than lost.
Task<i32> term_watch()
{
    for (;;) {
        Result<u32> r = co_await g_new_terms.recv();
        if (r.is_err()) {
            if (r.error() == Error::Again)
                continue;
            co_return 0;
        }

        u32 id  = r.value();
        Term *t = id ? term_at(id) : nullptr; // terminal 0 is init's own
        if (!t)
            continue;

        banner(*t);
        // The shell, not INIT's program: that one is terminal 0's alone.
        if (Task<i32> s = shell_session(*t, SHELL, g_sh_pid[id], false))
            g_sh_pid[id] = sched_spawn(move(s), "sh");
    }
}

} // namespace

void init_term_up(Term &term, bool no_shell)
{
    // The pump first, and it needs no store: it is that terminal's only
    // keyboard receiver. The session waits for the store, since /bin is in it.
    if (!sched_spawn(console_pump(term), "tty"))
        return;
    if (!no_shell)
        g_new_terms.try_send(term_id(term));
}

Task<i32> init_task(const u32 &pid)
{
    Term *t0 = term_at(0);
    if (!t0)
        co_return 1;

    // The store comes first, and the mounts always did — but the shell used to
    // do them. It cannot any more: it is a file in /bin, which is in the store.
    bool up = false;
    if (Task<bool> t = boot_filesystem(*t0, pid))
        up = co_await t;
    if (!up)
        co_return 1;

    if (Task<i32> t = term_watch())
        sched_spawn(move(t), "getty");

    // g_init is boot_filesystem's: INIT is a file in the store it mounts.
    if (Task<i32> t = shell_session(*t0, g_init ? g_init->str() : SHELL, pid, true))
        co_return co_await t;
    co_return 1;
}
