cmake_minimum_required(VERSION 3.24)
project(stx_fontpack_installed_consumer LANGUAGES CXX)

# This fixture has no source-tree include path or library fallback; the
# package config finds stx_zlib by itself.
find_package(stx_fontpack 1.0 CONFIG REQUIRED)
add_executable(stx_fontpack_installed_consumer main.cpp)
set_target_properties(stx_fontpack_installed_consumer PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(stx_fontpack_installed_consumer PRIVATE stx_fontpack::packer)
enable_testing()
add_test(NAME stx_fontpack_installed_consumer COMMAND stx_fontpack_installed_consumer)
# The exported tool runs too: an argument-less call prints its usage and exits 2.
add_test(NAME stx_fontpack_installed_tool COMMAND stx_fontpack::tool)
set_tests_properties(stx_fontpack_installed_tool PROPERTIES WILL_FAIL ON)
