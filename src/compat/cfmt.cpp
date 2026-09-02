// One printf engine, in place of six. Against the five it replaces: every
// length modifier is honoured (iconv swallowed l and z, so %ld read an int),
// precision is applied and not discarded (zip parsed and dropped it), a
// negative * width left-justifies (uemacs lost it), and the return value is
// C99's — the count that would have been written (zip returned what it wrote).
#define BRAAM_COMPAT_BUILDING 1

#include "cfmt.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

enum : unsigned {
    F_LEFT  = 1u << 0,
    F_PLUS  = 1u << 1,
    F_SPACE = 1u << 2,
    F_HASH  = 1u << 3,
    F_ZERO  = 1u << 4,
};

// Truncating sink: `n` counts what was wanted, which is the return value, and
// only the first cap-1 bytes are stored.
struct Sink {
    char *buf;
    size_t cap;
    size_t n;

    void put(char c)
    {
        if (buf && n + 1 < cap)
            buf[n] = c;
        n++;
    }

    void put(const char *s, size_t len)
    {
        while (len--)
            put(*s++);
    }

    void pad(char c, long count)
    {
        while (count-- > 0)
            put(c);
    }
};

struct Spec {
    unsigned flags;
    long width;
    long prec; // -1 when absent
};

const char *DIGITS_LO = "0123456789abcdef";
const char *DIGITS_UP = "0123456789ABCDEF";

// Body into `out`, returned length. Caller adds sign, prefix and padding.
size_t to_digits(char *out, unsigned long long v, unsigned base, bool upper)
{
    const char *d = upper ? DIGITS_UP : DIGITS_LO;
    char tmp[24];
    size_t i = 0;
    do {
        tmp[i++] = d[v % base];
        v /= base;
    } while (v);
    for (size_t j = 0; j < i; j++)
        out[j] = tmp[i - 1 - j];
    return i;
}

void emit_number(Sink &s, const Spec &sp, bool neg, const char *prefix, size_t prefix_len,
                 const char *body, size_t body_len)
{
    char sign = neg ? '-' : sp.flags & F_PLUS ? '+' : sp.flags & F_SPACE ? ' ' : '\0';

    // Precision pads the digits with zeros and cancels the 0 flag.
    long zeros = sp.prec >= 0 && size_t(sp.prec) > body_len ? sp.prec - long(body_len) : 0;
    long len   = long(body_len) + zeros + long(prefix_len) + (sign ? 1 : 0);
    long gap   = sp.width > len ? sp.width - len : 0;

    bool zero_pad = (sp.flags & F_ZERO) && !(sp.flags & F_LEFT) && sp.prec < 0;
    if (!(sp.flags & F_LEFT) && !zero_pad)
        s.pad(' ', gap);
    if (sign)
        s.put(sign);
    s.put(prefix, prefix_len);
    if (zero_pad)
        s.pad('0', gap);
    s.pad('0', zeros);
    s.put(body, body_len);
    if (sp.flags & F_LEFT)
        s.pad(' ', gap);
}

} // namespace

