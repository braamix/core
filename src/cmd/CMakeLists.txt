# One binary per program, each its own wasm module (Concept.md §4.1). The recipe
# itself is cmake/BraamProgram.cmake, which the SDK installs — so a program built
# out of tree is built the way these are.

# Two programs have a directory of their own, and the library each builds is
# linked below like any other.
add_subdirectory(pkg)
add_subdirectory(sh)

set(BRAAM_BIN_LIST basename cat chat clear cmp cp curl cut date df diff dirname du echo edit env
                   false fexport fimport find grep head hog less ln ls mkdir mount mv pbcopy
                   pbpaste pkg ps pwd rm seq sh sleep sort spin tail tee test timeout touch tr
                   truncate true uname uniq unzip vmstat watch wc xargs)
set(BRAAM_BIN_PROGRAMS ${BRAAM_BIN_LIST} PARENT_SCOPE)

# Every one of them takes a worker of its own, the shell included. There is
# nothing to ask for: it is what a program is (Concept.md §4).

# /bin/seq is the one program that asks for a library the kernel does not link:
# musl's strtod and printf float engines, over doubles it cannot avoid.
set(BRAAM_BIN_LIB_seq braam::math)

# A program that is more than one translation unit names the library that holds
# the rest of it, and where its own source is when that is not <name>.cpp.
set(BRAAM_BIN_LIB_pkg braam_pkg)
set(BRAAM_BIN_SRC_pkg pkg/main.cpp)

set(BRAAM_BIN_LIB_sh braam_sh)
set(BRAAM_BIN_SRC_sh sh/main.cpp)

# ...or names the extra sources outright. /bin/test is the shell's expression
# evaluator over a proc_main, and linking braam_sh for it would drag the whole
# shell in for two files.
set(BRAAM_BIN_SRC_test test.cpp sh/cond.cpp sh/condrun.cpp)

# /bin/find's -name is the shell's own pattern matcher, which is pure. The one
# file rather than braam_sh, for the same reason.
set(BRAAM_BIN_SRC_find find.cpp sh/match.cpp)

# /bin/diff's comparison and its three formats have no syscall in them, and are
# a directory of their own so the unit suite can compile them in.
set(BRAAM_BIN_SRC_diff diff/main.cpp diff/diffreg.cpp diff/emit.cpp)

# /bin/unzip is the package manager's zip reader over an archive that need not
# be a package. The two files rather than braam_pkg, for the same reason.
set(BRAAM_BIN_SRC_unzip unzip.cpp pkg/zip.cpp pkg/unzip.cpp)

# An undefined LIBS expands to nothing, which is the recipe's default.
foreach(prog IN LISTS BRAAM_BIN_LIST)
    set(src ${BRAAM_BIN_SRC_${prog}})
    if(NOT src)
        set(src ${prog}.cpp)
    endif()
    braam_add_program(NAME ${prog}
                      SOURCES ${src}
                      LIBS ${BRAAM_BIN_LIB_${prog}})
endforeach()
