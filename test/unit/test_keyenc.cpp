#include "harness.h"
#include "proc/keyenc.h"

namespace {

// Encodes one key and compares the bytes.
void enc(u32 code, u32 mods, Str want, Str what)
{
    char buf[KEY_ENC_MAX];
    usize n = key_encode(Key{ code, mods }, buf);

    test_check(Str(buf, n) == want, what, __FILE_NAME__, __LINE__);
}

} // namespace

void test_keyenc()
{
    test_begin("keyenc");

    // §5.1: one or two bytes, and no timer for Escape (§6.5).
    enc(KEY_ENTER, 0, "\r", "enter is CR");
    enc(KEY_TAB, 0, "\t", "tab is HT");
    enc(KEY_BACKSPACE, 0, "\177", "backspace is DEL");
    enc(KEY_ESCAPE, 0, "\033", "escape is ESC");
    enc('a', 0, "a", "a printable key is its codepoint");
    enc('A', MOD_SHIFT, "A", "shift is already in the codepoint");

    // §5.2: CSI for the arrows and Home/End, never SS3.
    enc(KEY_UP, 0, "\033[A", "up");
    enc(KEY_DOWN, 0, "\033[B", "down");
    enc(KEY_RIGHT, 0, "\033[C", "right");
    enc(KEY_LEFT, 0, "\033[D", "left");
    enc(KEY_HOME, 0, "\033[H", "home");
    enc(KEY_END, 0, "\033[F", "end");

    // The tilde family, and F1-F4 alone in SS3.
    enc(KEY_INSERT, 0, "\033[2~", "insert");
    enc(KEY_DELETE, 0, "\033[3~", "delete");
    enc(KEY_PAGE_UP, 0, "\033[5~", "page up");
    enc(KEY_PAGE_DOWN, 0, "\033[6~", "page down");
    enc(KEY_F1, 0, "\033OP", "F1");
    enc(KEY_F4, 0, "\033OS", "F4");
    enc(KEY_F5, 0, "\033[15~", "F5");
    enc(KEY_F8, 0, "\033[19~", "F8");
    enc(KEY_F9, 0, "\033[20~", "F9");
    enc(KEY_F12, 0, "\033[24~", "F12");

    // §5.3, the parameterised form. m = 1 + shift + 2*alt + 4*ctrl + 8*meta,
    // and F1-F4 lose SS3 when modified.
    enc(KEY_UP, MOD_SHIFT, "\033[1;2A", "shift+up");
    enc(KEY_UP, MOD_CTRL, "\033[1;5A", "ctrl+up");
    enc(KEY_END, MOD_ALT, "\033[1;3F", "alt+end");
    enc(KEY_PAGE_UP, MOD_CTRL | MOD_SHIFT, "\033[5;6~", "ctrl+shift+page up");
    enc(KEY_F1, MOD_CTRL, "\033[1;5P", "ctrl+F1");
    enc(KEY_F12, MOD_META, "\033[24;9~", "meta+F12");
    enc(KEY_F12, MOD_SHIFT | MOD_ALT | MOD_CTRL | MOD_META, "\033[24;16~", "every modifier");

    // §5.3, the control column.
    enc('a', MOD_CTRL, "\001", "ctrl+a");
    enc('Z', MOD_CTRL, "\032", "ctrl+Z");
    enc(' ', MOD_CTRL, Str("\0", 1), "ctrl+space is NUL");
    enc('@', MOD_CTRL, Str("\0", 1), "ctrl+@ is NUL");
    enc('[', MOD_CTRL, "\033", "ctrl+[");
    enc('_', MOD_CTRL, "\037", "ctrl+_");
    enc('?', MOD_CTRL, "\177", "ctrl+?");
    enc('1', MOD_CTRL, "", "ctrl+1 sends nothing");

    // Alt and Meta prefix ESC, and compose with the control column.
    enc('x', MOD_ALT, "\033x", "alt+x");
    enc('x', MOD_META, "\033x", "meta+x");
    enc('d', MOD_ALT | MOD_CTRL, "\033\004", "alt+ctrl+d");
    enc(KEY_ENTER, MOD_ALT, "\033\r", "alt+enter");

    // A printable key goes out in UTF-8, which is what carries Cyrillic.
    enc(0x044f, 0, "\321\217", "я is two bytes");
    enc(0x0416, 0, "\320\226", "Ж is two bytes");

    // 0x9b is never a CSI, in either direction (§6.3).
    for (u32 code = KEY_NAMED; code < KEY_NAMED + 26; code++)
        for (u32 mods = 0; mods < 16; mods++) {
            char buf[KEY_ENC_MAX];
            usize n = key_encode(Key{ code, mods }, buf);
            for (usize i = 0; i < n; i++)
                CHECK((unsigned char)buf[i] != 0x9b);
        }

    // A code past the table sends nothing rather than reading off the end.
    enc(KEY_NAMED + 26, 0, "", "a named key the table has not got");
}
