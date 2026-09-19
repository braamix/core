# zstdpipe — compression with braam::zstd

`zpipe`'s twin for Zstandard. It is a Braam program that compresses stdin to
stdout as a `.zst` frame, or decompresses it back with `-d`:

    zstdpipe <notes.txt >notes.zst
    zstdpipe -19 <notes.txt >notes.zst
    zstdpipe -d <notes.zst >notes.txt

What it writes is a frame `zstd -d` reads. The frame carries a checksum and no
content size, because the size of a stream is not known in advance. `-d` reads
what `zstd` writes, including several frames one after another. The level
defaults to 3, as `zstd`'s does, and goes up to 19.

Level 19 needs 90 MiB to compress, and a Braam process may have 100 MiB, so a
program holding much more than this one does would choose a lower level.
Levels 20 to 22 do not fit, which is why `zstdpipe` stops at 19. Level 3
needs 3.5 MiB. Decompressing needs the window, 8 MiB at level 19.

It is built exactly as `hello` is, with the toolchain file the SDK ships:

    cmake -B build --toolchain <prefix>/lib/cmake/braam/wasm32-unknown-unknown.cmake
    cmake --build build

The one difference is the library it asks for. `CMakeLists.txt` says
`LIBS braam::zstd`, and `zstdpipe.cpp` includes `zstd/zstd.h`, which is
libzstd's own C API: `ZSTD_compressStream2` and `ZSTD_decompressStream`,
stepped over an input cursor and an output cursor. There is no Braam-shaped
wrapper, and `doc/Programming_Manual.md` says what to watch for. A port gets
the same API as `<zstd.h>` through the port kit.
