# A pure leaf beside braam_zlib and braam_bzip2: no host import, no VFS, and
# the kernel does not link it. The heap is all it reaches for. A program asks
# for it with LIBS braam::lzma; the port kit carries it as <lzma.h>.
#
# liblzma 5.8.4 (0BSD) is vendored under liblzma/ and common/, byte-identical
# to xz's src/liblzma and src/common so a re-sync is a clean diff -- hence -w
# and DisableFormat there. Every .c in them is built. Left upstream:
#   common/stream_encoder_mt.c, stream_decoder_mt.c, outqueue.[ch]  (threads)
#   check/crc*_tablegen.c, crc_clmul_consts_gen.c, lzma/fastpos_tablegen.c,
#   rangecoder/price_tablegen.c                                     (generators)
#   check/crc*_x86.S, crc_x86_clmul.h, crc32_arm64.h, crc32_loongarch.h,
#   crc*_table_be.h, crc*_small.c                                   (other CPUs)
#   */Makefile.inc, api/Makefile.am, *.map, *.rc, *.pc.in
#   and of src/common every file but the nine here.
file(GLOB BRAAM_LZMA_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/liblzma/*/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.c)

add_library(braam_lzma STATIC
    sys.cpp
    xz.cpp
    ${BRAAM_LZMA_SOURCES})

target_link_libraries(braam_lzma PUBLIC braam_flags)

# sys/ answers <config.h>, <stdlib.h>, <string.h> and <assert.h> for the
# vendored sources and nobody else; the rest is clang's freestanding headers.
set(BRAAM_LZMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/liblzma)
target_compile_options(braam_lzma PRIVATE
    -fno-builtin
    $<$<COMPILE_LANGUAGE:C>:-DHAVE_CONFIG_H
                            -I${CMAKE_CURRENT_SOURCE_DIR}/sys
                            -I${BRAAM_LZMA_DIR}/api
                            -I${BRAAM_LZMA_DIR}/common
                            -I${BRAAM_LZMA_DIR}/check
                            -I${BRAAM_LZMA_DIR}/lz
                            -I${BRAAM_LZMA_DIR}/rangecoder
                            -I${BRAAM_LZMA_DIR}/lzma
                            -I${BRAAM_LZMA_DIR}/delta
                            -I${BRAAM_LZMA_DIR}/simple
                            -I${CMAKE_CURRENT_SOURCE_DIR}/common>)

set_source_files_properties(${BRAAM_LZMA_SOURCES} PROPERTIES COMPILE_OPTIONS -w)
