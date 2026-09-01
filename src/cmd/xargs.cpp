#include "kernel/fmt.h"
#include "kernel/text.h"
#include "kernel/vec.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// Arguments off the input, batched into command lines. FreeBSD's usr.bin/xargs
// is the port. -p and -o open /dev/tty, of which there is none here, and -J,
// -P, -R and -S go with them.

namespace {

constexpr Str ECHO = "/bin/echo";

// The most one command line may be. Sys::Spawn stages its payload, so
// SYS_STAGE_MAX is the ceiling and this is well inside it.
constexpr usize LINE_MAX = 128 * 1024;

// The most arguments one command line may take.
constexpr usize ARGS_MAX = 5000;

constexpr Str USAGE =
    "Usage:\n"
    "    xargs [-0rtx] [-E <eof>] [-I <str>] [-L <lines>]\n"
    "          [-n <count>] [-s <size>] [<command> [<arg>...]]\n"
    "Options:\n"
    "    -0    the input is separated by nulls; nothing quotes\n"
    "    -E    stop at an argument equal to <eof>\n"
    "    -I    put one input line where <str> is in an\n"
    "          argument, and run once a line\n"
    "    -L    input lines per run\n"
    "    -n    input arguments per run\n"
    "    -r    accepted and ignored: an empty input runs nothing\n"
    "    -s    the most bytes one command line may be\n"
    "    -t    print each command on stderr before running it\n"
    "    -x    fail rather than run a command that does not fit\n";

// One argument, as a place in the byte buffer: a Str would dangle when the
// buffer grows.
struct XWord {
    usize off = 0;
    usize len = 0;
};

// Why scan() stopped. End is also "nothing to report".
enum class Stop {
    End,
    Run,   // a batch is ready
    Quote, // a quote no newline closed
    Slash, // a backslash with nothing behind it
    Room,  // -x, and the batch does not fit
    Space, // one argument outgrew the whole budget
    Mem,
};

struct Xargs {
    Args cmd; // the command and the words it was given
    Str repl; // -I
    Str eof;  // -E
    usize nargs  = ARGS_MAX;
    usize nlines = 0; // -L, and 1 under -I
    usize budget = 0; // bytes the input arguments may take
    bool zero    = false;
    bool trace   = false;
    bool strict  = false;

    String buf;       // the arguments, each null-terminated
    Vec<XWord> words; // where each of them is
    String line;      // -I's whole input line
    usize argp = 0;   // where the argument being read begins

    bool insingle = false;
    bool indouble = false;
    bool quoted   = false; // the argument being read carried a quote
    bool escape   = false; // a backslash is holding the next byte
    bool got      = false; // this line has given an argument
    usize lines   = 0;     // lines since the last run

    bool ateof = false; // -E's string turned up
    bool reloc = false; // the run is mid-argument; its tail is kept
    char held  = 0;     // and the byte that did not fit

