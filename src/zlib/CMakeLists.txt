# A pure leaf beside braam_math and braam_regex: no host import, no VFS, and
# the kernel does not link it. The heap is all it reaches for. A program asks
# for it with LIBS braam::zlib; the port kit carries it as <zlib.h>.
add_library(braam_zlib STATIC check.cpp deflate.cpp inflate.cpp zlib.cpp)

target_link_libraries(braam_zlib PUBLIC braam_flags)

# The window and hash loops are memcpy and memset to the optimiser, which
# would emit calls to a libc that is not here.
target_compile_options(braam_zlib PRIVATE -fno-builtin)
