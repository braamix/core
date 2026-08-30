cmake_minimum_required(VERSION 3.24)
project(braam_portlet LANGUAGES CXX)

if(NOT TARGET braam::proc)
    find_package(braam REQUIRED)
endif()

# PORT is the whole of the opt-in: it links the kit and puts <string.h> and the
# rest on this target's include path, and on no other target's.
braam_add_program(NAME portlet SOURCES portlet.cpp PORT)
