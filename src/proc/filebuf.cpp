#include "filebuf.h"

#include "kernel/text.h"

void FileBuf::adopt(char *p, usize cap, usize len)
{
    b_   = p;
    cap_ = cap;
    pos_ = 0;
    len_ = len;
}

void FileBuf::consume(usize n)
{
    usize have = len_ - pos_;
    pos_ += n < have ? n : have;
    if (pos_ == len_)
        pos_ = len_ = 0;
}

void FileBuf::compact()
{
    if (pos_ == 0)
        return;
    usize have = len_ - pos_;
    if (have)
        __builtin_memmove(b_, b_ + pos_, have);
    pos_ = 0;
    len_ = have;
}

void FileBuf::reset()
{
    pos_ = len_ = 0;
}

RuneStep FileBuf::take(char32_t &out)
{
    char32_t ch = 0;
    usize n     = utf8_decode(held(), 0, ch);
    if (n == 0)
        return RuneStep::Need;
    consume(n);
    out = ch;
    return RuneStep::Got;
}

char32_t FileBuf::take_broken()
{
    if (pos_ < len_)
        consume(1);
    return 0xfffd;
}

bool FileBuf::unget(char32_t c)
{
    char e[4];
    usize n = utf8_encode(c, e);

    // Room in front, made by moving the held bytes along.
    if (pos_ < n) {
        if (len_ - pos_ + n > cap_)
            return false;
        __builtin_memmove(b_ + n, b_ + pos_, len_ - pos_);
        len_ = len_ - pos_ + n;
        pos_ = n;
    }
    pos_ -= n;
    __builtin_memcpy(b_ + pos_, e, n);
    return true;
}

LineStep FileBuf::take_line(String &out, bool keep_nl)
{
    Str s    = held();
    usize nl = s.find('\n');
    if (nl == Str::npos) {
        if (!out.append(s))
            return LineStep::NoMemory;
        consume(s.size());
        return LineStep::Need;
    }

    if (!out.append(s.substr(0, keep_nl ? nl + 1 : nl)))
        return LineStep::NoMemory;
    consume(nl + 1);
    return LineStep::Done;
}

bool FileBuf::append(Str s)
{
    if (s.size() > cap_ - len_)
        return false;
    if (s.size())
        __builtin_memcpy(b_ + len_, s.data(), s.size());
    len_ += s.size();
    return true;
}

usize FileBuf::append_rune(char32_t c)
{
    char e[4];
    usize n = utf8_encode(c, e);
    if (n > cap_ - len_)
        return 0;
    __builtin_memcpy(b_ + len_, e, n);
    len_ += n;
    return n;
}
