// The port kit's Group B worked example: streams, descriptors and directories,
// in C's shapes with a co_await in front (doc/Compat.md §4).
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "compat/cio.h"

namespace {

const char *const DIR_PATH  = "/tmp/portio";
const char *const FILE_PATH = "/tmp/portio/words";

// `if (Task<T> t = f()) r = co_await t;` is the idiom a frame that would not
// allocate needs; a b_* that returns an awaiter is awaited outright.
Task<int> write_words()
{
    FILE *f = co_await b_fopen(FILE_PATH, "w");
    if (!f) {
        co_await b_perror("portio: fopen");
        co_return -1;
    }

    b_setvbuf(f, _IOFBF, BUFSIZ);
    co_await b_fprintf(f, "%s %d\n", "apple", 1);
    co_await b_fputs("cherry 2\n", f);
    for (const char *p = "fig 3\n"; *p; p++)
        co_await b_fputc(*p, f);
    co_await b_fwrite("pear 4\n", 1, 7, f);

    co_return co_await b_fclose(f);
}

Task<int> read_words()
{
    FILE *f = co_await b_fopen(FILE_PATH, "r");
    if (!f) {
        co_await b_perror("portio: fopen");
        co_return -1;
    }

    // One byte, put back, then the line it starts.
    int c = co_await b_fgetc(f);
    b_ungetc(c, f);

    char line[128];
    int lines = 0;
    while (co_await b_fgets(line, int(sizeof line), f))
        lines++;

    long at = co_await b_ftell(f);
    co_await b_printf("%d lines, %ld bytes, eof %d\n", lines, at, b_feof(f));

    // The off_t pair, for a file a long cannot address.
    co_await b_fseeko(f, 0, SEEK_END);
    off_t end = co_await b_ftello(f);
    co_await b_printf("end %lld\n", (long long)end);

    co_await b_fseek(f, 0, SEEK_SET);
    char head[8];
    size_t got = co_await b_fread(head, 1, sizeof head - 1, f);
    head[got]  = '\0';
    co_await b_printf("head %s\n", head);

    co_return co_await b_fclose(f);
}

Task<int> walk()
{
    struct stat st;
    if (co_await b_stat(FILE_PATH, &st) < 0) {
        co_await b_perror("portio: stat");
        co_return -1;
    }
    co_await b_printf("size %lld dir %d reg %d\n", (long long)st.st_size, S_ISDIR(st.st_mode),
                      S_ISREG(st.st_mode));

    DIR *d = co_await b_opendir(DIR_PATH);
    if (!d) {
        co_await b_perror("portio: opendir");
        co_return -1;
    }
    for (struct dirent *e = b_readdir(d); e; e = b_readdir(d))
        co_await b_printf("  %s%s\n", e->d_name, e->d_type == DT_DIR ? "/" : "");
    b_closedir(d);
    co_return 0;
}

// The descriptor half, which is what a port with no FILE uses.
Task<int> raw()
{
    int fd = co_await b_open(FILE_PATH, O_RDONLY);
    if (fd < 0) {
        co_await b_perror("portio: open");
        co_return -1;
    }

    struct stat st;
    co_await b_fstat(fd, &st);

    char buf[16];
    ssize_t n = co_await b_read(fd, buf, sizeof buf);
    off_t at  = co_await b_lseek(fd, 0, SEEK_END);
    co_await b_close(fd);

    co_await b_printf("read %ld, end %lld, fstat %lld, tty %d\n", (long)n, (long long)at,
                      (long long)st.st_size, co_await b_isatty(0));
    co_return 0;
}

} // namespace

Task<i32> proc_main(Args)
{
    char cwd[PATH_MAX];
    if (co_await b_getcwd(cwd, sizeof cwd))
        co_await b_printf("cwd %s\n", cwd);

    if (co_await b_mkdir(DIR_PATH, 0755) < 0 && errno != EEXIST) {
        co_await b_perror("portio: mkdir");
        co_return 1;
    }

    int bad = 0;
    if (Task<int> t = write_words())
        bad |= co_await t < 0;
    if (Task<int> t = read_words())
        bad |= co_await t < 0;
    if (Task<int> t = walk())
        bad |= co_await t < 0;
    if (Task<int> t = raw())
        bad |= co_await t < 0;

    co_await b_unlink(FILE_PATH);
    co_await b_rmdir(DIR_PATH);
    co_return bad ? 1 : 0;
}
