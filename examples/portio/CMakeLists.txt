cmake_minimum_required(VERSION 3.24)
project(braam_portio LANGUAGES CXX)

if(NOT TARGET braam::proc)
    find_package(braam REQUIRED)
endif()

# Group B's worked example, and what links the kit's blocking half: an archive
# member nothing names is a member --gc-sections drops unbuilt.
braam_add_program(NAME portio SOURCES portio.cpp PORT)
