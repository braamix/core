cmake_minimum_required(VERSION 3.24)

project(braam_zpipe LANGUAGES CXX)

# As examples/hello: the installed SDK on its own, one more add_subdirectory
# inside Braam's tree.
if(NOT TARGET braam::proc)
    find_package(braam REQUIRED)
endif()

braam_add_program(NAME zpipe SOURCES zpipe.cpp LIBS braam::zlib)
