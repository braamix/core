# bzpipe — compression with braam::bzip2

`zpipe`'s twin for bzip2: a Braam program that compresses stdin to stdout in
the bzip2 format, or decompresses it back with `-d`:

    bzpipe <notes.txt >notes.bz2
    bzpipe -d <notes.bz2 >notes.txt

Its output is what `bzip2 -9` makes of the same bytes, and `-d` reads what
`bzip2` wrote, several streams one after another included.

It is built exactly as `hello` is, with the toolchain file the SDK ships:

    cmake -B build --toolchain <prefix>/lib/cmake/braam/wasm32-unknown-unknown.cmake
    cmake --build build

The one difference is the library it asks for. `CMakeLists.txt` says
`LIBS braam::bzip2`, and `bzpipe.cpp` includes `bzip2/bzip2.h`: the
`BzCompressor` and `BzDecompressor` classes, each stepped over a span of input
and a span of output. `doc/Programming_Manual.md` describes them. A port
written against libbzip2's C API gets the same code as `<bzlib.h>` instead,
through the port kit.
