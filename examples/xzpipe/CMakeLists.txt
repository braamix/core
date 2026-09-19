cmake_minimum_required(VERSION 3.24)

project(braam_xzpipe LANGUAGES CXX)

# As examples/hello: the installed SDK on its own, one more add_subdirectory
# inside Braam's tree.
if(NOT TARGET braam::proc)
    find_package(braam REQUIRED)
endif()

braam_add_program(NAME xzpipe SOURCES xzpipe.cpp LIBS braam::lzma)
