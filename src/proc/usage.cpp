#include "usage.h"

#include "io.h"

Task<i32> usage_asked(Str text)
{
    co_return (co_await write_all(SYS_STDOUT, text)).is_err() ? 1 : 0;
}

Task<i32> usage_error(Str text)
{
    co_await write_all(SYS_STDERR, text);
    co_return 2;
}
