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
# - GUI_FORMS_STX_PACKAGE_TEXT: ON adds FreeType, HarfBuzz, SheenBidi and
#   libunibreak (GUI_FORMS_ENABLE_HARFBUZZ_TEXT);
# - STX_WITH_VORBIS, passed to the stx root as is;
# - the compilers, build type, flags and toolchain file, which Stx.cmake passes
#   from GUI.Forms' own configure, so every object is compiled as it was when
#   GUI.Forms built these sources itself.
#
# Outputs: two install components, each a relocatable prefix of its own.
# - `stx`: the stx libraries (stx_zlib, stx_png, stx_conv with stx_auxlib::simd,
#   stx_rtf2md and, when asked, stx_vorbis) exactly as the stx root installs
#   them. Stx.cmake installs it at <package>/stx, and the SDK carries that
#   prefix whole, as it did before this package existed.
# - `build`: what only the GUI.Forms build links, installed at <package>/build
#   and never copied into the SDK whole: the stx_skia package (stx_skia::stx_skia
#   and stx_skia::swizzle), the stx_text package (stx_text::freetype,
#   stx_text::harfbuzz, stx_text::sheenbidi, stx_text::linebreak, the MD4C
#   objects stx_text::md4c and the libunibreak conformance driver
#   stx_text::linebreak_conformance), the headers of each, and the data the
#   GUI.Forms build and its packages read from the stx source otherwise:
#   share/stx_text/linebreak (the conformance data), share/stx_text/rtf2md_corpus
#   (the markdown corpus test) and share/stx_text/notices (each licence the
#   programs ship, at its path in the stx source; packaging/licenses.json).
#
# The install components follow CMAKE_INSTALL_DEFAULT_COMPONENT_NAME, which
# every install() call without a COMPONENT takes at the point it is made: the
# stx root's calls are made under `stx` and everything below under `build`.
project(gui_forms_stx_package LANGUAGES C CXX)

if(NOT DEFINED GUI_FORMS_STX_SOURCE_DIR OR NOT EXISTS "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/CMakeLists.txt")
    message(FATAL_ERROR "GUI_FORMS_STX_SOURCE_DIR must name an stx checkout")
