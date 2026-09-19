# xzpipe — compression with braam::lzma

`zpipe`'s twin for xz. It is a Braam program that compresses stdin to stdout in
the `.xz` format, or decompresses it back with `-d`:

    xzpipe <notes.txt >notes.xz
    xzpipe -3 <notes.txt >notes.xz
    xzpipe -d <notes.xz >notes.txt

What it writes is what `xz -T1` makes of the same bytes at the same preset,
since this is the same liblzma. `xz` without `-T1` runs multithreaded and
records sizes in each block header, which xzpipe does not. `-d` reads what
`xz`, `lzma` and `lzip` write, including several streams one after another.
The preset defaults to 6, as `xz`'s does.

Preset 6 needs 93 MiB to compress, and a Braam process may have 100 MiB, so
a program holding much more than this one does would choose a lower preset.
Presets 7 to 9 do not fit, and each preset from 0 to 3 needs 31 MiB or less.
Decompressing needs the dictionary, 8 MiB at preset 6.

It is built exactly as `hello` is, with the toolchain file the SDK ships:

    cmake -B build --toolchain <prefix>/lib/cmake/braam/wasm32-unknown-unknown.cmake
    cmake --build build

The one difference is the library it asks for. `CMakeLists.txt` says
`LIBS braam::lzma`, and `xzpipe.cpp` includes `lzma/xz.h`. That header holds
the `XzEncoder` and `XzDecoder` classes, each stepped over a span of input and
a span of output, and `doc/Programming_Manual.md` describes them. Everything
else in liblzma, such as filter chains, raw streams and the index, is
`lzma/lzma.h`, liblzma's own C API. A port gets that API as `<lzma.h>`
through the port kit.
