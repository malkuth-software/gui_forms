cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(stx_fontpack VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(STX_FONTPACK_TESTS_DEFAULT ON)
else()
    set(STX_FONTPACK_TESTS_DEFAULT ${GUI_FORMS_BUILD_TESTS})
endif()
option(STX_FONTPACK_TESTS "Build the font pack tests" ${STX_FONTPACK_TESTS_DEFAULT})
# Install rules and the CMake package export; a consumer that builds the
# package as a subdirectory (GUI.Forms) turns them off.
option(STX_FONTPACK_INSTALL "Install stx_fontpack and export its package" ON)
# A directory of .ttf/.otf files the tests pack as well as their synthetic
# inputs (GUI.Forms passes assets/fonts). Empty: synthetic inputs only.
set(STX_FONTPACK_TEST_FONT_DIR "" CACHE PATH "Fonts the stx_fontpack tests also pack")

# stx_fontpack's only dependency (stx_zlib::crc32). Inside a build that
# already has the sibling target it is used as is; a standalone build finds
# an installed stx_zlib.
if(NOT TARGET stx_zlib::stx_zlib)
    find_package(stx_zlib 0.1 CONFIG REQUIRED)
endif()

set(_stx_fontpack_warnings "")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(_stx_fontpack_warnings -Wall -Wextra -Wpedantic -Wconversion -Wshadow -ffp-contract=off)
elseif(MSVC)
    set(_stx_fontpack_warnings /W4)
endif()

# The loader: what a program links to open an embedded pack.
add_library(stx_fontpack STATIC src/runtime.cpp src/codec_decode.cpp src/loader.cpp)
add_library(stx_fontpack::stx_fontpack ALIAS stx_fontpack)
target_compile_features(stx_fontpack PUBLIC cxx_std_20)
set_target_properties(stx_fontpack PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(stx_fontpack
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(stx_fontpack PRIVATE stx_zlib::stx_zlib)
target_compile_options(stx_fontpack PRIVATE ${_stx_fontpack_warnings})

# The loader's objects again, for a consumer that compiles them into its own
# static library instead of linking (and exporting) this one: GUI.Forms Core,
# whose embedded font pack they open.
add_library(stx_fontpack_objects OBJECT src/runtime.cpp src/codec_decode.cpp src/loader.cpp)
target_compile_features(stx_fontpack_objects PUBLIC cxx_std_20)
set_target_properties(stx_fontpack_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(stx_fontpack_objects
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(stx_fontpack_objects PRIVATE stx_zlib::stx_zlib)
target_compile_options(stx_fontpack_objects PRIVATE ${_stx_fontpack_warnings})

# The packer: build-time coding, for the tool and the tests.
add_library(stx_fontpack_packer STATIC src/codec_encode.cpp src/packer.cpp)
add_library(stx_fontpack::packer ALIAS stx_fontpack_packer)
set_target_properties(stx_fontpack_packer PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME packer)
target_include_directories(stx_fontpack_packer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(stx_fontpack_packer PUBLIC stx_fontpack PRIVATE stx_zlib::stx_zlib)
target_compile_options(stx_fontpack_packer PRIVATE ${_stx_fontpack_warnings})

# The tool a build runs to pack its fonts (README.md). It lends the packer
# std::thread workers, so it alone needs the threads library.
find_package(Threads REQUIRED)
add_executable(stx_fontpack_tool tools/fontpack.cpp)
add_executable(stx_fontpack::tool ALIAS stx_fontpack_tool)
set_target_properties(stx_fontpack_tool PROPERTIES OUTPUT_NAME stx_fontpack EXPORT_NAME tool)
target_link_libraries(stx_fontpack_tool PRIVATE stx_fontpack_packer Threads::Threads)
target_compile_options(stx_fontpack_tool PRIVATE ${_stx_fontpack_warnings})

if(STX_FONTPACK_TESTS)
    enable_testing()
    add_executable(stx_fontpack_tests tests/fontpack_tests.cpp)
    target_include_directories(stx_fontpack_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    target_link_libraries(stx_fontpack_tests PRIVATE stx_fontpack_packer stx_zlib::stx_zlib Threads::Threads)
    target_compile_options(stx_fontpack_tests PRIVATE ${_stx_fontpack_warnings})
    add_test(NAME stx_fontpack_tests COMMAND stx_fontpack_tests)
    if(STX_FONTPACK_TEST_FONT_DIR)
        add_test(NAME stx_fontpack_font_tests COMMAND stx_fontpack_tests "${STX_FONTPACK_TEST_FONT_DIR}")
    endif()
endif()

if(STX_FONTPACK_INSTALL)
    install(TARGETS stx_fontpack stx_fontpack_packer stx_fontpack_tool EXPORT stx_fontpackTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT stx_fontpackTargets NAMESPACE stx_fontpack::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_fontpack)
    configure_package_config_file(cmake/stx_fontpackConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/stx_fontpackConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_fontpack)
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/stx_fontpackConfigVersion.cmake
        VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stx_fontpackConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/stx_fontpackConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_fontpack)
    install(FILES LICENSE PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stx_fontpack)
endif()