extern "C" int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    Sink s{ buf, cap, 0 };

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            s.put(*p);
            continue;
        }
        p++;
        if (*p == '%') {
            s.put('%');
            continue;
        }

        Spec sp{ 0, 0, -1 };
        for (;; p++) {
            if (*p == '-')
                sp.flags |= F_LEFT;
            else if (*p == '+')
                sp.flags |= F_PLUS;
            else if (*p == ' ')
                sp.flags |= F_SPACE;
            else if (*p == '#')
                sp.flags |= F_HASH;
            else if (*p == '0')
                sp.flags |= F_ZERO;
            else
                break;
        }

        if (*p == '*') {
            int w = va_arg(ap, int);
            p++;
            // A negative * width is a '-' flag with a positive width, which
            // uemacs's engine accepted and then dropped.
            if (w < 0) {
                sp.flags |= F_LEFT;
                sp.width = -long(w);
            } else {
                sp.width = w;
            }
        } else {
            while (*p >= '0' && *p <= '9')
                sp.width = sp.width * 10 + (*p++ - '0');
        }

        if (*p == '.') {
            p++;
            if (*p == '*') {
                int pr  = va_arg(ap, int);
                sp.prec = pr < 0 ? -1 : pr;
                p++;
            } else {
                sp.prec = 0;
                while (*p >= '0' && *p <= '9')
                    sp.prec = sp.prec * 10 + (*p++ - '0');
            }
        }

        // Every one of these is honoured. iconv's engine swallowed l and z and
        // then read an int, misreading every 64-bit argument.
        enum { L_INT, L_CHAR, L_SHORT, L_LONG, L_LLONG, L_SIZE, L_INTMAX, L_PTRDIFF };
        int len = L_INT;
        if (*p == 'h') {
            len = *++p == 'h' ? (p++, L_CHAR) : L_SHORT;
        } else if (*p == 'l') {
            len = *++p == 'l' ? (p++, L_LLONG) : L_LONG;
        } else if (*p == 'z') {
            len = L_SIZE;
            p++;
        } else if (*p == 'j') {
            len = L_INTMAX;
            p++;
        } else if (*p == 't') {
            len = L_PTRDIFF;
            p++;
        } else if (*p == 'L') {
            // There is no long double on this target; va_arg would not link.
            p++;
        }

        char conv = *p;
        char body[24];
        switch (conv) {
        case 'd':
        case 'i': {
            long long v = 0;
            switch (len) {
            case L_CHAR:  v = (signed char)va_arg(ap, int); break;
            case L_SHORT: v = (short)va_arg(ap, int); break;
            case L_LONG:  v = va_arg(ap, long); break;
            case L_LLONG: v = va_arg(ap, long long); break;
            // The real types: size_t and ptrdiff_t are 32-bit on wasm32 and
            // intmax_t is 64, so naming them keeps this right by derivation.
            // Signed size_t is ptrdiff_t's width on this target.
            case L_SIZE:
            case L_PTRDIFF: v = (long long)va_arg(ap, ptrdiff_t); break;
            case L_INTMAX:  v = va_arg(ap, long long); break;
            default: v = va_arg(ap, int); break;
            }
            bool neg              = v < 0;
            unsigned long long uv = neg ? 0ull - (unsigned long long)v : (unsigned long long)v;
            size_t n              = to_digits(body, uv, 10, false);
            emit_number(s, sp, neg, "", 0, body, n);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            unsigned long long v = 0;
            switch (len) {
            case L_CHAR:  v = (unsigned char)va_arg(ap, unsigned); break;
            case L_SHORT: v = (unsigned short)va_arg(ap, unsigned); break;
            case L_LONG:  v = va_arg(ap, unsigned long); break;
            case L_LLONG: v = va_arg(ap, unsigned long long); break;
            case L_SIZE:    v = va_arg(ap, size_t); break;
            case L_PTRDIFF: v = (unsigned long long)va_arg(ap, ptrdiff_t); break;
            case L_INTMAX:  v = va_arg(ap, unsigned long long); break;
            default: v = va_arg(ap, unsigned); break;
            }
            unsigned base = conv == 'u' ? 10 : conv == 'o' ? 8 : 16;
            size_t n      = to_digits(body, v, base, conv == 'X');
            const char *prefix = "";
            size_t plen        = 0;
            if (sp.flags & F_HASH) {
                if (conv == 'o' && body[0] != '0') {
                    prefix = "0";
                    plen   = 1;
                } else if ((conv == 'x' || conv == 'X') && v) {
                    prefix = conv == 'x' ? "0x" : "0X";
                    plen   = 2;
                }
            }
            emit_number(s, sp, false, prefix, plen, body, n);
            break;
        }
        case 'p': {
            void *v  = va_arg(ap, void *);
            size_t n = to_digits(body, (unsigned long long)(usize)v, 16, false);
            Spec ps  = sp;
            ps.prec  = -1;
            emit_number(s, ps, false, "0x", 2, body, n);
            break;
        }
        case 'c': {
            char c   = char(va_arg(ap, int));
            long gap = sp.width > 1 ? sp.width - 1 : 0;
            if (!(sp.flags & F_LEFT))
                s.pad(' ', gap);
            s.put(c);
            if (sp.flags & F_LEFT)
                s.pad(' ', gap);
            break;
        }
        case 's': {
            const char *v = va_arg(ap, const char *);
            if (!v)
                v = "(null)";
            // Precision caps a string and it need not be terminated within it.
            size_t n = sp.prec >= 0 ? strnlen(v, size_t(sp.prec)) : strlen(v);
            long gap = sp.width > long(n) ? sp.width - long(n) : 0;
            if (!(sp.flags & F_LEFT))
                s.pad(' ', gap);
            s.put(v, n);
            if (sp.flags & F_LEFT)
                s.pad(' ', gap);
            break;
        }
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            // musl's engine with printf's precision convention, so style and
            // precision come across unchanged; a separate archive, see cfmt.h.
            double v = va_arg(ap, double);
            char fbuf[512];
            Str t    = compat_fmt_f64(fbuf, sizeof(fbuf), v, i32(sp.prec), conv);
            bool neg = t.size() && t[0] == '-';
            const char *d = t.data() + (neg ? 1 : 0);
            size_t n      = t.size() - (neg ? 1 : 0);
            Spec fs       = sp;
            fs.prec       = -1; // already applied by compat_fmt_f64
            emit_number(s, fs, neg, "", 0, d, n);
            break;
        }
        case '\0':
            p--; // a trailing % — stop without running off the end
            break;
        default:
            s.put('%');
            s.put(conv);
            break;
        }
    }

    if (buf && cap)
        buf[s.n < cap ? s.n : cap - 1] = '\0';
    return int(s.n);
}

extern "C" {

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, ~size_t(0), fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, ~size_t(0), fmt, ap);
    va_end(ap);
    return n;
}

} // extern "C"
