#include "keyenc.h"

#include "kernel/text.h"

namespace {

// One row of §5.2: the final byte, the tilde family's parameter (0 for the
// letter family), and whether the unmodified form is SS3.
struct Named {
    char fin;
    u8 par;
    bool ss3;
};

// KEY_ENTER..KEY_F12 are 26 consecutive codes, so the row is an index. The
// first four send one byte and are handled below; their rows are empty.
const Named NAMED[] = {
    { 0, 0, false },    // KEY_ENTER
    { 0, 0, false },    // KEY_BACKSPACE
    { 0, 0, false },    // KEY_TAB
    { 0, 0, false },    // KEY_ESCAPE
    { '~', 3, false },  // KEY_DELETE
    { '~', 2, false },  // KEY_INSERT
    { 'A', 0, false },  // KEY_UP
    { 'B', 0, false },  // KEY_DOWN
    { 'D', 0, false },  // KEY_LEFT
    { 'C', 0, false },  // KEY_RIGHT
    { 'H', 0, false },  // KEY_HOME
    { 'F', 0, false },  // KEY_END
    { '~', 5, false },  // KEY_PAGE_UP
    { '~', 6, false },  // KEY_PAGE_DOWN
    { 'P', 0, true },   // KEY_F1
    { 'Q', 0, true },   // KEY_F2
    { 'R', 0, true },   // KEY_F3
    { 'S', 0, true },   // KEY_F4
    { '~', 15, false }, // KEY_F5
    { '~', 17, false }, // KEY_F6
    { '~', 18, false }, // KEY_F7
    { '~', 19, false }, // KEY_F8
    { '~', 20, false }, // KEY_F9
    { '~', 21, false }, // KEY_F10
    { '~', 23, false }, // KEY_F11
    { '~', 24, false }, // KEY_F12
};

// Two digits at most: a parameter is 24 and a modifier 16.
usize put_num(char *out, u32 v)
{
    usize n = 0;

    if (v >= 10)
        out[n++] = char('0' + v / 10);
    out[n++] = char('0' + v % 10);
    return n;
}

// §5.3, the control column. -1 for a key that sends nothing.
i32 ctrl_byte(u32 c)
{
    if (c >= 'a' && c <= 'z')
        return i32(c - 'a' + 1);
    if (c >= 'A' && c <= 'Z')
        return i32(c - 'A' + 1);
    if (c == ' ' || c == '@')
        return 0;
    if (c >= '[' && c <= '_') // [ \ ] ^ _ -> 0x1b..0x1f
        return i32(c - '@');
    if (c == '?')
        return 0177;
    return -1;
}

} // namespace

usize key_encode(const Key &k, char out[KEY_ENC_MAX])
{
    usize n = 0;

    // §5.3: ESC, then the key's own bytes. Before everything, so Alt+Ctrl+D is
    // ESC 004. A modified named key folds alt into <m> instead, below.
    bool esc_prefix = (k.mods & (MOD_ALT | MOD_META)) != 0;

    if (k.code >= KEY_NAMED) {
        u32 i = k.code - KEY_NAMED;
        if (i >= sizeof(NAMED) / sizeof(NAMED[0]))
            return 0; // a key key.h grew later
        const Named &e = NAMED[i];

        if (!e.fin) {
            // §5.1. Shift and control have no form here, so they send the
            // plain byte rather than nothing.
            constexpr char ONE[] = { '\r', 0177, '\t', 033 };
            if (esc_prefix)
                out[n++] = 033;
            out[n++] = ONE[i];
            return n;
        }

        u32 m = 1 + ((k.mods & MOD_SHIFT) ? 1 : 0) + ((k.mods & MOD_ALT) ? 2 : 0) +
                ((k.mods & MOD_CTRL) ? 4 : 0) + ((k.mods & MOD_META) ? 8 : 0);

        out[n++] = 033;
        if (m == 1 && e.ss3) { // ESC O P .. ESC O S
            out[n++] = 'O';
            out[n++] = e.fin;
            return n;
        }
        out[n++] = '['; // two bytes, never 0x9b (§6.3)
        if (e.par || m != 1)
            n += put_num(out + n, e.par ? e.par : 1);
        if (m != 1) {
            out[n++] = ';';
            n += put_num(out + n, m);
        }
        out[n++] = e.fin;
        return n;
    }

    if (esc_prefix)
        out[n++] = 033;

    if (k.mods & MOD_CTRL) {
        i32 c = ctrl_byte(k.code);
        if (c < 0)
            return 0;
        out[n++] = char(c);
        return n;
    }

    // Shift needs no arm: it is already in the codepoint. Everything else is
    // the codepoint in UTF-8, which is what carries Cyrillic.
    n += utf8_encode(char32_t(k.code), out + n);
    return n;
}
