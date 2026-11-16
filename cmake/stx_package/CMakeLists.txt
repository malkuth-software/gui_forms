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
# - GUI_FORMS_STX_PACKAGE_TEXT: ON adds FreeType and HarfBuzz
#   (GUI_FORMS_ENABLE_HARFBUZZ_TEXT);
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
#   and stx_skia::swizzle), the stx_text package (stx_text::freetype and
#   stx_text::harfbuzz, with the text engine; empty without it), the headers
#   of each, and the data the GUI.Forms build and its packages read from the
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
option(GUI_FORMS_STX_PACKAGE_TEXT "Build FreeType and HarfBuzz" ON)
set(_stx_third_party "${GUI_FORMS_STX_SOURCE_DIR}/third_party")
get_filename_component(_stx_gui_cmake "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# As in GUI.Forms' CMakeLists.txt: HarfBuzz caches -pthread, so the first
# configure prefers it too.
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

# --- component `build`: stx_text ---------------------------------------------
set(_stx_text_targets "")

if(GUI_FORMS_STX_PACKAGE_TEXT)
    # FreeType and HarfBuzz with GUI.Forms' options (these were CMakeLists.txt's
    # until the package existed). Their own install rules are skipped; the
    # targets are installed into stx_text's export below.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON)
    add_subdirectory("${_stx_third_party}/freetype" third_party/freetype)
    # Decision 3A: FT_DISABLE_ZLIB only stops a system zlib search; the stock
    # options still compile FreeType's bundled inflate. This options header
    # undefines FT_CONFIG_OPTION_USE_ZLIB for the library's own compile.
    target_include_directories(freetype BEFORE PRIVATE "${_stx_gui_cmake}/freetype")
    target_compile_definitions(freetype PRIVATE "FT_CONFIG_OPTIONS_H=<gui_forms_ftoption.h>")

    set(HB_HAVE_CAIRO OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_CORETEXT OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
    set(HB_HAVE_GRAPHITE2 OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_INTROSPECTION OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
    add_subdirectory("${_stx_third_party}/harfbuzz" third_party/harfbuzz)
    unset(SKIP_INSTALL_ALL)
    # HarfBuzz's heap calls go through counting functions linked into its own
    # archive (cmake/stx_text_allocation/stx_text_allocation.h): the C heap,
    # counted per thread, so the text engine's tests can show a shaping that
    # allocates nothing (paragraph layout's zero-allocation test).
    set(_stx_text_allocation "${_stx_gui_cmake}/stx_text_allocation")
    target_sources(harfbuzz PRIVATE "${_stx_text_allocation}/stx_harfbuzz_allocation.c")
    target_include_directories(harfbuzz PRIVATE "${_stx_text_allocation}")
    target_compile_definitions(harfbuzz PRIVATE
        hb_malloc_impl=stx_text_hb_malloc hb_calloc_impl=stx_text_hb_calloc
        hb_realloc_impl=stx_text_hb_realloc hb_free_impl=stx_text_hb_free)
    # The counters' header, beside HarfBuzz's (the text engine reads them).
    install(FILES "${_stx_text_allocation}/stx_text_allocation.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/harfbuzz)

    # FreeType's headers: the source include tree without its internal headers,
    # then the configured ftconfig.h and ftoption.h the build generated (the
    # text engine compiles against those two as well), at include/freetype2,
    # the directory the target's INSTALL_INTERFACE names.
    get_target_property(_stx_freetype_binary_dir freetype BINARY_DIR)
    install(DIRECTORY "${_stx_third_party}/freetype/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/freetype2
        PATTERN "internal" EXCLUDE
        PATTERN "ftconfig.h" EXCLUDE
        PATTERN "ftoption.h" EXCLUDE)
    install(FILES
        "${_stx_freetype_binary_dir}/include/freetype/config/ftconfig.h"
        "${_stx_freetype_binary_dir}/include/freetype/config/ftoption.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/freetype2/freetype/config)
    # HarfBuzz's public headers: the .h files among the target's sources (its
    # private headers are .hh), plus hb-cplusplus.hh and hb-ft.h (listed
    # upstream only for a system FreeType), at include/harfbuzz.
    get_target_property(_stx_harfbuzz_sources harfbuzz SOURCES)
    set(_stx_harfbuzz_headers
        "${_stx_third_party}/harfbuzz/src/hb-cplusplus.hh"
        "${_stx_third_party}/harfbuzz/src/hb-ft.h")
    foreach(_stx_harfbuzz_source IN LISTS _stx_harfbuzz_sources)
        if(_stx_harfbuzz_source MATCHES "/src/hb[^/]*\\.h$")
            list(APPEND _stx_harfbuzz_headers "${_stx_harfbuzz_source}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _stx_harfbuzz_headers)
    install(FILES ${_stx_harfbuzz_headers} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/harfbuzz)

    list(APPEND _stx_text_targets freetype harfbuzz)
endif()

# Without the text engine the package is empty but still found, so the
# GUI.Forms configure reads one shape.
set(_stx_text_include "")
if(_stx_text_targets)
    install(TARGETS ${_stx_text_targets} EXPORT stx_textTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT stx_textTargets NAMESPACE stx_text:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_text)
    set(_stx_text_include "include(\"\${CMAKE_CURRENT_LIST_DIR}/stx_textTargets.cmake\")\n")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stx_textConfig.cmake.in"
    "@PACKAGE_INIT@\n"
    "include(CMakeFindDependencyMacro)\n"
    "find_dependency(Threads)\n"
    "${_stx_text_include}"
    "check_required_components(stx_text)\n")
configure_package_config_file("${CMAKE_CURRENT_BINARY_DIR}/stx_textConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/stx_textConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_text)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stx_textConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_text)

# The stx source's data the GUI.Forms build and its packages read: the
# markdown corpus, and every licence file packaging/licenses.json names under
# $STX, at its path in the stx source (package_programs.py reads them there
# when no stx checkout is at hand).
install(DIRECTORY "${GUI_FORMS_STX_SOURCE_DIR}/stx_rtf2md/tests/corpus/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/stx_text/rtf2md_corpus)
set(_stx_notice_root ${CMAKE_INSTALL_DATADIR}/stx_text/notices)
install(FILES "${_stx_third_party}/unicode/LICENSE.txt" DESTINATION ${_stx_notice_root}/third_party/unicode)
install(FILES "${_stx_third_party}/harfbuzz/COPYING" DESTINATION ${_stx_notice_root}/third_party/harfbuzz)
install(FILES "${_stx_third_party}/freetype/docs/FTL.TXT" DESTINATION ${_stx_notice_root}/third_party/freetype/docs)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_skia/LICENSE" "${GUI_FORMS_STX_SOURCE_DIR}/stx_skia/LICENSE-Skia"
    DESTINATION ${_stx_notice_root}/stx_skia)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE" "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE-puff.txt"
    "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE-zlib.txt" DESTINATION ${_stx_notice_root}/stx_zlib)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_png/LICENSE" DESTINATION ${_stx_notice_root}/stx_png)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_conv/LICENSE" DESTINATION ${_stx_notice_root}/stx_conv)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_raster3d/LICENSE" DESTINATION ${_stx_notice_root}/stx_raster3d)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_rtf2md/LICENSE" DESTINATION ${_stx_notice_root}/stx_rtf2md)
