add_library(braam_fs STATIC
    devfs.cpp
    hostfs.cpp
    opfsfs.cpp
    path.cpp
    vfs.cpp)

target_link_libraries(braam_fs PUBLIC braam_core braam_flags)
