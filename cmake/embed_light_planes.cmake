# Build-time embedding of the shipped light-plane stores
# (include/gui_forms/engine/light_planes/light_planes.hpp,
# shipped_light_plane_store):
#   cmake -DSOURCE=<assets/light_planes> -DNAMES=<name|name|...> -DOUTPUT=<inc>
#         -P embed_light_planes.cmake
# Each store, <SOURCE>/<name>.gflp (made by
# tools/industrial_surfaces/pack_light_planes.py, re-run by the target
# gui_forms_light_planes), becomes one constexpr byte array, and the table
# light_planes.cpp reads names each. A missing store, or one that does not
# begin "GFLP", fails the build, not the program.
cmake_minimum_required(VERSION 3.25)
string(REPLACE "|" ";" NAMES "${NAMES}")
set(arrays "")
set(entries "")
set(count 0)
foreach(name IN LISTS NAMES)
    if(NOT name MATCHES "^[a-z0-9_]+$")
        message(FATAL_ERROR "Light-plane store name is malformed: ${name}")
    endif()
    set(path "${SOURCE}/${name}.gflp")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Shipped light-plane store is missing: ${path} (run the gui_forms_light_planes target)")
    endif()
    file(SIZE "${path}" size)
    if(size LESS 24 OR size GREATER 16777216)
        message(FATAL_ERROR "Light-plane store ${name} has ${size} bytes; 24 bytes to 16 MiB allowed")
    endif()
    file(READ "${path}" hex HEX)
    string(SUBSTRING "${hex}" 0 8 magic)
    if(NOT magic STREQUAL "47464c50")
        message(FATAL_ERROR "Light-plane store ${name} does not begin GFLP")
    endif()
    # Two hex digits a byte, all on one line (CMake's regular expressions
    # have no counted repetition to break it).
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(APPEND arrays "inline constexpr std::array<unsigned char, ${size}> shipped_store_${name}{{\n${bytes}\n}};\n")
    string(APPEND entries "    ShippedStore{\"${name}\", std::span<const unsigned char>(shipped_store_${name})},\n")
    math(EXPR count "${count} + 1")
endforeach()
if(count EQUAL 0)
    message(FATAL_ERROR "No shipped light-plane stores are named")
endif()
set(content "// Generated from assets/light_planes by cmake/embed_light_planes.cmake; do not edit.\n")
string(APPEND content "// Each shipped store's bytes (read_light_plane_store), then the table of them by name.\n")
string(APPEND content "${arrays}")
string(APPEND content "inline constexpr std::array<ShippedStore, ${count}> shipped_stores{{\n${entries}}};\n")
# Rewrite only on a change, so unchanged stores do not rebuild their users.
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" existing)
    if(existing STREQUAL content)
        return()
    endif()
endif()
file(WRITE "${OUTPUT}" "${content}")
