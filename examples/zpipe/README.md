# zpipe — compression with braam::zlib

zlib's own `examples/zpipe.c`, rewritten as a Braam program. It deflates stdin
to stdout in the zlib format, or inflates it back with `-d`:

    zpipe <notes.txt >notes.z
    zpipe -d <notes.z >notes.txt

It is built exactly as `hello` is, with the toolchain file the SDK ships:

    cmake -B build --toolchain <prefix>/lib/cmake/braam/wasm32-unknown-unknown.cmake
    cmake --build build

The one difference is the library it asks for. `CMakeLists.txt` says
`LIBS braam::zlib`, and `zpipe.cpp` includes `zlib/zlib.h`: the `Deflater` and
`Inflater` classes, each stepped over a span of input and a span of output.
`doc/Programming_Manual.md` describes them. A port written against zlib's C API
gets the same code as `<zlib.h>` instead, through the port kit.
