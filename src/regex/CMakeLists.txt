# A pure leaf beside braam_math and braam_ui: no host import, no VFS, and the
# kernel does not link it. The heap and kernel/text.h's classes are all it
# reaches for. A program asks for it with LIBS braam::regex; the port kit
# carries it for a PORT target, under the name <regex.h>.
add_library(braam_regex STATIC regex.cpp)

target_link_libraries(braam_regex PUBLIC braam_flags)

# str_len's loop is a strlen to the optimiser, which would emit a call to a
# libc that is not here. The explicit __builtin_memcpy and __builtin_memset
# stay inline: -fno-builtin drops the recognition, not the builtins.
target_compile_options(braam_regex PRIVATE -fno-builtin)