    i32 status = 0;
    bool halt  = false; // read no further
};

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("xargs: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

Task<void> warn(Str what, Str why)
{
    Buf<128> b;
    b.put("xargs: ");
    if (!what.empty())
        b.put(what).put(": ");
    b.put(why).put('\n');
    co_await write_all(SYS_STDERR, b.str());
}

Task<i32> fatal(Stop s)
{
    Str why = "out of memory";
    switch (s) {
    case Stop::Quote:
        why = "unterminated quote";
        break;
    case Stop::Slash:
        why = "a backslash ends the input";
        break;
    case Stop::Room:
        why = "insufficient space for arguments";
        break;
    case Stop::Space:
        why = "insufficient space for an argument";
        break;
    default:
        break;
    }
    co_await warn({}, why);
    co_return 1;
}

// The argument that just ended, if it is one. An empty one counts only when it
// was quoted.
bool commit(Xargs &x)
{
    usize len = x.buf.size() - x.argp;
    Str s(x.buf.data() + x.argp, len);
    if (!x.eof.empty() && s == x.eof) {
        x.buf.truncate(x.argp);
        x.ateof = true;
        return true;
    }
    if (len == 0 && !x.quoted)
        return true;

    // Before the terminator below, which may move the buffer under `s`.
    if (!x.repl.empty() && ((!x.line.empty() && !x.line.push(' ')) || !x.line.append(s)))
        return false;
    if (!x.buf.push('\0') || !x.words.push(XWord{ x.argp, len }))
        return false;
    x.got = true;
    return true;
}

// Bytes into arguments, stopping at whatever the caller has to act on. One
// coroutine frame per run rather than per byte, which is why this is not one.
Stop scan(Xargs &x, Str s, usize &i)
{
    while (i < s.size()) {
        char c   = s[i++];
        bool sep = false;
        bool eol = false;

        if (x.escape) {
            x.escape = false; // the byte is data, whatever it is
        } else if (x.zero) {
            sep = eol = c == '\0';
        } else if (c == ' ' || c == '\t') {
            sep = !x.insingle && !x.indouble;
        } else if (c == '\n') {
            sep = eol = true;
        } else if (c == '\'' && !x.indouble) {
            x.insingle = !x.insingle;
            x.quoted   = true;
            continue;
        } else if (c == '"' && !x.insingle) {
            x.indouble = !x.indouble;
            x.quoted   = true;
            continue;
        } else if (c == '\\' && !x.insingle && !x.indouble) {
            x.escape = true;
            continue;
        }

        if (!sep) {
            if (x.buf.size() < x.budget) {
                if (!x.buf.push(c))
                    return Stop::Mem;
                continue;
            }
            if (x.words.empty())
                return Stop::Space;
            if (x.strict)
                return Stop::Room;
            x.reloc = true; // the argument is unfinished; the run keeps its tail
            x.held  = c;
            return Stop::Run;
        }

        if (eol && (x.insingle || x.indouble))
            return Stop::Quote;
        if (!commit(x))
            return Stop::Mem;
        if (eol && x.got) { // a line with no argument on it is not a line
            x.lines++;
            x.got = false;
        }
        x.argp   = x.buf.size();
        x.quoted = false;

        bool full = x.words.size() >= x.nargs;
        bool over = x.buf.size() >= x.budget;
        if (x.strict && over && !full)
            return Stop::Room;
        if (full || over || (x.nlines != 0 && x.lines >= x.nlines) || x.ateof)
            return Stop::Run;
    }
    return Stop::End;
}

// The end of the input: a last argument, and a last run if there is anything
// to run over.
Stop finish(Xargs &x)
{
    if (x.escape)
        return Stop::Slash;
    if (x.insingle || x.indouble)
        return Stop::Quote;
    if (!commit(x))
        return Stop::Mem;
    x.argp = x.buf.size();
    return x.words.empty() ? Stop::End : Stop::Run;
}

// The words as given, then the arguments read.
bool build(Xargs &x, Vec<Str> &argv)
{
    for (usize i = 0; i < x.cmd.size(); i++)
        if (!argv.push(x.cmd[i]))
            return false;
    for (usize i = 0; i < x.words.size(); i++)
        if (!argv.push(Str(x.buf.data() + x.words[i].off, x.words[i].len)))
            return false;
    return true;
}

// -I: the line goes where the marker is and the arguments go nowhere else.
// The command's own name is never substituted.
bool build_repl(Xargs &x, Vec<Str> &argv, String &sub, Vec<XWord> &at)
{
    if (!argv.push(x.cmd[0]))
        return false;
    for (usize i = 1; i < x.cmd.size(); i++) {
        usize off = sub.size();
        Str w     = x.cmd[i];
        for (usize k = 0;;) {
            usize hit = w.find(x.repl, k);
            if (hit == Str::npos) {
                if (!sub.append(w.substr(k)))
                    return false;
                break;
            }
            if (!sub.append(w.substr(k, hit - k)) || !sub.append(x.line.str()))
                return false;
            k = hit + x.repl.size();
        }
        if (!at.push(XWord{ off, sub.size() - off }))
            return false;
    }
    for (usize i = 0; i < at.size(); i++)
        if (!argv.push(Str(sub.data() + at[i].off, at[i].len)))
            return false;
    return true;
}

// One command, its stdin /dev/null: this process reads ahead on its own, so a
// shared descriptor 0 would hand the child an arbitrary remainder.
Task<Result<i32>> once(Xargs &x, Args argv)
{
    if (x.trace) {
        String t;
        for (usize i = 0; i < argv.size(); i++)
            if ((i != 0 && !t.push(' ')) || !t.append(argv[i]))
                co_return Err(Error::NoMemory);
        if (!t.push('\n'))
            co_return Err(Error::NoMemory);
        Result<void> w = co_await write_all(SYS_STDERR, t.str());
        if (w.is_err())
            co_return Err(w.error());
    }

    Result<i32> nul = co_await open_read("/dev/null");
    if (nul.is_err())
        co_return Err(nul.error());

    // Moved rather than shared, so it is opened again for the next run.
    ChildIo io{ u32(nul.value()), SYS_STDOUT, SYS_STDERR };
    Result<u32> pid = co_await spawn(argv, io);
    if (pid.is_err()) {
        co_await close_fd(u32(nul.value()));
        co_return Err(pid.error());
    }

    Result<Exited> done = co_await wait_child(pid.value());
    if (done.is_err()) {
        co_await kill_child(pid.value()); // ^C abandoned the wait
        co_return Err(done.error());
    }
    co_return done.value().status;
}

// One batch run, and the next one started.
Task<void> flush(Xargs &x)
{
    Vec<Str> argv;
    String sub;
    Vec<XWord> at;
    if (!(x.repl.empty() ? build(x, argv) : build_repl(x, argv, sub, at))) {
        co_await warn({}, "out of memory");
        x.status = 1;
        x.halt   = true;
        co_return;
    }

    Result<i32> r = co_await once(x, Args{ argv });
    if (r.is_err()) {
        if (r.error() == Error::Cancelled) {
            x.status = 130;
        } else {
            co_await errln("xargs", x.cmd[0], r.error());
            x.status = r.error() == Error::NotFound ? 127 : 126;
        }
        x.halt = true;
        co_return;
    }

    // A signal or a 255 stops the reading, as POSIX asks; anything else
    // non-zero is carried to the end.
    i32 st = r.value();
    if (st >= 128 || st == 255) {
        co_await warn(x.cmd[0],
                      st == 255 ? "exited with 255; stopping" : "terminated by a signal; stopping");
        x.status = 1;
        x.halt   = true;
        co_return;
    }
    if (st != 0)
        x.status = 1;

    x.words.clear();
    x.line.clear();
    x.lines = 0;
    x.got   = false;
    if (x.reloc) {
        x.buf.erase(0, x.argp); // the unfinished argument, and the byte after it
        x.reloc = false;
        if (!x.buf.push(x.held)) {
            x.status = 1;
            x.halt   = true;
        }
    } else {
        x.buf.clear();
    }
    x.argp = 0;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Xargs x;
    usize size = LINE_MAX;
    bool has_n = false, has_l = false;
    OptParse p(args, Opts{ "0rtx", "EILns" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            Str why = r.error() == Error::NotFound ? "needs a value" : "bad option";
            co_return co_await complain(why, Str(&o.name, 1));
        }
        if (!r.value())
            break;

        switch (o.name) {
        case '0':
            x.zero = true;
            break;
        case 'r': // ignored: an empty input runs nothing anyway
            break;
        case 't':
            x.trace = true;
            break;
        case 'x':
            x.strict = true;
            break;
        case 'E':
            x.eof = o.value;
            break;
        case 'I':
            if (o.value.empty())
                co_return co_await complain("needs a value", "I");
            x.repl = o.value;
            break;
        default: {
            Option<u32> n = parse_u32(o.value);
            if (!n.has_value() || n.value() == 0)
                co_return co_await complain("not a count", o.value);
            if (o.name == 'n') {
                x.nargs = n.value();
                has_n   = true;
            } else if (o.name == 'L') {
                x.nlines = n.value();
                has_l    = true;
            } else {
                size = n.value();
            }
            break;
        }
        }
    }

    // FreeBSD's own consistency rules, bar one: there, -x alone also flushes at
    // every line, which makes -n2 -x behave as -n1.
    if (x.strict && !has_n)
        co_return co_await complain("-x wants -n", {});
    if (!x.repl.empty()) {
        x.nlines = 1; // -I is a run to the line
        x.strict = true;
    }
    if (has_l)
        x.strict = true;

    Str only[1] = { ECHO };
    Args rest   = p.rest();
    x.cmd       = rest.size() != 0 ? rest : Args{ Span<const Str>(only, 1) };

    usize cost = 0;
    for (usize i = 0; i < x.cmd.size(); i++)
        cost += x.cmd[i].size() + 1;
    if (size > LINE_MAX)
        size = LINE_MAX;
    if (cost + 1 >= size)
        co_return co_await complain("insufficient space for the command", {});
    x.budget = size - cost;

    for (;;) {
        Result<String> r = co_await read_chunk(SYS_STDIN);
        if (r.is_err()) {
            if (r.error() != Error::Closed) {
                if (r.error() == Error::Cancelled)
                    co_return 130;
                co_await errln("xargs", "stdin", r.error());
                co_return 1;
            }
            Stop s = finish(x);
            if (s == Stop::Run)
                co_await flush(x);
            else if (s != Stop::End)
                co_return co_await fatal(s);
            break;
        }

        Str in  = r.value().str();
        usize i = 0;
        while (i < in.size() && !x.halt) {
            Stop s = scan(x, in, i);
            if (s == Stop::Run) {
                co_await flush(x);
                if (x.ateof)
                    x.halt = true;
            } else if (s != Stop::End) {
                co_return co_await fatal(s);
            }
        }
        if (x.halt)
            break;
    }

    co_return x.status;
}