endif()
option(GUI_FORMS_STX_PACKAGE_SKIA "Build stx_skia whole (OFF: only its red/blue swap kernel)" ON)
option(GUI_FORMS_STX_PACKAGE_TEXT "Build FreeType, HarfBuzz, SheenBidi and libunibreak" ON)
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
# statically (here only the conformance driver).
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
# MD4C 0.6.0 (stx third_party/md4c), the markdown framework's CommonMark
# parser. An object library: GUI.Forms adds its objects to Core, so no archive
# of it is exported from GUI.Forms; its headers, include/md4c/md4c.h and entity.h, stay
# private to GUI.Forms' markdown.cpp.
add_library(stx_text_md4c OBJECT "${_stx_third_party}/md4c/md4c.c" "${_stx_third_party}/md4c/entity.c")
set_target_properties(stx_text_md4c PROPERTIES EXPORT_NAME md4c POSITION_INDEPENDENT_CODE ON)
install(FILES "${_stx_third_party}/md4c/md4c.h" "${_stx_third_party}/md4c/entity.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/md4c)
set(_stx_text_targets stx_text_md4c)

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

    # SheenBidi, the text engine's bidirectional algorithm: its unmodified
    # unity build, compiled through a unit that sends its heap calls to an
    # arena a caller may install, or to the C heap, counted per thread
    # (stx_text_allocation.h, installed beside SheenBidi's headers).
    add_library(stx_text_sheenbidi STATIC "${_stx_text_allocation}/stx_sheenbidi_allocation.c")
    target_include_directories(stx_text_sheenbidi PUBLIC
        $<BUILD_INTERFACE:${_stx_third_party}/sheenbidi/Headers>
        $<BUILD_INTERFACE:${_stx_text_allocation}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_include_directories(stx_text_sheenbidi PRIVATE "${_stx_third_party}/sheenbidi/Source")
    target_compile_definitions(stx_text_sheenbidi PRIVATE SB_CONFIG_UNITY)
    set_target_properties(stx_text_sheenbidi PROPERTIES EXPORT_NAME sheenbidi POSITION_INDEPENDENT_CODE ON)
    install(DIRECTORY "${_stx_third_party}/sheenbidi/Headers/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES "${_stx_text_allocation}/stx_text_allocation.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # UAX #14 line breaking (libunibreak 8.0): the line-break subset the text
    # engine links, and the upstream conformance driver, which also needs the
    # word and grapheme objects (test-only). GUI.Forms registers the driver as
    # gui_forms_linebreak_conformance, run in share/stx_text/linebreak.
    set(_stx_linebreak "${_stx_third_party}/libunibreak/src")
    add_library(stx_text_linebreak STATIC
        "${_stx_linebreak}/unibreakbase.c"
        "${_stx_linebreak}/unibreakdef.c"
        "${_stx_linebreak}/linebreak.c"
        "${_stx_linebreak}/linebreakdata.c"
        "${_stx_linebreak}/linebreakdef.c"
        "${_stx_linebreak}/eastasianwidthdef.c"
        "${_stx_linebreak}/emojidef.c")
    target_include_directories(stx_text_linebreak PUBLIC
        $<BUILD_INTERFACE:${_stx_linebreak}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/libunibreak>)
    set_target_properties(stx_text_linebreak PROPERTIES EXPORT_NAME linebreak POSITION_INDEPENDENT_CODE ON)
    install(FILES "${_stx_linebreak}/linebreak.h" "${_stx_linebreak}/linebreakdef.h"
        "${_stx_linebreak}/unibreakbase.h" "${_stx_linebreak}/unibreakdef.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libunibreak)
    add_executable(stx_text_linebreak_conformance
        "${_stx_linebreak}/tests.c"
        "${_stx_linebreak}/wordbreak.c"
        "${_stx_linebreak}/graphemebreak.c")
    target_link_libraries(stx_text_linebreak_conformance PRIVATE stx_text_linebreak)
    set_target_properties(stx_text_linebreak_conformance PROPERTIES EXPORT_NAME linebreak_conformance)
    install(FILES "${_stx_linebreak}/LineBreakTest.txt" "${_stx_linebreak}/WordBreakTest.txt"
        "${_stx_linebreak}/GraphemeBreakTest.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/stx_text/linebreak)
    list(APPEND _stx_text_targets freetype harfbuzz stx_text_sheenbidi stx_text_linebreak
        stx_text_linebreak_conformance)
endif()

install(TARGETS ${_stx_text_targets} EXPORT stx_textTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    OBJECTS DESTINATION ${CMAKE_INSTALL_LIBDIR}/stx_text_objects)
install(EXPORT stx_textTargets NAMESPACE stx_text:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_text)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stx_textConfig.cmake.in"
    "@PACKAGE_INIT@\n"
    "include(CMakeFindDependencyMacro)\n"
    "find_dependency(Threads)\n"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/stx_textTargets.cmake\")\n"
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
install(FILES "${_stx_third_party}/sheenbidi/LICENSE" DESTINATION ${_stx_notice_root}/third_party/sheenbidi)
install(FILES "${_stx_third_party}/md4c/LICENSE.md" DESTINATION ${_stx_notice_root}/third_party/md4c)
install(FILES "${_stx_third_party}/harfbuzz/COPYING" DESTINATION ${_stx_notice_root}/third_party/harfbuzz)
install(FILES "${_stx_third_party}/freetype/docs/FTL.TXT" DESTINATION ${_stx_notice_root}/third_party/freetype/docs)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_skia/LICENSE" "${GUI_FORMS_STX_SOURCE_DIR}/stx_skia/LICENSE-Skia"
    DESTINATION ${_stx_notice_root}/stx_skia)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE" "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE-puff.txt"
    "${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib/LICENSE-zlib.txt" DESTINATION ${_stx_notice_root}/stx_zlib)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_png/LICENSE" DESTINATION ${_stx_notice_root}/stx_png)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_conv/LICENSE" DESTINATION ${_stx_notice_root}/stx_conv)
install(FILES "${GUI_FORMS_STX_SOURCE_DIR}/stx_rtf2md/LICENSE" DESTINATION ${_stx_notice_root}/stx_rtf2md)
