cmake_minimum_required(VERSION 3.25)
# The stx package: everything GUI.Forms builds from the stx source, built once
# per stx commit, toolchain and configuration and then only linked
# (cmake/Stx.cmake builds it at configure time, or consumes one already built
# through GUI_FORMS_STX_PREFIX or a restored GUI_FORMS_STX_PACKAGE_DIR).
#
# Inputs:
# - GUI_FORMS_STX_SOURCE_DIR, an stx checkout (stx.lock's commit, or a local
#   checkout in lockstep development);
# - GUI_FORMS_STX_PACKAGE_SKIA: ON builds stx_skia whole, OFF only its red/blue
#   swap kernel (GUI_FORMS_ENABLE_SKIA=OFF);
# - STX_WITH_VORBIS, passed to the stx root as is;
# - the compilers, build type, flags and toolchain file, which Stx.cmake passes
#   from GUI.Forms' own configure, so every object is compiled as it was when
#   GUI.Forms built these sources itself.
#
# Outputs: two install components, each a relocatable prefix of its own.
# - `stx`: the stx libraries (stx_zlib, stx_png, stx_conv, stx_raster3d, stx_rtf2md and, when
#   asked, stx_vorbis) and STXlib (stxlib::algorithms with its boundary and
#   state, and its licences) exactly as the stx root installs
#   them. Stx.cmake installs it at <package>/stx, and the SDK carries that
#   prefix whole, as it did before this package existed.
# - `build`: what only the GUI.Forms build links, installed at <package>/build
#   and never copied into the SDK whole: the stx_skia package (stx_skia::stx_skia
#   and stx_skia::swizzle), its headers, and the data the GUI.Forms build and its packages read from the
#   stx source otherwise: share/stx_text/rtf2md_corpus (the markdown corpus
#   test) and share/stx_text/notices (each licence the programs ship, at its
#   path in the stx source; packaging/licenses.json). Markdown, bidi, line
#   breaking and script runs are STXlib's (stxlib::markdown and
#   stxlib::textlayout, in stxlib::algorithms) since 2026-11-16.
#
# The install components follow CMAKE_INSTALL_DEFAULT_COMPONENT_NAME, which
# every install() call without a COMPONENT takes at the point it is made: the
# stx root's calls are made under `stx` and everything below under `build`.
project(gui_forms_stx_package LANGUAGES C CXX)

if(NOT DEFINED GUI_FORMS_STX_SOURCE_DIR OR NOT EXISTS "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/CMakeLists.txt")
    message(FATAL_ERROR "GUI_FORMS_STX_SOURCE_DIR must name an stx checkout")
endif()
option(GUI_FORMS_STX_PACKAGE_SKIA "Build stx_skia whole (OFF: only its red/blue swap kernel)" ON)
set(_stx_third_party "${GUI_FORMS_STX_SOURCE_DIR}/third_party")
get_filename_component(_stx_gui_cmake "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# As in GUI.Forms' CMakeLists.txt: the first configure prefers -pthread, so
# a reconfigure does not change it (HarfBuzz cached it, until 2026-11-16).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
# As in GUI.Forms' CMakeLists.txt: Windows executables link their runtimes
# statically (the package builds none today).
if(WIN32 AND NOT MSVC)
    add_link_options(-static)
endif()

# --- component `stx`: the stx root, without stx_skia and without tests -------
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME stx)
set(STX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STX_WITH_SKIA OFF CACHE BOOL "" FORCE)
add_subdirectory("${GUI_FORMS_STX_SOURCE_DIR}" stx)

# --- component `build`: stx_skia ---------------------------------------------
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME build)
set(STX_SKIA_TESTS OFF CACHE BOOL "" FORCE)
set(STX_SKIA_INSTALL ON CACHE BOOL "" FORCE)
if(GUI_FORMS_STX_PACKAGE_SKIA)
    set(STX_SKIA_SWIZZLE_ONLY OFF CACHE BOOL "" FORCE)
else()
    set(STX_SKIA_SWIZZLE_ONLY ON CACHE BOOL "" FORCE)
endif()
add_subdirectory("${GUI_FORMS_STX_SOURCE_DIR}/stx_skia" stx_skia)

# The stx source's data the GUI.Forms build and its packages read: the
# markdown corpus, and every licence file packaging/licenses.json names under
# $STX, at its path in the stx source (package_programs.py reads them there
# when no stx checkout is at hand).
install(DIRECTORY "${GUI_FORMS_STX_SOURCE_DIR}/stx_rtf2md/tests/corpus/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/stx_text/rtf2md_corpus)
set(_stx_notice_root ${CMAKE_INSTALL_DATADIR}/stx_text/notices)
install(FILES "${_stx_third_party}/unicode/LICENSE.txt" DESTINATION ${_stx_notice_root}/third_party/unicode)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_skia/LICENSE" "${GUI_FORMS_STX_SOURCE_DIR}/stx_skia/LICENSE-Skia"
    DESTINATION ${_stx_notice_root}/stx_skia)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE" "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE-puff.txt"
    "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE-zlib.txt" DESTINATION ${_stx_notice_root}/stx_zlib)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_png/LICENSE" DESTINATION ${_stx_notice_root}/stx_png)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_conv/LICENSE" DESTINATION ${_stx_notice_root}/stx_conv)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_raster3d/LICENSE" DESTINATION ${_stx_notice_root}/stx_raster3d)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_rtf2md/LICENSE" DESTINATION ${_stx_notice_root}/stx_rtf2md)
