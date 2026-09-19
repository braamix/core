# A pure leaf beside braam_zlib, braam_bzip2 and braam_lzma: no host import, no
# VFS, and the kernel does not link it. The heap is all it reaches for. A
# program asks for it with LIBS braam::zstd; the port kit carries it as
# <zstd.h>.
#
# zstd 1.6.0 (BSD) is vendored under lib/, byte-identical to upstream's lib/ so
# a re-sync is a copy -- hence -w and DisableFormat there. Every .c in it is
# built; zstdmt_compress.c, pool.c and threading.c are their single-threaded
# builds, which zstd_compress.c names regardless. Left upstream:
#   dictBuilder/, legacy/, deprecated/, dll/
#   decompress/huf_decompress_amd64.S
#   zdict.h, *.mk, Makefile, BUCK, module.modulemap, libzstd.pc.in
file(GLOB BRAAM_ZSTD_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/*/*.c)

add_library(braam_zstd STATIC
    sys.cpp
    ${BRAAM_ZSTD_SOURCES})

target_link_libraries(braam_zstd PUBLIC braam_flags)

# sys/ answers <string.h> and <stdlib.h> for the vendored sources and nobody
# else; the rest is clang's freestanding headers. DEBUGLEVEL 0 makes assert a
# no-op and keeps stdio out.
target_compile_options(braam_zstd PRIVATE
    -fno-builtin
    $<$<COMPILE_LANGUAGE:C>:-I${CMAKE_CURRENT_SOURCE_DIR}/sys
                            -DDEBUGLEVEL=0
                            -DZSTD_LEGACY_SUPPORT=0
                            -DZSTD_DISABLE_ASM
                            -DZSTD_TRACE=0
                            -DXXH_NO_STDLIB>)

set_source_files_properties(${BRAAM_ZSTD_SOURCES} PROPERTIES COMPILE_OPTIONS -w)
