// Branch functions rather than a 257-entry table: --gc-sections drops the ones
// a port does not call, and seventeen branches cost less than the table.
#include <ctype.h>

extern "C" {

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

int isalpha(int c)
{
    return isupper(c) || islower(c);
}

int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// The six bytes kernel/text.h's is_space accepts.
int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

int isblank(int c)
{
    return c == ' ' || c == '\t';
}

int iscntrl(int c)
{
    return (c >= 0 && c < 32) || c == 127;
}

int isprint(int c)
{
    return c >= 32 && c < 127;
}

int isgraph(int c)
{
    return c > 32 && c < 127;
}

int ispunct(int c)
{
    return isgraph(c) && !isalnum(c);
}

int isascii(int c)
{
    return c >= 0 && c < 128;
}

int tolower(int c)
{
    return isupper(c) ? c + 32 : c;
}

int toupper(int c)
{
    return islower(c) ? c - 32 : c;
}

int toascii(int c)
{
    return c & 0x7f;
}

} // extern "C"
