# A pure leaf beside braam_ui: no host import, no allocator, no VFS, and the
# kernel does not link it. musl's libm is vendored under musl/ (MIT), kept
# byte-identical so a re-sync is a clean diff -- hence -w and DisableFormat
# there. C rather than C++: libm.h's asuint64/asdouble are compound literals.
#
# Left upstream: *l.c, nexttoward.c, nexttowardf.c, all long double, which is
# 113-bit quad here and needs compiler-rt. Eight more are native.c's.

# cvt/ is musl's strtod and printf engines, derived rather than verbatim: long
# double is 113-bit quad here, and stdio does not exist. Same -w for the same
# reason.
file(GLOB BRAAM_MUSL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/musl/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cvt/*.c)

add_library(braam_math STATIC
    ftoa.cpp
    native.c
    ${BRAAM_MUSL_SOURCES})

target_link_libraries(braam_math PUBLIC braam_flags)

# -nostdinc: CMAKE_C_FLAGS_INIT carries -nostdlib but not this, and nothing from
# the compiler's headers is included in this tree. shim/ answers <stdint.h>,
# <float.h>, <math.h> and the rest.
target_compile_options(braam_math PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-nostdinc -I${CMAKE_CURRENT_SOURCE_DIR}/musl/shim>)

set_source_files_properties(${BRAAM_MUSL_SOURCES} PROPERTIES COMPILE_OPTIONS -w)
