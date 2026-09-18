# A pure leaf beside braam_zlib: no host import, no VFS, and the kernel does
# not link it. The heap is all it reaches for. A program asks for it with
# LIBS braam::bzip2; the port kit carries it as <bzlib.h>.
add_library(braam_bzip2 STATIC
    blocksort.cpp
    bzip2.cpp
    compress.cpp
    decompress.cpp
    huffman.cpp
    tables.cpp)

target_link_libraries(braam_bzip2 PUBLIC braam_flags)

# The sort and move-to-front loops are memcpy and memset to the optimiser,
# which would emit calls to a libc that is not here.
target_compile_options(braam_bzip2 PRIVATE -fno-builtin)
