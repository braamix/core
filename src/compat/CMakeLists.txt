# The opt-in port kit. Nothing in this tree links it: the kernel has no libc,
# src/cmd/ has no libc, and include/ is not on braam::flags' path. A program
# asks for it by name, and one that does not is byte-for-byte unchanged.

# Group A: pure computation. Links braam_flags and braam_math alone — no
# syscall, no host import — which is what lets tests.wasm link it, exactly as
# it does braam_math.
#
# ctime.cpp, cwchar.cpp and cwctype.cpp reference civil()/civil_secs() and the
# UTF-8 codec, which live in braam_proc and braam_core. Those stay undefined in
# this archive and the final link answers them — braam::proc for a program, the
# compiled-in time.cpp and braam_core for tests.wasm — as cenv_intern.cpp's
# heap_alloc already does. All three are pure, so nothing is smuggled in.
#
# cstrtod.cpp is a translation unit of its own: a port naming only strtol must
# not pull musl's __floatscan in behind it.
#
# cmode.cpp is Group B's three decisions that perform no syscall -- the mode
# string, the kind's st_mode and the path's st_ino -- so that the half which
# can be tested is.
add_library(braam_compat_pure STATIC
    calloc.cpp
    cctype.cpp
    cenv_intern.cpp
    cerr.cpp
    cfmt.cpp
    cfnmatch.cpp
    cmem.cpp
    cmisc.cpp
    cmode.cpp
    csort.cpp
    cstring.cpp
    cstrtod.cpp
    cstrtox.cpp
    ctime.cpp
    cwchar.cpp
    cwctype.cpp
    cwidth.cpp)

# PUBLIC: a memcpy compiled with the builtin recognised becomes a call to
# itself, and clang turns a consumer's `while (w[n]) n++` into a wcslen call.
# One flag replaces the 26-45 -fno-builtin-<name> entries five packages carry.
target_compile_options(braam_compat_pure PUBLIC -fno-builtin)

# include/ holds real system header names. It reaches a target that links this
# and nothing else: on braam::flags' path, every native program would suddenly
# resolve #include <string.h>.
target_include_directories(braam_compat_pure INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${BRAAM_INSTALL_INCLUDEDIR}/compat/include>)
target_include_directories(braam_compat_pure PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(braam_compat_pure PUBLIC braam_math braam_flags)

# The half that reaches braam_proc: Group B, which blocks, plus Group A's one
# member that is not pure -- getenv reads the environment block, which is
# braam_proc's. Separate so braam_compat_pure stays linkable into tests.wasm,
# where a reference to braam_proc is a link error.
add_library(braam_compat_proc STATIC
    cdir.cpp
    cenv.cpp
    cfd.cpp
    cio.cpp
    cstat.cpp)
target_compile_options(braam_compat_proc PUBLIC -fno-builtin)
target_include_directories(braam_compat_proc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(braam_compat_proc PUBLIC braam_compat_pure braam_proc)

# The umbrella a program names.
add_library(braam_compat INTERFACE)
target_link_libraries(braam_compat INTERFACE braam_compat_pure braam_compat_proc)

# Upstream keeps its identifiers, its casts and its unused arguments, and
# -Wall -Wextra -Wshadow fires all over code doing nothing wrong. Separate from
# braam_compat: linking a strtol should not switch off -Wshadow. Linked last,
# so these land after braam::flags' warning flags.
add_library(braam_portflags INTERFACE)
target_compile_options(braam_portflags INTERFACE
    -Wno-shadow -Wno-sign-compare -Wno-unused-parameter -Wno-unused-function
    -Wno-unused-but-set-variable -Wno-writable-strings
    -Wno-missing-field-initializers)
