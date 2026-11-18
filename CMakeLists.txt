cmake_minimum_required(VERSION 3.25)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum supported macOS")
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Supported Apple architecture")
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "GUI.Forms supports Apple silicon arm64 only")
    endif()
endif()
project(GUIForms VERSION 0.6.0 LANGUAGES C CXX)

# The browser target's compiler is the pinned emsdk's clang
# (cmake/emscripten.cmake checks the release); every other target is LLVM 22.1.
if(EMSCRIPTEN)
    if(NOT DEFINED GUI_FORMS_EMSDK_VERSION)
        message(FATAL_ERROR "GUI.Forms' browser target requires cmake/emscripten.cmake as the toolchain file")
    endif()
elseif(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
       NOT CMAKE_CXX_COMPILER_VERSION MATCHES "^22\\.1\\.")
    message(FATAL_ERROR "GUI.Forms requires LLVM 22.1.x; use cmake/llvm22.cmake")
endif()
# Linux builds against libc++ 22 (cmake/llvm22.cmake), like macOS and
# Windows: a tree that would compile against libstdc++ is refused here
# rather than at the first library difference (owner decision, 2026-11-15).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles([=[
#include <version>
#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION < 220000 || _LIBCPP_VERSION >= 230000
#error "not libc++ 22"
#endif
int main() { return 0; }
]=] GUI_FORMS_LINUX_LIBCXX_22)
    if(NOT GUI_FORMS_LINUX_LIBCXX_22)
        # Not cached as a failure: the next configure checks again.
        unset(GUI_FORMS_LINUX_LIBCXX_22 CACHE)
        message(FATAL_ERROR "GUI.Forms on Linux builds against libc++ 22 and libc++abi, linked "
                            "statically: use cmake/llvm22.cmake with GUI_FORMS_LLVM_RUNTIME naming "
                            "the static runtime template_harness/harness/build_linux_runtimes.py builds")
    endif()
endif()
# The browser target builds the library and its web host. The gallery, the
# tests and the form compiler run natively, and the Clang plugin passes load
# into the host's LLVM 22.1, not the emsdk's clang: off unless asked for.
if(EMSCRIPTEN)
    set(GUI_FORMS_BUILD_GALLERY OFF CACHE BOOL "Build the native control gallery")
    set(GUI_FORMS_BUILD_FORM_COMPILER OFF CACHE BOOL "Build and install the form compiler")
    set(GUI_FORMS_LOCALIZABLE_PLUGIN OFF CACHE BOOL "Build the localizable-literal Clang plugin")
    set(GUI_FORMS_EFFECT_VERIFY OFF CACHE BOOL "Verify effect programs with the Clang plugin's effect pass")
endif()
if(APPLE)
    add_compile_options(-Werror=unguarded-availability -Werror=unguarded-availability-new)
endif()
if(WIN32)
    # NOMINMAX: the Windows SDK's minwindef.h defines min and max as macros
    # unless it is set, which breaks std::min and std::max after <windows.h>
    # (MinGW-w64's headers define them for C only).
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX)
endif()
# The Windows targets: MinGW-w64 (MINGW, the MSYS2 CLANG64 toolchain,
# cmake/llvm22.cmake) or the MSVC target, x86_64-pc-windows-msvc, compiled by
# the same clang with its GNU command line (cmake/llvm22-windows-msvc.cmake).
# CMake sets MSVC only for an MSVC-style command line (cl, clang-cl), so the
# MSVC target is recognised by the ABI the compiler simulates.
if(WIN32 AND NOT MINGW AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"
   AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    set(GUI_FORMS_WINDOWS_MSVC_TARGET ON)
else()
    set(GUI_FORMS_WINDOWS_MSVC_TARGET OFF)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The high-integrity configuration settles the options below before they are
# declared, and refuses a configure that contradicts it (cmake/HighIntegrity.cmake).
include(cmake/HighIntegrity.cmake)

option(GUI_FORMS_BUILD_GALLERY "Build the native control gallery" ${PROJECT_IS_TOP_LEVEL})
# The tests live in malkuth-software/gui_forms_tests and build against the
# test cache this tree describes (cmake/TestCache.cmake, tools/test_cache.py).
include(cmake/TestCache.cmake)
# The Gallery model (the control specimen catalogue: gui_forms_gallery_model)
# apart from the programs. The tests read it (the schema export, anatomy
# coverage, the Gallery page, interaction and accessibility tests), so the
# test cache builds it; the programs (GUI_FORMS_BUILD_GALLERY) link it.
option(GUI_FORMS_BUILD_GALLERY_MODEL "Build the Gallery model (the test cache carries it)" ${GUI_FORMS_TEST_CACHE})
option(GUI_FORMS_INSTALL_GALLERY_MODEL "Install the Gallery model as the GalleryModel package component" OFF)
# Checked borrowed views (borrow sanitizer, gui_forms/borrow/borrowed_view/
# borrowed_view.hpp; owner rulings 2026-11-11): the sanitizer tree turns it
# on (tools/ci_build.py --sanitize). It changes the borrowed and lending
# types, so the whole program, consumers included, is built one way: the
# definition is public on gui_forms_core and every target here sees it.
option(GUI_FORMS_CHECKED_BORROWS "Check borrowed views against their owners (the sanitizer tree)" OFF)
# The reference arithmetic (owner, 2026-11-16): ON compiles every first-party
# target with -ffp-contract=off and defines GUI_FORMS_REFERENCE_ARITHMETIC, so
# the raster, engine, material, theme-kernel and effect paths fuse no
# multiply-add and every CPU gives the same bytes. The reference and
# conformance builds turn it on: tools/ci_build.py (tools/toolkit_configure.py
# with tests: the native and sanitizer jobs), the engine gate, the pixel-hash
# tests and the cross-host comparisons. Deployment builds leave it OFF, so the
# compiler contracts as it would by default: the SDK, profiling, and the
# release programs job, which builds what ships and skips the tests labelled
# reference_arithmetic. See "Floating-point contraction" below.
option(GUI_FORMS_REFERENCE_ARITHMETIC
    "Reference arithmetic: no floating-point contraction in first-party code (reference and conformance builds)" OFF)
if(GUI_FORMS_CHECKED_BORROWS)
    add_compile_definitions(GUI_FORMS_CHECKED_BORROWS=1)
endif()

option(GUI_FORMS_BUILD_AUDIO "Build portable PCM audio service" OFF)
set(GUI_FORMS_AUDIO_VORBIS_BACKEND "stx" CACHE STRING "Ogg decoder: stx (default) or stb")
set_property(CACHE GUI_FORMS_AUDIO_VORBIS_BACKEND PROPERTY STRINGS stx stb)
option(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT "Build the development audio loop transport" OFF)
option(GUI_FORMS_ENABLE_SKIA "Build the private CPU-only Skia adapter" ON)
option(GUI_FORMS_ENABLE_MACOS_HOST "Build the native AppKit host" ${APPLE})
option(GUI_FORMS_ENABLE_WINDOWS_HOST "Build the native Win32 host" ${WIN32})
option(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB
       "Enable bounded transactional DIB frames in the Windows development host" OFF)
# A-gf-host-03-F6: the GUI_FORMS_WINDOWS_AUTOMATION variable counts only in
# a program that called host::accept_automation_environment, and only when
# this is on. CI packages the tree it tested, so it stays on there; the
# opt-in keeps a shipped program closed to the variable.
option(GUI_FORMS_WINDOWS_AUTOMATION_ENVIRONMENT
       "Let a Windows program that opts in (host::accept_automation_environment) turn on automation from GUI_FORMS_WINDOWS_AUTOMATION=1" ON)
option(GUI_FORMS_ENABLE_LINUX_HOST "Build the native X11 host with the private CPU renderer" OFF)
option(GUI_FORMS_ENABLE_WAYLAND_PRESENT "Build the Wayland wl_shm presentation path (no Wayland host yet)" OFF)
# The browser host (owner decisions, 2026-11-15): Emscripten only, through
# cmake/emscripten.cmake; WebGL2 is its present backend and nothing else.
option(GUI_FORMS_ENABLE_WEB_HOST "Build the browser host (Emscripten) with the private CPU renderer" ${EMSCRIPTEN})
# ADR-004: the Skia painter, the C drawing API and the Win32 host all draw
# text with the shared text engine (gui_forms_text_engine), which reads
# fonts, shapes, measures and draws with STXlib and is always built.
option(GUI_FORMS_BUILD_FUZZERS "Build libFuzzer entry points" OFF)
option(GUI_FORMS_WARNINGS_AS_ERRORS "Fail the build on any compiler warning in the library and its programs (tools/ci_build.py turns it on)" OFF)
# Stage FD5: the program that generates a form document's C++ header; the SDK
# installs it. A cross build has no host to run it on.
if(CMAKE_CROSSCOMPILING)
    set(GUI_FORMS_FORM_COMPILER_DEFAULT OFF)
else()
    set(GUI_FORMS_FORM_COMPILER_DEFAULT ON)
endif()
option(GUI_FORMS_BUILD_FORM_COMPILER "Build and install the form compiler" ${GUI_FORMS_FORM_COMPILER_DEFAULT})
option(GUI_FORMS_BUILD_BENCHMARKS "Build measurement harnesses" OFF)
option(GUI_FORMS_BUILD_DOCUMENT_VIEW_BENCHMARK "Build the renderer-neutral D1 measurement harness" OFF)
option(GUI_FORMS_BUILD_TEXT_LAYOUT_PROBE "Build the private text shaping diagnostic" OFF)
option(GUI_FORMS_BUILD_LABEL_LAYOUT_PROBE "Build the fixed Label measurement diagnostic" OFF)
option(GUI_FORMS_BUILD_TEXT_WORKER_PROBE "Build the private text worker lifecycle proof" OFF)
option(GUI_FORMS_BUILD_TEXT_PAINT_PROBE "Build the private prepared glyph raster proof" OFF)
option(GUI_FORMS_BUILD_TEXT_ENGINE_BENCHMARK "Build the shared text engine cost benchmark" OFF)
option(GUI_FORMS_BUILD_PAINT_PROFILE "Build the Gallery paint workload that the Linux profiling job samples" OFF)
option(GUI_FORMS_BUILD_BLEND_STUDY "Build the Gallery blend and opaque study workload (tools/blend_study.py)" OFF)
option(GUI_FORMS_BUILD_PREPARED_TEXT "Build the development prepared-text service" OFF)
option(GUI_FORMS_BUILD_TEXT_MASKS "Build the development logical text-mask service" OFF)
option(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS "Instrument private text shaping phases" OFF)
option(GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE "Record private run directions for geometry comparison" OFF)
option(GUI_FORMS_BUILD_FILE_MANAGER_DEMOBOARD
       "Build the deterministic native File Manager first-party consumer" OFF)

# The stx package's third-party builds set THREADS_PREFER_PTHREAD_FLAG and
# cache that -pthread works (HarfBuzz did, until 2026-11-16). Without the
# same preference here, a tree's first configure
# gave Threads::Threads -lpthread and every later configure -pthread, so the
# compile commands of every target using threads changed on reconfigure:
# fresh trees (every CI job) and reconfigured ones never shared an object,
# and CI's second configure of the native tree recompiled what it built.
# Prefer the flag from the first configure on.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
include(GNUInstallDirs)
include(cmake/LocalizablePlugin.cmake)

# Windows (the MSYS2 CLANG64 toolchain, a MinGW-w64 target): every executable
# and DLL this project builds links the LLVM runtimes (libc++, libc++abi,
# libunwind, compiler-rt) and winpthreads statically, so no build output or
# program package imports a runtime DLL, and none can import a GCC runtime
# (libgcc_s, libstdc++). A program is one module (owner, 2026-11-15: every
# independently delivered program holds exactly one copy of any routine):
# the C++ libraries are static archives linked into it, so it holds one copy
# of the runtimes; the C ABI library (cmake/CAbi.cmake) is one module of its
# own for C and C# programs. tools/package_programs.py rejects a package
# that imports a GCC or LLVM runtime DLL, or any DLL but the system's.
if(MINGW)
    add_link_options(-static)
endif()
# Linker pruning for Release programs and the C ABI library, the modules
# that ship (one module each, above). Every function and datum gets its own
# section (ELF and PE; Mach-O's subsections need no flag), and each linked
# module drops the sections nothing reaches from its entry or exports:
# --gc-sections on ELF and PE, -dead_strip on Mach-O. Sections the
# localizable-literal plugin and the effect pass write (.gfloc) are not COMDAT
# and are always kept; the code that remains is unchanged. Identical-code
# folding is left off: it would merge functions whose addresses the code may
# compare, and it saved nothing measurable here. Stripping is the packaging
# step's (tools/package_programs.py strips every shipped binary).
if(NOT MSVC AND NOT EMSCRIPTEN)
    if(NOT APPLE)
        add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    endif()
    set(_gf_linked_module "$<OR:$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>,$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>,$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>>")
    if(APPLE)
        add_link_options($<$<AND:$<CONFIG:Release>,${_gf_linked_module}>:LINKER:-dead_strip>)
        # CMake repeats a static library on a link line for the dependency
        # cycles it resolves; Apple's linker (ld-prime) warns at each repeat,
        # which is no fault of the link.
        add_link_options($<${_gf_linked_module}:LINKER:-no_warn_duplicate_libraries>)
    elseif(GUI_FORMS_WINDOWS_MSVC_TARGET)
        # lld-link's spelling: /OPT:REF drops unreferenced sections, and
        # /OPT:NOICF keeps identical-code folding off, as above.
        add_link_options($<$<AND:$<CONFIG:Release>,${_gf_linked_module}>:LINKER:/OPT:REF>
                         $<$<AND:$<CONFIG:Release>,${_gf_linked_module}>:LINKER:/OPT:NOICF>)
    else()
        add_link_options($<$<AND:$<CONFIG:Release>,${_gf_linked_module}>:LINKER:--gc-sections>)
    endif()
    unset(_gf_linked_module)
endif()
# Link-time optimisation of Release builds where the pinned toolchain links
# with lld, which reads LLVM 22's bitcode: MinGW (CLANG64's lld) always. The
# macOS linker reads bitcode through Xcode's libLTO, not LLVM 22's, and the
# Linux link uses the system linker, so both stay off until their toolchains
# pin an LLVM 22 linker. The archives then hold bitcode, which a consumer
# links with the same pinned toolchain (docs/user/consume-the-sdk.md).
option(GUI_FORMS_LTO "Link Release builds with ThinLTO where the pinned toolchain's linker is lld (MinGW, the MSVC target)" ON)
if(GUI_FORMS_LTO AND (MINGW OR GUI_FORMS_WINDOWS_MSVC_TARGET))
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
include(CMakePackageConfigHelpers)
# Everything from malkuth-software/stx at stx.lock's commit, as one built
# package this build links and never compiles (cmake/Stx.cmake,
# cmake/stx_package/): the stx libraries (stx_zlib, stx_png, stx_conv with
# STXlib's SIMD level, the rich-text library stx_rtf2md of
# notes:planning/stages/RT-rich-text/PLAN.md and, for audio builds, stx_vorbis);
# stx_skia, the CPU Skia derivative the Skia adapters draw with (without the
# adapters only its red/blue swap kernel, stx_skia::swizzle, with which the
# core, the controls and the hosts swap RGBA and BGRA rows). No third-party
# source is compiled (FreeType, the last, left on 2026-11-16). Imported targets' headers are
# system headers here. The package also carries STXlib at stx's stxlib.lock
# commit (stxlib::algorithms; the controls' row height index adapts its
# RowHeightIndex), so stx.lock is the only pin for both.
include(cmake/Stx.cmake)
# stx_fontpack, the font pack codec (stx_fontpack/README.md): its loader, its
# build-time packer and tool, and its tests, which also pack every bundled
# face of assets/fonts. It is written beside the stx package until it moves
# to malkuth-software/stx, links the package's stx_zlib, and installs nothing
# from this build.
set(STX_FONTPACK_TEST_FONT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
set(STX_FONTPACK_INSTALL OFF)
add_subdirectory(stx_fontpack)
include(cmake/FuzzTargets.cmake)

if(GUI_FORMS_ENABLE_MACOS_HOST AND NOT APPLE)
    message(FATAL_ERROR "GUI_FORMS_ENABLE_MACOS_HOST requires Apple AppKit")
endif()

if(GUI_FORMS_ENABLE_WINDOWS_HOST AND NOT WIN32)
    message(FATAL_ERROR "GUI_FORMS_ENABLE_WINDOWS_HOST requires a Windows target")
endif()

if(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB AND NOT GUI_FORMS_ENABLE_WINDOWS_HOST)
    message(FATAL_ERROR "GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB requires the Windows host")
endif()

if(GUI_FORMS_ENABLE_MACOS_HOST AND NOT GUI_FORMS_ENABLE_SKIA)
    message(FATAL_ERROR "The current macOS proving host requires the private Skia adapter")
endif()

# A static archive linked into each program (one module, above); position
# independent, since the C ABI library holds it too.
add_library(gui_forms_threading STATIC
    src/core/threading/worker.cpp)
add_library(GUIForms::Threading ALIAS gui_forms_threading)
set_target_properties(gui_forms_threading PROPERTIES
    EXPORT_NAME Threading OUTPUT_NAME gui_forms_threading POSITION_INDEPENDENT_CODE ON)
target_include_directories(gui_forms_threading PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(gui_forms_threading PUBLIC cxx_std_20)
# Its pool and worker threads are STXlib's (stxlib::AtomicPool and
# stxlib::Thread, the standard augmentation), which end their STXlib thread
# records before they return (worker.cpp).
target_link_libraries(gui_forms_threading PRIVATE Threads::Threads stxlib::boundary stxlib::state)
# GUI_FORMS_THREADING_API (gui_forms/export/threading_export.hpp) marks the
# public declarations; in an archive it adds nothing on Windows, and default
# visibility on ELF and Mach-O, which the C ABI library's export list then
# keeps local. Inline functions are hidden.
set_target_properties(gui_forms_threading PROPERTIES VISIBILITY_INLINES_HIDDEN YES)
if(WIN32)
    target_compile_definitions(gui_forms_threading PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    # MinGW's winpthread; the MSVC target has none, and STXlib's threads are
    # Win32 threads there.
    if(MINGW)
        target_link_libraries(gui_forms_threading PRIVATE winpthread)
    endif()
endif()


# The archives the Application library holds
# whole. They are linked into one module, a program or the C ABI library, so
# GUI_FORMS_API (gui_forms/export/application_export.hpp) adds nothing on
# Windows and default visibility elsewhere, which the C ABI library's export
# list keeps local. Inline functions are hidden: each image compiles its own
# copy, no first-party code compares the address of an inline function
# across images, and a static local of an inline function keeps default
# visibility (Clang's -fvisibility-inlines-hidden-static-local-var is not
# used). Hiding them keeps the many inline definitions out of the C ABI
# library's dynamic symbol table. Every archive is position independent: the C ABI
# library holds them.
function(gui_forms_application_archive target)
    set_target_properties(${target} PROPERTIES VISIBILITY_INLINES_HIDDEN YES POSITION_INDEPENDENT_CODE ON)
endfunction()

# The Application library: a static archive of the runtime's own sources,
# whose usage requirement links the engine's archives whole into the
# consumer (one module: a program, or the C ABI library). Whole, because a
# program reaches the controls and services through registries the archives
# fill at static initialisation, which an ordinary archive link would drop;
# the consumer's --gc-sections or -dead_strip (above) then removes what
# nothing reaches. `archives` are linked whole, `libraries` ordinarily after
# them (the text stack, the stx packages, the system's libraries). The
# whole-archive options name build-tree files, so they are BUILD_INTERFACE
# only: the installed Application is the merged archive
# cmake/ApplicationPackage.cmake writes.
function(gui_forms_application_library target)
    cmake_parse_arguments(PARSE_ARGV 1 _gf "" "" "ARCHIVES;LIBRARIES")
    gui_forms_application_archive(${target})
    gui_forms_application_add_archives(${target} ${_gf_ARCHIVES})
    target_link_libraries(${target} PUBLIC ${_gf_LIBRARIES})
endfunction()

# Adds archives to those `target` (the Application library) links whole into
# its consumer: the whole-archive options for each, and the archive again as
# an ordinary library for its own usage requirements (include directories,
# definitions, its dependencies) and the build order. A member already linked
# whole defines every symbol it could resolve, so the ordinary mention adds
# nothing. GUI_FORMS_APPLICATION_ARCHIVES lists them for the installed,
# merged archive (cmake/ApplicationPackage.cmake).
function(gui_forms_application_add_archives target)
    set(_gf_whole "")
    foreach(_gf_archive IN LISTS ARGN)
        if(APPLE)
            list(APPEND _gf_whole "LINKER:-force_load,$<TARGET_FILE:${_gf_archive}>")
        elseif(GUI_FORMS_WINDOWS_MSVC_TARGET)
            list(APPEND _gf_whole "LINKER:/WHOLEARCHIVE:$<TARGET_FILE:${_gf_archive}>")
        else()
            list(APPEND _gf_whole "$<TARGET_FILE:${_gf_archive}>")
        endif()
        target_link_libraries(${target} PUBLIC "$<BUILD_INTERFACE:${_gf_archive}>")
    endforeach()
    if(NOT APPLE AND NOT GUI_FORMS_WINDOWS_MSVC_TARGET)
        # SHELL: keeps each group whole: CMake removes repeated link options,
        # which would otherwise merge the --whole-archive of two groups.
        string(JOIN " " _gf_group ${_gf_whole})
        set(_gf_whole "SHELL:-Wl,--whole-archive ${_gf_group} -Wl,--no-whole-archive")
    endif()
    target_link_options(${target} INTERFACE "$<BUILD_INTERFACE:${_gf_whole}>")
    set_property(TARGET ${target} APPEND PROPERTY GUI_FORMS_APPLICATION_ARCHIVES ${ARGN})
endfunction()

# The bundled fallback faces (owner ruling 2026-11-11; slimmed 2026-11-16): the nine Noto faces
# and their OFL texts. Japanese and Korean are proper CJK subsets; Chinese, Arabic and the Indic
# scripts are minimal; Hebrew and emoji are the upstream files. tools/fallback_font_subsets.py
# cuts the subsets from the upstream files and documents each character set.
# With GUI_FORMS_INSTALL_FALLBACK_FONTS ON (the default) the embedded font pack carries the faces
# (cmake/FontPack.cmake) and their licences form their own install component, fallback_fonts;
# OFF leaves out both. The runtime works without them: they are optional faces, so text only
# they cover falls back as for any missing face, and bundled_font_diagnostics reports how many the
# pack holds.
#
# No face file is installed (font pack stage 4): the GUI.Forms runtime and GUI.Drawing's raster
# C API read every face from the pack compiled into Core. The install carries the faces' licences
# and the attribution (component fonts, and fallback_fonts for the Noto licences) in
# share/GUIForms/fonts, still GUIForms_FONT_DIR. gui_forms_install_bundled_fonts() is the one
# install rule the hosts use.
option(GUI_FORMS_INSTALL_FALLBACK_FONTS
       "Embed the bundled Noto fallback faces in the font pack and install their licences (install component fallback_fonts)" ON)
set(GUI_FORMS_FALLBACK_FONT_FILES
    NotoSansCJKjp-Subset.otf
    NotoSansCJKkr-Subset.otf
    NotoSansCJKsc-Minimal.otf
    NotoEmoji-Regular.ttf
    NotoSansArabic-Minimal.ttf
    NotoSansHebrew-Regular.ttf
    NotoSansDevanagari-Minimal.ttf
    NotoSansBengali-Minimal.ttf
    NotoSansGurmukhi-Minimal.ttf)
set(GUI_FORMS_FALLBACK_FONT_NOTICES
    OFL-Noto.txt
    OFL-NotoSansCJKjp.txt
    OFL-NotoEmoji.txt)
# What an in-tree program's bundle or folder carries instead of face files: the licences of the
# faces the embedded pack holds and the attribution (macOS bundles in Contents/Resources/fonts,
# the Windows and X11 programs in fonts/ beside them; tools/package_programs.py ships the same).
# The program reads the faces from the pack; a development build reads the checkout's
# assets/fonts directly (GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY), never a copy.
set(GUI_FORMS_PROGRAM_FONT_NOTICES
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/ATTRIBUTION.md
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE-PortsmouthRapids.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Arimo.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Carlito.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Cousine.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Selawik.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Tinos.txt)
if(GUI_FORMS_INSTALL_FALLBACK_FONTS)
    foreach(_gf_name IN LISTS GUI_FORMS_FALLBACK_FONT_NOTICES)
        list(APPEND GUI_FORMS_PROGRAM_FONT_NOTICES ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/${_gf_name})
    endforeach()
endif()
function(gui_forms_install_bundled_fonts)
    set(_gf_font_source ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)
    set(_gf_font_destination ${CMAKE_INSTALL_DATADIR}/GUIForms/fonts)
    set(_gf_excluded)
    set(_gf_fallback)
    foreach(_gf_name IN LISTS GUI_FORMS_FALLBACK_FONT_NOTICES)
        list(APPEND _gf_excluded PATTERN ${_gf_name} EXCLUDE)
        list(APPEND _gf_fallback ${_gf_font_source}/${_gf_name})
    endforeach()
    install(DIRECTORY ${_gf_font_source}/ DESTINATION ${_gf_font_destination}
            COMPONENT fonts FILES_MATCHING
            PATTERN "OFL-*.txt" PATTERN "LICENSE-*.txt" PATTERN "ATTRIBUTION.md" ${_gf_excluded})
    if(GUI_FORMS_INSTALL_FALLBACK_FONTS)
        install(FILES ${_gf_fallback} DESTINATION ${_gf_font_destination} COMPONENT fallback_fonts)
    endif()
endfunction()

# The runtime font set's manifest table (src/core/resources/fonts/bundled_font_set.cpp): one
# record per face of assets/fonts/bundled-fonts.sha256, with its size; regenerated when the
# manifest changes, and the build fails if a face is missing or does not match its digest.
set(GUI_FORMS_BUNDLED_FONT_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/bundled-fonts.sha256)
set(GUI_FORMS_BUNDLED_FONT_MANIFEST_RECORDS
    ${CMAKE_CURRENT_BINARY_DIR}/generated/bundled_fonts/bundled_font_manifest.inc)
add_custom_command(
    OUTPUT ${GUI_FORMS_BUNDLED_FONT_MANIFEST_RECORDS}
    COMMAND ${CMAKE_COMMAND}
        -DMANIFEST=${GUI_FORMS_BUNDLED_FONT_MANIFEST}
        -DFONT_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
        -DOUTPUT=${GUI_FORMS_BUNDLED_FONT_MANIFEST_RECORDS}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_bundled_font_manifest.cmake
    DEPENDS
        ${GUI_FORMS_BUNDLED_FONT_MANIFEST}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_bundled_font_manifest.cmake
    COMMENT "Generating the bundled font manifest records"
    VERBATIM)
include(cmake/FontPack.cmake)

# The stock icon records (src/core/resources/stock_icons/stock_icon_records.hpp):
# gui_forms_stock_icon_generator (tools/stock_icon_generator/, C++20, standard
# library only) converts the committed Heroicons SVG files,
# assets/icons/heroicons/ (assets/icons/heroicons/PROVENANCE.md), into
# stock_icon_data.inc in the build tree, regenerated when an SVG file or the
# generator changes. The build needs no Python: tools/stock_icon_codec.py is
# the generator's test oracle and runs only with the tests. A cross build
# has no host to run the generator it builds, so it names a host-built one in
# GUI_FORMS_STOCK_ICON_GENERATOR.
set(GUI_FORMS_STOCK_ICON_GENERATOR "" CACHE FILEPATH
    "A host-built gui_forms_stock_icon_generator to run in place of the one this build compiles (cross builds)")
if(GUI_FORMS_STOCK_ICON_GENERATOR)
    set(_gf_stock_icon_generator ${GUI_FORMS_STOCK_ICON_GENERATOR})
else()
    add_executable(gui_forms_stock_icon_generator tools/stock_icon_generator/stock_icon_generator.cpp)
    target_compile_features(gui_forms_stock_icon_generator PRIVATE cxx_std_20)
    # Byte-identical to the Python oracle: no fused multiply-adds.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(gui_forms_stock_icon_generator PRIVATE -ffp-contract=off)
    endif()
    # A target name in COMMAND and DEPENDS runs its file and orders the build.
    set(_gf_stock_icon_generator gui_forms_stock_icon_generator)
endif()
set(GUI_FORMS_STOCK_ICON_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/heroicons)
file(GLOB GUI_FORMS_STOCK_ICON_SVGS CONFIGURE_DEPENDS
    ${GUI_FORMS_STOCK_ICON_SOURCE}/24/outline/*.svg
    ${GUI_FORMS_STOCK_ICON_SOURCE}/24/solid/*.svg
    ${GUI_FORMS_STOCK_ICON_SOURCE}/20/solid/*.svg
    ${GUI_FORMS_STOCK_ICON_SOURCE}/16/solid/*.svg)
set(GUI_FORMS_STOCK_ICON_RECORDS ${CMAKE_CURRENT_BINARY_DIR}/generated/stock_icons/stock_icon_data.inc)
add_custom_command(
    OUTPUT ${GUI_FORMS_STOCK_ICON_RECORDS}
    COMMAND ${_gf_stock_icon_generator}
        --source ${GUI_FORMS_STOCK_ICON_SOURCE}
        --output ${GUI_FORMS_STOCK_ICON_RECORDS}
    DEPENDS
        ${GUI_FORMS_STOCK_ICON_SVGS}
        ${_gf_stock_icon_generator}
    COMMENT "Generating the stock icon records from the Heroicons SVG files"
    VERBATIM)

add_library(gui_forms_core STATIC
    src/core/application/application.cpp
    src/core/build_profile/build_profile.cpp
    src/core/application/native_popup.cpp
    src/core/application/transient_window.cpp
    src/core/application/splashscreen.cpp
    src/core/window/native_popup/native_popup_window.cpp
    src/core/window/native_popup/popup_standby.cpp
    src/core/window/native_popup/popup_lift.cpp
    src/core/application/window_frame_drag.cpp
    src/core/application/workspace_transaction.cpp
    src/core/animation/animation_timeline/animation_timeline.cpp
    src/core/animation/animation_timer/animation_timer.cpp
    src/core/binding/collection_cell/collection_cell.cpp
    src/core/binding/property_type/property_type.cpp
    src/core/binding/value/binding_value.cpp
    src/core/binding/property_table/property_table.cpp
    src/core/binding/number_format/number_format.cpp
    src/core/binding/value/property_collection_value/property_collection_value.cpp
    src/core/binding/value/property_object_value/property_object_value.cpp
    src/core/binding/currency_manager/currency_manager.cpp
    src/core/binding/binding_schema/binding_schema.cpp
    src/core/binding/binding_provider/binding_provider.cpp
    src/core/binding/binding_table/binding_table.cpp
    src/core/binding/binding_view/binding_order_tree.cpp
    src/core/binding/binding_view/binding_view.cpp
    src/core/binding/binding_source/binding_source.cpp
    src/core/binding/binding/binding.cpp
    src/core/binding/control_bindings_collection/control_bindings_collection.cpp
    src/core/binding/property_link/property_link.cpp
    src/core/binding/form_binding/form_binding.cpp
    src/core/binding/binding_context/binding_context.cpp
    src/core/types/damage_region/damage_region.cpp
    src/core/types/painter/painter.cpp
    src/core/types/painter/painter_marks.cpp
    src/core/types/paint_marks/paint_marks.cpp
    src/core/types/paint_path/paint_path.cpp
    src/core/scene/spatial_index/spatial_index.cpp
    src/core/scene/scene_view/scene_view.cpp
    src/core/types/paint_recipes/paint_recipes.cpp
    src/core/types/rect/rect.cpp
    src/core/types/linear_light/linear_light.cpp
    src/core/theme/theme/theme.cpp
    src/core/theme/contrast/theme_contrast.cpp
    src/core/theme/dark_variant/dark_variant.cpp
    src/core/theme/familiar_themes/familiar_themes.cpp
    src/core/theme/gui_forms_theme/gui_forms_theme.cpp
    src/core/theme/stock_anatomies/stock_anatomies.cpp
    src/core/theme/theme_overrides/theme_overrides.cpp
    src/core/theme/canvas_look/canvas_look.cpp
    src/core/theme/theme_compiler/theme_compiler.cpp
    src/core/theme/part_painting/part_painting.cpp
    src/core/theme/owner_draw/owner_draw.cpp
    src/core/engine/types/engine_types.cpp
    src/core/engine/engine_math/engine_math.cpp
    src/core/engine/authored_gradient/authored_gradient.cpp
    src/core/engine/engine_expression/engine_expression.cpp
    src/core/engine/primitive_registry/primitive_registry.cpp
    src/core/engine/feature_samples/engine_feature_samples.cpp
    src/core/engine/primitives/bulge/bulge_primitives.cpp
    src/core/engine/primitives/relief/relief_primitives.cpp
    src/core/engine/primitives/material/material_image.cpp
    src/core/engine/primitives/material/image_variant.cpp
    src/core/engine/primitives/material/material_primitive.cpp
    src/core/engine/primitives/material/material_raster.cpp
    src/core/engine/primitives/material/overlay_shadow_primitive.cpp
    src/core/engine/primitives/finish/finish_kernel.cpp
    src/core/engine/primitives/finish/finish_world.cpp
    src/core/engine/light_planes/light_planes.cpp
    src/core/engine/primitives/procedural/procedural_pattern.cpp
    src/core/engine/primitives/light/sheen_primitive.cpp
    src/core/engine/primitives/light/glass_primitive.cpp
    src/core/window/backplane/backplane_composite.cpp
    src/core/window/backdrop/window_backdrop_grant.cpp
    src/core/window/backdrop/backdrop_chain.cpp
    src/core/window/band/window_band.cpp
    src/core/window/chrome/window_chrome.cpp
    src/core/window/backplane/window_backplane.cpp
    src/core/engine/primitives/kernel/box_blur.cpp
    src/core/engine/primitives/kernel/pixel_kernel.cpp
    src/core/engine/primitives/kernel/dither.cpp
    src/core/engine/part_shape/part_shape.cpp
    src/core/engine/part_table/engine_part_table.cpp
    src/core/engine/part_table/slice_assembly.cpp
    src/core/engine/part_raster/part_raster.cpp
    src/core/engine/theme_engine/theme_engine.cpp
    src/core/engine/theme_rules/theme_rules.cpp
    src/core/engine/theme_rules/colour_roles.cpp
    src/core/engine/theme_rules/built_in_rules.cpp
    src/core/engine/motion_value/motion_value.cpp
    src/core/engine/motion_clock/motion_clock.cpp
    src/core/failure/fail_fast.cpp
    src/core/failure/failure_snapshot.cpp
    src/core/borrow/borrowed_view/borrowed_view.cpp
    src/core/drag_engine/drag_engine.cpp
    src/core/drag_engine/drag_tracker.cpp
    src/core/drag_engine/scrub_gesture.cpp
    src/core/engine/effect_canvas/effect_canvas.cpp
    src/core/engine/effect_registry/effect_registry.cpp
    src/core/surface_material/colour_matrix/colour_matrix.cpp
    src/core/surface_material/material_fill_layer/material_fill_layer.cpp
    src/core/surface_material/paint_group_effect/paint_group_effect.cpp
    src/core/surface_material/presets/material_presets.cpp
    src/core/surface_material/types/surface_material_types.cpp
    src/core/text/markdown/markdown.cpp
    src/core/help/help_records/help_records.cpp
    src/core/help/help_contents/help_contents.cpp
    src/core/help/helpbook_section/helpbook_section.cpp
    src/core/help/help_search/help_search.cpp
    src/core/help/help_page/help_page.cpp
    src/core/text/markdown/rtf_conversion.cpp
    src/core/text/text_store/text_store.cpp
    src/core/text/prepared/prepared_storage.cpp
    src/core/text/composite/text_composite.cpp
    src/core/text/composite/clip_mask.cpp
    src/core/text/localizable/localizable.cpp
    src/core/text/locale_file/locale_file.cpp
    src/core/text/paragraph_layout/paragraph_layout.cpp
    src/core/document_view/document_view_state.cpp
    src/core/pages/page_set/page_set.cpp
    src/core/events/key_commands/key_commands.cpp
    src/core/events/key_events/key_events.cpp
    src/core/text_shaping/validation/text_shaping_validation.cpp
    src/core/text/unicode/unicode_grapheme.cpp
    src/core/typography/resolution/typography_resolution.cpp
    src/core/layout/types/layout_types.cpp
    src/core/layout/style_reader/layout_style_reader.cpp
    src/core/layout/flex_solver/flex_solver.cpp
    src/core/layout/grid_solver/grid_solver.cpp
    src/core/layout/grid_span/grid_span.cpp
    src/core/layout/radial_layout/radial_layout.cpp
    src/core/layout/block_flow_solver/block_flow_solver.cpp
    src/core/layout/positioning/positioning.cpp
    src/core/layout/diagnostics/layout_diagnostics.cpp
    src/core/diagnostics/shadow_frames/shadow_frames.cpp
    src/core/diagnostics/blend_study/blend_study.cpp
    src/core/diagnostics/blend_study/count_display_blend.cpp
    src/core/diagnostics/invalidation_overlay/invalidation_overlay.cpp
    src/core/metrics/metrics/metrics.cpp
    src/core/metrics/snapshot/metrics_snapshot.cpp
    src/core/visual_inspection/snapshot/visual_inspection_snapshot.cpp
    src/core/window/inspection/window_visual_inspection.cpp
    src/core/allocation/statistics/allocation_statistics.cpp
    src/core/resources/image_registry/image_registry.cpp
    src/core/resources/stock_icons/stock_icons.cpp
    ${GUI_FORMS_STOCK_ICON_RECORDS}
    src/core/resources/stock_icons/stock_icon_raster.cpp
    src/core/resources/stock_icons/stock_icon_cache.cpp
    src/core/resources/image_atlas/image_atlas.cpp
    src/core/resources/application_resources/application_resources.cpp
    src/runtime/user_themes/theme_files.cpp
    src/runtime/user_themes/user_themes.cpp
    src/core/resources/integrity/sha256.cpp
    src/core/resources/fonts/bundled_font_set.cpp
    src/core/resources/fonts/bundled_font_pack.cpp
    ${GUI_FORMS_FONT_PACK_RECORDS}
    src/core/resources/fonts/app_font_registry.cpp
    src/core/resources/fonts/app_font_validation.cpp
    src/core/resources/fonts/font_family_stacks.cpp
    src/core/resources/fonts/program_typefaces.cpp
    ${GUI_FORMS_BUNDLED_FONT_MANIFEST_RECORDS}
    src/core/live_surface/buffer/live_surface_buffer.cpp
    src/core/live_surface/frame/live_surface_frame.cpp
    src/core/live_surface/pacer/live_surface_pacer.cpp
    src/core/live_surface/surface/live_surface.cpp
    src/core/live_surface/wake_connection/live_surface_wake_connection.cpp
    src/core/live_surface/write_lease/live_surface_write_lease.cpp
    src/core/content/channel/content_channel.cpp
    src/core/content/frame_sink/content_frame_sink.cpp
    src/core/content/editing/content_text_layout.cpp
    src/core/content/hosted/hosted_protocol.cpp
    src/core/content/operation/content_worker.cpp
    src/core/content/types/content_types.cpp
    src/core/content/watchdog/content_watchdog.cpp
    src/core/semantics/snapshot/semantic_snapshot.cpp
    src/core/semantics/types/semantic_types.cpp
    src/core/anatomy/types/anatomy_types.cpp
    src/core/schema/types/schema_types.cpp
    src/core/xml/types/xml_types.cpp
    src/core/xml/xml_characters/xml_characters.cpp
    src/core/xml/xml_reader/xml_reader.cpp
    src/core/xml/xml_writer/xml_writer.cpp
    src/core/theme_language/source_text/theme_source_text.cpp
    src/core/theme_language/diagnostics/theme_diagnostics.cpp
    src/core/theme_language/number/theme_number.cpp
    src/core/theme_language/tokenizer/theme_tokenizer.cpp
    src/core/theme_language/syntax_tree/theme_syntax_tree.cpp
    src/core/theme_language/syntax_tree/theme_sheet_parser.cpp
    src/core/form_language/form_xml/form_xml.cpp
    src/core/form_language/form_model/form_model.cpp
    src/core/control_language/control_expression/control_expression.cpp
    src/core/control_language/control_model/control_model.cpp
    src/core/control_language/control_lock/control_lock.cpp
    src/core/control_language/composite_definition/composite_definition.cpp
    src/core/control_language/control_generator/control_generator.cpp
    src/core/form_language/form_loader/form_loader.cpp
    src/core/form_language/form_loader/form_window_loader.cpp
    src/core/form_language/form_editor/form_editor.cpp
    src/core/form_language/form_generator/form_generator.cpp
    src/core/form_language/form_schema_export/form_schema_export.cpp
    src/core/theme_language/selector/theme_selector.cpp
    src/core/theme_language/custom_properties/theme_custom_properties.cpp
    src/core/theme_language/value/theme_value.cpp
    src/core/theme_language/material/theme_material_lowering.cpp
    src/core/theme_language/motion/theme_motion_lowering.cpp
    src/core/theme_language/media/theme_media_query.cpp
    src/core/theme_language/compiler/theme_compiler.cpp
    src/core/theme_language/package/theme_manifest.cpp
    src/core/theme_language/package/theme_header.cpp
    src/core/theme_language/package/theme_package.cpp
    src/core/json/json_reader/json_reader.cpp
    src/core/json/json_text/json_text.cpp
    src/core/docking/types/dock_types.cpp
    src/core/docking/dock_split_solver/dock_split_solver.cpp
    src/core/docking/dock_layout/dock_layout.cpp
    src/core/docking/dock_layout/dock_layout_edits.cpp
    src/core/docking/dock_layout/dock_layout_model.cpp
    src/core/docking/dock_layout/dock_layout_persistence.cpp
    src/core/docking/dock_drop_targets/dock_drop_targets.cpp
    src/core/host/image/host_image.cpp
    src/core/host/image/host_image_pixels.cpp
    src/core/host/image/clipboard_image_wire.cpp
    src/core/host/image/clipboard_dib.cpp
    src/core/host/image/clipboard_png.cpp
    src/core/image/png_pixels.cpp
    src/core/image/resource_pixels.cpp
    src/core/image/png_color.cpp
    src/core/image/color_conversion.cpp
    src/core/interpolation/interpolation.cpp
    # Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).
    $<$<NOT:$<BOOL:${GUI_FORMS_HIGH_INTEGRITY}>>:src/core/host/frame_capture/frame_capture.cpp>
    src/core/host/compositor/window_backdrop.cpp
    src/core/host/types/host_types.cpp
    src/core/host/types/host_capabilities/host_capabilities.cpp
    src/core/host/types/host_locale/host_locale.cpp
    src/core/host/types/host_services_snapshot/host_services_snapshot.cpp
    src/core/host/types/host_session_snapshot/host_session_snapshot.cpp
    src/core/host/services/host_services.cpp
    src/core/host/presentation/presentation_geometry.cpp
    src/core/host/presentation/presentation_trace.cpp
    src/core/host/presentation/presentation_shim.cpp
    src/core/host/presentation/host_scroll.cpp
    src/core/host/cursor_interaction/cursor_interaction.cpp
    src/core/types/cursor_image/cursor_image.cpp
    src/core/host/session/host_session.cpp
    src/core/host/windowing/desktop_coordinates.cpp
    src/core/host/windowing/host_windowing.cpp
    src/core/host/windowing/first_frame_show.cpp
    src/core/feedback/semantic_feedback/semantic_feedback.cpp
    src/core/display/chunk/display_chunk.cpp
    src/core/display/recording_painter/recording_painter.cpp
    src/core/display/group_effect/group_effect.cpp
    src/core/display/replay/replay_display_chunk.cpp
    src/core/display/snapshot/display_snapshot.cpp
    src/core/display/snapshot/splash_baker.cpp
    src/core/dispatcher/operation/dispatch_operation.cpp
    src/core/dispatcher/state/post_dispatch.cpp
    src/core/thread_records/thread_records_end.cpp
    src/core/dispatcher/window_dispatcher/dispatcher_handle.cpp
    src/core/scheduler/frame_request_token/frame_request_token.cpp
    src/core/scheduler/request/scheduled_frame_request.cpp
    src/core/surface_root/dispatcher/surface_root_dispatcher.cpp
    src/core/surface_root/surface_root.cpp
    src/core/surface_root/sub_surface/sub_surface.cpp
    src/core/surface_root/sub_surface/sub_surface_host.cpp
    src/core/surface_root/sub_surface/sub_surface_tool_tips.cpp
    src/core/surface_root/sub_surface/sub_surface_alerts.cpp
    src/core/surface_root/requests/surface_alerts.cpp
    src/core/window/alerts/window_alerts.cpp
    src/core/surface_root/control_table/control_table.cpp
    src/core/surface_root/control_lookup/control_lookup.cpp
    src/core/window/lifecycle/window_lifecycle.cpp
    src/core/window/typography/window_typography.cpp
    src/core/window/update_scope/update_scope.cpp
    src/core/window/presentation/window_presentation.cpp
    src/core/capture/specimen_states/specimen_states.cpp
    src/core/window/scheduler/window_scheduler.cpp
    src/core/window/engine_inputs/window_engine_inputs.cpp
    src/core/window/engine_motion/window_engine_motion.cpp
    src/core/window/overlay/overlay_token.cpp
    src/core/window/tool_tips/tool_tip_bubble.cpp
    src/core/window/tool_tips/tool_tip_presenter.cpp
    src/core/window/toasts/toast_presenter.cpp
    src/core/window/toasts/window_toasts.cpp
    src/core/window/tool_tips/tool_tip_subjects.cpp
    src/core/window/tool_tips/window_tool_tips.cpp
    src/core/window/interaction/interaction_state.cpp
    src/core/window/interaction/window_interaction.cpp
    src/core/window/relative_pointer/window_relative_pointer.cpp
    src/core/window/accelerator/accelerator_token.cpp
    src/core/control/dispatcher/control_dispatcher.cpp
    src/core/component/component/component.cpp
    src/core/component/component_container/component_container.cpp
    src/core/timer/timer/timer.cpp
    src/core/background_worker/background_worker.cpp
    src/core/control/content_region/content_region.cpp
    src/core/control/active_border/active_border.cpp
    src/core/control/active_border/control_active_border.cpp
    src/core/control/control/control.cpp
    src/core/control/stable_id/stable_id.cpp
    src/core/control/static_tree/control_factory/control_factory.cpp
    src/core/window/window.cpp
    src/core/window/command_route/window_command_route.cpp
    src/core/commands/command_route/command_route.cpp
)
target_link_libraries(gui_forms_core PUBLIC Threads::Threads GUIForms::Threading stx_zlib::stx_zlib stx_png::stx_png)
if(TARGET gui_forms_stx_refresh)
    add_dependencies(gui_forms_core gui_forms_stx_refresh)
endif()
# The interpolation seam's private engine; no stx_conv type is public.
target_link_libraries(gui_forms_core PRIVATE stx_conv::stx_conv)
# rtf_conversion's reader and writer; no stx_rtf2md type is public.
target_link_libraries(gui_forms_core PRIVATE stx_rtf2md::stx_rtf2md)
# Hosted content's parent end (include/gui_forms/content/hosted): POSIX
# processes, shared memory and a socket pair on macOS and Linux; on Windows
# CreateProcessW in a job object, a pagefile-backed section and an
# overlapped named pipe.
if(WIN32)
    target_sources(gui_forms_core PRIVATE src/surface/windows/hosted/hosted_content_windows.cpp)
else()
    target_sources(gui_forms_core PRIVATE src/surface/posix/hosted/hosted_content_posix.cpp)
endif()
# stx_fontpack's loader (stx_fontpack/README.md), which opens the embedded font pack
# (cmake/FontPack.cmake). Its objects are part of Core, so no target joins the
# exports; its headers stay private to the font set.
target_sources(gui_forms_core PRIVATE $<TARGET_OBJECTS:stx_fontpack_objects>)
target_include_directories(gui_forms_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stx_fontpack/include)
if(GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY)
    # The checkout's own faces: development_source_font_directory, offered after GUI_FORMS_FONT_DIR.
    target_compile_definitions(gui_forms_core PRIVATE
        GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
endif()
# A program may link Core beside Application: Application links Core's
# archive whole into the program (gui_forms_application_library), so the
# ordinary mention draws no second copy. The generation-time refusal of the
# combination, which guarded against a static Core beside a shared
# Application, was retired with the shared library (one module, 2026-11-15).
if(GUI_FORMS_CHECKED_BORROWS)
    target_compile_definitions(gui_forms_core PUBLIC GUI_FORMS_CHECKED_BORROWS=1)
endif()
# The high-integrity configuration changes the declarations of the public
# headers (entries are compiled out), so a consumer compiles them the same way
# (cmake/HighIntegrity.cmake).
if(GUI_FORMS_HIGH_INTEGRITY)
    target_compile_definitions(gui_forms_core PUBLIC GUI_FORMS_HIGH_INTEGRITY=1)
    if(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
        target_compile_definitions(gui_forms_core PUBLIC GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT=1)
    endif()
endif()
# The shadow frames, failure snapshots and the fail-fast boundary are
# STXlib's gates and frames (src/core/diagnostics/shadow_frames,
# src/core/failure); their state is stxlib_state's, shared by every module
# that links the core. PUBLIC: the hosts' native entries include
# src/core/failure/fail_fast.hpp, whose call_failing_fast is a gate.
target_link_libraries(gui_forms_core PUBLIC stxlib::boundary stxlib::state)
# The program's typefaces (src/core/resources/fonts/program_typefaces.cpp)
# are STXlib's glyph face (stxlib::glyphs, in stxlib::algorithms); no public
# header includes it. The text rasterizers that include the private header
# link stxlib::algorithms themselves. The markdown framework's CommonMark
# reader is stxlib::markdown, also in stxlib::algorithms; its header stays
# private to markdown.cpp.
target_link_libraries(gui_forms_core PRIVATE stxlib::algorithms)
if(WIN32)
    target_compile_definitions(gui_forms_core PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    if(MINGW)
        target_link_libraries(gui_forms_core PUBLIC winpthread)
    endif()
endif()
target_include_directories(gui_forms_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(gui_forms_core PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated/bundled_fonts
    ${CMAKE_CURRENT_BINARY_DIR}/generated/stock_icons)
target_compile_features(gui_forms_core PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Inspection must account for every retained operation, including optional
    # development profiles. A missing case otherwise becomes a runtime throw.
    set_source_files_properties(src/core/window/inspection/window_visual_inspection.cpp
        PROPERTIES COMPILE_OPTIONS "-Werror=switch")
endif()
set_target_properties(gui_forms_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Sources several targets share, each compiled once (audit I1): an object
# library per source set, whose objects ($<TARGET_OBJECTS:...>) its users
# link. A shared source must read no build switch of its users, so the
# object compiled once is the object each would compile.
# The Windows build switches every Win32 source compiles with.
set(GUI_FORMS_WINDOWS_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
# The sealed font pages of the platform (bundled fonts, item 4): the hosts,
# the bundled font tests and the Skia diagnostics.
if(WIN32)
    add_library(gui_forms_sealed_pages_objects OBJECT src/surface/windows/fonts/sealed_pages_windows.cpp)
    target_compile_definitions(gui_forms_sealed_pages_objects PRIVATE ${GUI_FORMS_WINDOWS_DEFINITIONS})
elseif(EMSCRIPTEN)
    add_library(gui_forms_sealed_pages_objects OBJECT src/host/web/fonts/sealed_pages_web.cpp)
else()
    add_library(gui_forms_sealed_pages_objects OBJECT src/surface/posix/fonts/sealed_pages_posix.cpp)
endif()
target_link_libraries(gui_forms_sealed_pages_objects PRIVATE gui_forms_core)
set_target_properties(gui_forms_sealed_pages_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The hosted program's end (include/gui_forms/content/hosted/hosted_client.hpp):
# what a program started by make_hosted_content links. macOS, Linux and
# Windows.
if(WIN32)
    add_library(gui_forms_hosted_client STATIC src/surface/windows/hosted/hosted_client_windows.cpp)
else()
    add_library(gui_forms_hosted_client STATIC src/surface/posix/hosted/hosted_client_posix.cpp)
endif()
target_link_libraries(gui_forms_hosted_client PUBLIC gui_forms_core)
set_target_properties(gui_forms_hosted_client PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(GUIForms::HostedClient ALIAS gui_forms_hosted_client)
if(WIN32 AND GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    # The transactional DIB frame store: the Win32 host and three tests.
    add_library(gui_forms_dib_frame_store_objects OBJECT src/surface/windows/raster/dib_frame_store.cpp)
    target_include_directories(gui_forms_dib_frame_store_objects PRIVATE include src/surface/windows/raster)
    target_compile_definitions(gui_forms_dib_frame_store_objects PRIVATE ${GUI_FORMS_WINDOWS_DEFINITIONS})
    target_compile_features(gui_forms_dib_frame_store_objects PRIVATE cxx_std_20)
endif()

# The shared text engine: shaping, measurement and glyph coverage are
# STXlib's (stxlib::glyphs); bidi levels, UAX #14 line breaking and script
# runs are stxlib::textlayout's (until 2026-11-16 other libraries did this
# work). Both are in stxlib::algorithms.
add_library(gui_forms_text_engine STATIC
    src/render/text/harfbuzz/harfbuzz_font_engine.cpp
    src/render/text/harfbuzz/bounded_shape_workspace.cpp
    src/render/text/harfbuzz/reusable_shape_workspace.cpp
    src/render/text/glyph_run/glyph_run_raster.cpp
)
target_include_directories(gui_forms_text_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/render/text/harfbuzz
)
# The glyph-run rasterizer draws with STXlib's glyph face; PUBLIC because
# its header carries face_profile_version beside retained coverage.
target_link_libraries(gui_forms_text_engine PUBLIC gui_forms_core stxlib::algorithms)
target_compile_features(gui_forms_text_engine PUBLIC cxx_std_20)
if(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    target_compile_definitions(gui_forms_text_engine PUBLIC GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS=1)
endif()
if(GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE)
    target_compile_definitions(gui_forms_text_engine PUBLIC GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE=1)
endif()
set_target_properties(gui_forms_text_engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# Stage F's metrics differential (tools/font_metrics_differential): the
# bundled engine's measurements of two faces, a GDI oracle on Windows, and
# the comparison under a mapping's tolerance.
add_library(gui_forms_font_metrics_differential STATIC
    tools/font_metrics_differential/differential_inputs.cpp
    tools/font_metrics_differential/engine_measurement.cpp
    tools/font_metrics_differential/differential_compare.cpp
    tools/font_metrics_differential/differential_report.cpp
    tools/font_metrics_differential/metric_snapshot.cpp
)
if(WIN32)
    target_sources(gui_forms_font_metrics_differential PRIVATE
        tools/font_metrics_differential/gdi_measurement_windows.cpp)
    target_link_libraries(gui_forms_font_metrics_differential PRIVATE gdi32)
else()
    target_sources(gui_forms_font_metrics_differential PRIVATE
        tools/font_metrics_differential/gdi_measurement_unavailable.cpp)
endif()
target_include_directories(gui_forms_font_metrics_differential
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tools/font_metrics_differential)
target_link_libraries(gui_forms_font_metrics_differential
    PUBLIC gui_forms_core
    PRIVATE gui_forms_text_engine stxlib::algorithms)
add_executable(gui_forms_font_metrics_differential_tool
    tools/font_metrics_differential/font_metrics_differential_main.cpp)
# The program's import library (a Windows link writes one beside each
# executable, named after its output) keeps the target's own name, so that it
# never overwrites the library of the same output name.
set_target_properties(gui_forms_font_metrics_differential_tool PROPERTIES
    OUTPUT_NAME gui_forms_font_metrics_differential
    ARCHIVE_OUTPUT_NAME gui_forms_font_metrics_differential_tool)
target_link_libraries(gui_forms_font_metrics_differential_tool PRIVATE
    gui_forms_font_metrics_differential)

if(GUI_FORMS_BUILD_PREPARED_TEXT)
    target_sources(gui_forms_core PRIVATE
        src/core/text/prepared_window/prepared_window_input.cpp
        src/core/text/prepared_window/prepared_window_batch.cpp)
    find_package(Threads REQUIRED)
    # Core and Controls compile the same with or without this option: Painter
    # always declares draw_prepared_text, and the retained display list always
    # carries the prepared command, so toggling it adds sources but never
    # recompiles them. Only the renderer and the hosts see a definition.
    add_library(gui_forms_prepared_text_impl STATIC
        src/render/text/prepared_service.cpp
        src/render/text/prepared_window_shape.cpp
        src/render/text/prepared_window_session.cpp
        src/render/text/prepared_raster.cpp)
    target_link_libraries(gui_forms_prepared_text_impl
        PUBLIC gui_forms_core
        PRIVATE gui_forms_text_engine stxlib::algorithms Threads::Threads)
    target_compile_features(gui_forms_prepared_text_impl PUBLIC cxx_std_20)
    set_target_properties(gui_forms_prepared_text_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(GUI_FORMS_BUILD_TEXT_MASKS)
    find_package(Threads REQUIRED)
    add_library(gui_forms_text_masks_impl STATIC
        src/core/text/text_mask/text_mask_budget.cpp
        src/core/text/text_mask/text_mask_storage.cpp
        src/core/text/text_mask/text_mask_queue.cpp
        src/core/text/text_mask/text_mask_service.cpp
        src/render/text/text_mask/text_mask_fonts.cpp
        src/render/text/text_mask/text_mask_shape.cpp
        src/render/text/text_mask/text_mask_raster.cpp
        src/render/text/text_mask/text_mask_layout.cpp)
    target_link_libraries(gui_forms_text_masks_impl
        PUBLIC gui_forms_core
        PRIVATE Threads::Threads stxlib::algorithms)
    target_compile_features(gui_forms_text_masks_impl PUBLIC cxx_std_20)
    set_target_properties(gui_forms_text_masks_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Stage F's font substitution table. Configure validates it and lists the
# faces and licences it names (for the raster module's font directory); the
# build regenerates the C++ records whenever the JSON file changes.
include(cmake/FontSubstitution.cmake)
set(GUI_FORMS_FONT_SUBSTITUTION_TABLE ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/font-substitution.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GUI_FORMS_FONT_SUBSTITUTION_TABLE})
gui_forms_font_substitution_read(
    JSON ${GUI_FORMS_FONT_SUBSTITUTION_TABLE}
    FONT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
    FILES GUI_FORMS_SUBSTITUTION_FONT_FILES
    LICENCES GUI_FORMS_SUBSTITUTION_FONT_LICENCES)
set(GUI_FORMS_FONT_SUBSTITUTION_RECORDS
    ${CMAKE_CURRENT_BINARY_DIR}/generated/font_substitution/font_substitution_table.inc)
add_custom_command(
    OUTPUT ${GUI_FORMS_FONT_SUBSTITUTION_RECORDS}
    COMMAND ${CMAKE_COMMAND}
        -DTABLE=${GUI_FORMS_FONT_SUBSTITUTION_TABLE}
        -DFONT_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts
        -DOUTPUT=${GUI_FORMS_FONT_SUBSTITUTION_RECORDS}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_font_substitution.cmake
    DEPENDS
        ${GUI_FORMS_FONT_SUBSTITUTION_TABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FontSubstitution.cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_font_substitution.cmake
    COMMENT "Generating the font substitution records"
    VERBATIM)

# GUI.Drawing is a renderer-free compatibility substrate.  It deliberately
# remains separate from the retained GUI.Forms core and from every host and
# raster adapter so consumers can use its value/resource semantics alone.
add_library(gui_drawing_core STATIC
    src/core/drawing/bitmap/bitmap.cpp
    src/core/drawing/brush/checker_brush.cpp
    src/core/drawing/brush/conic_gradient_brush.cpp
    src/core/drawing/brush/hatch_brush.cpp
    src/core/drawing/brush/linear_gradient_brush.cpp
    src/core/drawing/brush/path_gradient_brush.cpp
    src/core/drawing/brush/radial_gradient_brush.cpp
    src/core/drawing/brush/solid_brush.cpp
    src/core/drawing/color/color.cpp
    src/core/drawing/font/font.cpp
    src/core/drawing/font_substitution/font_substitution.cpp
    ${GUI_FORMS_FONT_SUBSTITUTION_RECORDS}
    src/core/drawing/geometry/point_i/point_i.cpp
    src/core/drawing/geometry/point_f/point_f.cpp
    src/core/drawing/geometry/rect_i/rect_i.cpp
    src/core/drawing/geometry/rect_f/rect_f.cpp
    src/core/drawing/graphics_path/graphics_path.cpp
    src/core/drawing/graphics_recorder/graphics_recorder.cpp
    src/core/drawing/image_attributes/image_attributes.cpp
    src/core/drawing/image_reference/image_reference.cpp
    src/core/drawing/image_snapshot/image_snapshot.cpp
    src/core/drawing/matrix/matrix.cpp
    src/core/drawing/object/drawing_object.cpp
    src/core/drawing/pen/pen.cpp
    src/core/drawing/region/region.cpp
    src/core/drawing/string_format/string_format.cpp
    src/core/drawing/texture_brush/texture_brush.cpp
    # The Painter's bridge for recorded GUI.Drawing (gd_*) commands, which
    # the Skia executor (gui_drawing_skia) also calls: here, since this
    # target links gui_forms_core (the Painter) beside the recorder.
    src/core/drawing/drawing_replay/drawing_replay.cpp
)
target_include_directories(gui_drawing_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(gui_drawing_core PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated/font_substitution)
target_compile_features(gui_drawing_core PUBLIC cxx_std_20)
# Drawing paths take the engine's trigonometry (engine_math, built into
# gui_forms_core; drawing_support.hpp), so every program that links the
# drawing core links the core too. gui_forms_core builds no drawing source,
# so the link is one way.
target_link_libraries(gui_drawing_core PRIVATE gui_forms_core)
# The public include directory it shares holds headers that include STXlib's
# (stxlib/optional.hpp), so its users get STXlib's include directory too; the
# link adds nothing, since gui_forms_core already links stxlib::boundary.
target_link_libraries(gui_drawing_core PUBLIC stxlib::boundary)
set_target_properties(gui_drawing_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The GUI.Drawing C API (gui_forms/drawing_c_api.h). Its objects ship inside
# the C ABI library (cmake/CAbi.cmake), the one module for C and C#
# programs, which exports gd_get_api_v0 beside the gf_ entry points; the
# static archive made of them is for the in-tree tests, each its own module.
# A program that links the archive defines GD_C_API_STATIC, so the header
# declares the entry point without dllimport.
add_library(gui_drawing_c_api_objects OBJECT
    src/abi/drawing_c_api.cpp
)
if(WIN32)
    target_sources(gui_drawing_c_api_objects PRIVATE src/surface/windows/drawing/drawing_platform_windows.cpp)
else()
    target_sources(gui_drawing_c_api_objects PRIVATE src/abi/drawing_platform_stub.cpp)
endif()
# The thread's last error is an STXlib thread record (stxlib::state's
# registry; stxlib/thread_records.hpp).
target_link_libraries(gui_drawing_c_api_objects PRIVATE gui_drawing_core stxlib::boundary stxlib::state)
target_include_directories(gui_drawing_c_api_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(gui_drawing_c_api_objects PRIVATE GD_C_API_BUILD)
set_target_properties(gui_drawing_c_api_objects PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    POSITION_INDEPENDENT_CODE ON
)
add_library(gui_drawing_c_api STATIC $<TARGET_OBJECTS:gui_drawing_c_api_objects>)
target_link_libraries(gui_drawing_c_api PUBLIC gui_drawing_core stxlib::boundary stxlib::state)
if(WIN32)
    target_link_libraries(gui_drawing_c_api PUBLIC gdi32 user32)
endif()
target_include_directories(gui_drawing_c_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_definitions(gui_drawing_c_api INTERFACE GD_C_API_STATIC)
set_target_properties(gui_drawing_c_api PROPERTIES LINKER_LANGUAGE CXX)
# The drawing ABI and its raster are installed inside the C ABI library
# (cmake/CAbi.cmake: GUIForms::CAbi exports gd_get_api_v0, whose table's
# raster_initialize installs the raster), not as libraries of their own: a C
# or C# program loads one module.
#
# The table's raster_initialize calls gui_drawing_raster_module_initialize
# (src/abi/drawing_raster_service.h), which the raster objects define. A
# program of the archive above without them links this one instead, whose
# raster_initialize fails GD_ERROR_NOT_AVAILABLE; never both.
add_library(gui_drawing_raster_absent STATIC src/abi/drawing_raster_absent.cpp)
target_include_directories(gui_drawing_raster_absent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(gui_drawing_raster_absent PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)

add_library(gui_forms_host_headless STATIC
    src/host/headless/capabilities/headless_capabilities.cpp
    src/host/headless/services/headless_host_services.cpp
    src/host/headless/session/headless_host.cpp
    src/host/headless/application/headless_application.cpp
)
target_include_directories(gui_forms_host_headless PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/headless
)
target_link_libraries(gui_forms_host_headless PUBLIC gui_forms_core)

# The plots package's public primitives (IMGUI_IMPLOT_STUDY B.3): series
# columns, scales, ticks, axes, decimation, colormaps, picking, the view
# model, legend layout, signal programs and the batched mark painters.
# Beside the core and the controls; the controls link it for Sparkline.
add_library(gui_forms_plot_primitives STATIC
    src/plots/axis/axis.cpp
    src/plots/colormap/colormap.cpp
    src/plots/mark_painters/mark_painters.cpp
    src/plots/plot3d_scene/plot3d_scene.cpp
    src/plots/plot_legend/plot_legend.cpp
    src/plots/plot_transform/plot_transform.cpp
    src/plots/plot_view_model/plot_view_model.cpp
    src/plots/scale/scale.cpp
    src/plots/series_picker/series_picker.cpp
    src/plots/series_ring_channel/series_ring_channel.cpp
    src/plots/series_view/series_view.cpp
    src/plots/signal_program/signal_program.cpp
    src/plots/tick_generator/tick_generator.cpp
)
target_include_directories(gui_forms_plot_primitives PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gui_forms_plot_primitives PUBLIC gui_forms_core)
# The 3D scene's private renderer and its CONV surfaces (D5); no stx type is
# public.
target_link_libraries(gui_forms_plot_primitives PRIVATE stx_raster3d::stx_raster3d stx_conv::stx_conv)
set_target_properties(gui_forms_plot_primitives PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The signal programs, verified by the plugin's effect pass like the effect
# programs (make_signal_entry; cmake/LocalizablePlugin.cmake).
gui_forms_verify_effects(gui_forms_plot_primitives
    SOURCES src/plots/signal_program/signal_program.cpp)

add_library(gui_forms_controls STATIC
    src/controls/basic/basic_control_rendering.cpp
    src/controls/connected_controls/connected_controls.cpp
    src/controls/panel/panel.cpp
    src/controls/panel/group_box/group_box.cpp
    src/controls/panel/picture_box/picture_box.cpp
    src/controls/label/label.cpp
    src/controls/shared_property_enums/shared_property_enums.cpp
    src/controls/text_block/text_block.cpp
    src/controls/badge/badge.cpp
    src/controls/indicator_lamp/indicator_lamp.cpp
    src/controls/content_surface/content_surface.cpp
    src/controls/content_text_editor/content_text_editor.cpp
    src/controls/info_banner/info_banner.cpp
    src/controls/empty_state/empty_state.cpp
    src/controls/spinner/spinner.cpp
    src/controls/progress_ring/progress_ring.cpp
    src/controls/progress_ring/ring_dots.cpp
    src/controls/toast/toast.cpp
    src/controls/button_base/button_base.cpp
    src/controls/button_base/button/button.cpp
    src/controls/button_base/button/drop_down_button/drop_down_button.cpp
    src/controls/button_base/check_box/check_box.cpp
    src/controls/button_base/toggle_switch/toggle_switch.cpp
    src/controls/button_base/radio_button/radio_button.cpp
    src/controls/button_base/link_label/link_label.cpp
    src/controls/raster_canvas/raster_canvas.cpp
    src/controls/panel/breadcrumb_trail/breadcrumb_trail.cpp
    src/controls/panel/cut_off_text/cut_off_text_reveal.cpp
    src/controls/panel/tree_view/tree_view.cpp
    src/controls/panel/object_view/object_view.cpp
    src/controls/panel/list_view/list_view.cpp
    src/controls/panel/tree_list_view/tree_list_view.cpp
    src/controls/panel/card/card.cpp
    src/controls/container/master_detail_view/master_detail_view.cpp
    src/controls/collections/expandable_sections.cpp
    src/controls/collections/row_height_index.cpp
    src/controls/collections/column_strip.cpp
    src/controls/virtual_rows_view/virtual_rows_view.cpp
    src/controls/virtual_rows_view/virtual_rows_columns.cpp
    src/controls/window_capsule/window_capsule.cpp
    src/controls/window_capsule/window_capsule_gesture.cpp
    src/controls/collections/choice_group.cpp
    src/controls/collections/command_bar.cpp
    src/controls/commands/command/command.cpp
    src/controls/commands/command_binding/command_binding.cpp
    src/controls/rules/rule/rule.cpp
    src/controls/rules/rule_set/rule_set.cpp
    src/controls/rules/window_rules/window_rules.cpp
    src/controls/menu/context_menu/context_menu.cpp
    src/controls/menu/menu_strip/menu_strip.cpp
    src/controls/tool_strip/tool_strip.cpp
    src/controls/tool_strip/strip_merge.cpp
    src/controls/pages/navigation_pane/navigation_pane.cpp
    src/controls/quick_access_toolbar/quick_access_toolbar.cpp
    src/controls/quick_access_toolbar/ribbon_state.cpp
    src/controls/tool_strip_panel/tool_strip_panel.cpp
    src/controls/tool_strip_container/tool_strip_container.cpp
    src/controls/binding_navigator/binding_navigator.cpp
    src/controls/ribbon/ribbon.cpp
    src/controls/ribbon/ribbon_contextual.cpp
    src/controls/ribbon/ribbon_application_menu.cpp
    src/controls/ribbon/ribbon_file_area.cpp
    src/controls/backstage_view/backstage_view.cpp
    src/controls/ribbon/ribbon_group_overlay.cpp
    src/controls/ribbon/ribbon_scaling.cpp
    src/controls/status_strip/status_strip.cpp
    src/controls/pages/page_strip/page_strip.cpp
    src/controls/pages/page_view/page_view.cpp
    src/controls/docking/dock_navigator/dock_navigator.cpp
    src/controls/docking/dock_workspace/dock_workspace.cpp
    src/controls/docking/dock_site/dock_site.cpp
    src/controls/docking/dock_site/dock_site_drag.cpp
    src/controls/drag_gesture/drag_gesture.cpp
    src/controls/docking/dock_site/dock_site_auto_hide.cpp
    src/controls/data_grid_view/data_grid_view.cpp
    src/controls/data_grid_view/data_grid_view_parts.cpp
    src/controls/data_grid_view/data_grid_view_rows.cpp
    src/controls/data_grid_view/data_grid_view_bound_index.cpp
    src/controls/data_grid_view/data_grid_view_row_heights.cpp
    src/controls/data_grid_view/data_grid_view_row_order.cpp
    src/controls/data_grid_view/data_grid_view_cells.cpp
    src/controls/data_grid_view/data_grid_view_cells_region.cpp
    src/controls/task_dialog/task_dialog.cpp
    src/controls/notify_icon/notify_icon.cpp
    src/controls/file_dialog/file_dialog_support.cpp
    src/controls/file_dialog/file_dialog_types.cpp
    src/controls/open_file_dialog/open_file_dialog.cpp
    src/controls/save_file_dialog/save_file_dialog.cpp
    src/controls/color_dialog/color_dialog.cpp
    src/controls/folder_browser_dialog/folder_browser_dialog.cpp
    src/controls/message_box/message_box.cpp
    src/controls/thread_exception_dialog/thread_exception_dialog.cpp
    src/controls/about_dialog/about_dialog.cpp
    src/controls/business_card/business_card.cpp
    src/controls/business_card/card_turn.cpp
    src/controls/callout/callout.cpp
    src/controls/instruments/dial/dial.cpp
    src/controls/instruments/gauge_base/gauge_base.cpp
    src/controls/instruments/gauge_base/linear_gauge/linear_gauge.cpp
    src/controls/instruments/gauge_base/radial_gauge/radial_gauge.cpp
    src/controls/instruments/seven_segment_display/seven_segment_display.cpp
    src/controls/lookup_combo_box/lookup_combo_box.cpp
    src/controls/modal_layer/modal_layer.cpp
    src/controls/step_indicator/step_indicator.cpp
    src/controls/wizard/wizard.cpp
    src/controls/wizard/wizard_page/wizard_page.cpp
    src/controls/ribbon_gallery/ribbon_gallery.cpp
    src/controls/ribbon_gallery/ribbon_gallery_parts.cpp
    src/controls/ribbon_font_control/ribbon_font_control.cpp
    src/controls/ribbon_color_picker/ribbon_color_picker.cpp
    src/controls/commands/gallery_command/gallery_command.cpp
    src/controls/commands/font_command/font_command.cpp
    src/controls/commands/color_command/color_command.cpp
    src/controls/panel/anchored_overlay_layer/anchored_overlay_placement/anchored_overlay_placement.cpp
    src/controls/panel/anchored_overlay_layer/anchored_overlay_layer.cpp
    src/controls/scrollable_control/container_control/container_control.cpp
    src/controls/scrollable_control/container_control/user_control/user_control.cpp
    src/controls/composite_control/composite_evaluator.cpp
    src/controls/composite_control/composite_control.cpp
    src/controls/composite_control/composite_children.cpp
    src/controls/composite_control/composite_forms.cpp
    src/controls/panel/scaled_panel/scaled_panel.cpp
    src/controls/panel/form_layout/form_layout.cpp
    src/controls/panel/group_box/scaled_group_box/scaled_group_box.cpp
    src/controls/scrollable_control/container_control/flow_layout_panel_compat/flow_layout_panel_compat.cpp
    src/controls/scrollable_control/container_control/flex_panel/flex_panel.cpp
    src/controls/scrollable_control/container_control/grid_panel/grid_panel.cpp
    src/controls/scrollable_control/container_control/block_panel/block_panel.cpp
    src/controls/scrollable_control/container_control/layout_style_property.cpp
    src/controls/scrollable_control/container_control/out_of_flow.cpp
    src/controls/scrollable_control/container_control/table_layout_panel_compat/table_layout_panel_compat.cpp
    src/controls/scrollable_control/container_control/responsive_track_panel/responsive_track_panel.cpp
    src/controls/scrollable_control/container_control/command_overflow_panel/command_overflow_panel.cpp
    src/controls/scrollable_control/container_control/tab_control/tab_page/tab_page.cpp
    src/controls/scrollable_control/container_control/tab_control/tab_control.cpp
    src/controls/scrollable_control/container_control/split_container/splitter_panel/splitter_panel.cpp
    src/controls/scrollable_control/container_control/split_container/splitter_grip.cpp
    src/controls/scrollable_control/container_control/split_container/split_container.cpp
    src/controls/splitter/splitter.cpp
    src/controls/node_canvas/node_graph/node_graph.cpp
    src/controls/node_canvas/node_graph/node_graph_document.cpp
    src/controls/node_canvas/link_router/link_router.cpp
    src/controls/node_canvas/node_canvas.cpp
    src/controls/node_canvas/node_canvas_input.cpp
    src/controls/node_canvas/node_canvas_paint.cpp
    src/controls/panel/date_time_picker/date_time_utilities.cpp
    src/controls/panel/date_time_picker/calendar_grid/calendar_grid.cpp
    src/controls/panel/date_time_picker/date_time_value/date_time_value.cpp
    src/controls/panel/date_time_picker/date_time_format_provider/date_time_format_provider.cpp
    src/controls/panel/date_time_picker/calendar_overlay_layer/calendar_overlay_layer.cpp
    src/controls/panel/date_time_picker/calendar_overlay/calendar_overlay.cpp
    src/controls/panel/date_time_picker/date_time_picker.cpp
    src/controls/panel/month_calendar/month_calendar.cpp
    src/controls/drawing_surface/drawing_surface.cpp
    src/controls/metrics_view/metrics_view.cpp
    src/controls/specimen/stock_specimens/stock_specimens.cpp
    src/controls/panel/text_box_base/text_box_base.cpp
    src/controls/panel/text_box/text_box.cpp
    src/controls/panel/text_editing/text_editing_engine.cpp
    src/controls/panel/rich_text_box/rich_text_box.cpp
    src/controls/panel/rich_text_box/rich_text_box_bar.cpp
    src/controls/panel/rich_text_box/rich_text_box_format.cpp
    src/controls/panel/rich_text_box/rich_text_box_layout.cpp
    src/controls/panel/text_box/text_box_scroll.cpp
    src/controls/panel/text_box/text_box_auto_complete.cpp
    src/controls/panel/text_box/text_box_decorations.cpp
    src/controls/panel/text_box/text_decorations/text_decorations.cpp
    src/controls/panel/text_box/auto_complete/auto_complete.cpp
    src/controls/panel/list_control/list_control.cpp
    src/controls/panel/list_box/list_box.cpp
    src/controls/panel/list_box/checked_list_box/checked_list_box.cpp
    src/controls/panel/combo_box/drop_down_layer.cpp
    src/controls/panel/combo_box/combo_box.cpp
    src/controls/panel/up_down/spin_buttons.cpp
    src/controls/panel/up_down/up_down_base.cpp
    src/controls/panel/domain_up_down/domain_up_down.cpp
    src/controls/panel/token_box/token_box.cpp
    src/controls/search_field/search_field.cpp
    src/controls/panel/masked_text_box/masked_text_provider/masked_text_provider.cpp
    src/controls/panel/masked_text_box/masked_text_box.cpp
    src/controls/panel/numeric_up_down/numeric_up_down.cpp
    src/controls/panel/flags_value_editor/property_editor_drop_down_layer/property_editor_drop_down_layer.cpp
    src/controls/panel/flags_value_editor/flags_value_editor.cpp
    src/controls/panel/font_dialog/font_dialog.cpp
    src/controls/panel/color_value_editor/color_value_editor.cpp
    src/controls/color_picker/color_picker.cpp
    src/controls/inspection/property_row_spec/property_row_spec.cpp
    src/controls/inspection/property_value_converter_registry/property_value_converter_registry.cpp
    src/controls/inspection/property_editor_registry/property_editor_registry.cpp
    src/controls/panel/property_list/property_list.cpp
    src/controls/panel/property_grid/property_grid.cpp
    src/core/resources/image_list/image_list/image_list.cpp
    src/controls/guidance/error_layer/error_layer.cpp
    src/controls/guidance/error_glyph/error_glyph.cpp
    src/controls/guidance/error_provider/error_provider.cpp
    src/controls/guidance/help_provider/help_provider.cpp
    src/controls/help/help_book/help_book.cpp
    src/controls/help/help_book_viewer/help_book_viewer.cpp
    src/controls/help/help_book_system/help_book_system.cpp
    src/controls/help/help_print/help_print.cpp
    src/controls/panel/material_panel/material_panel.cpp
    src/controls/range_control/range_control_rendering.cpp
    src/controls/range_control/progress_bar/progress_bar_effects.cpp
    src/controls/range_control/range_control.cpp
    src/controls/range_control/value_bar/value_text.cpp
    src/controls/range_control/value_bar/value_bar.cpp
    src/controls/range_control/track_bar/track_bar.cpp
    src/controls/range_control/progress_bar/progress_bar.cpp
    src/controls/instruments/sweep_scope/sweep_scope.cpp
    src/controls/instruments/sweep_scope/sweep_scope_effects.cpp
    src/controls/link_flow/link_flow_effects.cpp
    src/controls/interaction_cues/interaction_cue_effects.cpp
    src/controls/instruments/sparkline/sparkline.cpp
    src/controls/plots/plot_axis_item/plot_axis_item.cpp
    src/controls/plots/plot_series_item/plot_series_item.cpp
    src/controls/plots/plot_series_item/plot_series_item_custom.cpp
    src/controls/plots/plot_annotation_item/plot_annotation_item.cpp
    src/controls/plots/plot_view/plot_engine.cpp
    src/controls/plots/plot_view/plot_view.cpp
    src/controls/plots/plot_view/plot_view_live.cpp
    src/controls/plots/plot_view/plot_view_links.cpp
    src/controls/plots/plot3d_view/plot3d_view.cpp
    src/controls/built_in_effect_registry/built_in_effect_registry.cpp
    src/controls/range_control/scroll_bar/scroll_bar.cpp
    src/controls/range_control/scroll_bar/h_scroll_bar/h_scroll_bar.cpp
    src/controls/range_control/scroll_bar/v_scroll_bar/v_scroll_bar.cpp
    src/controls/scrollable_control/scroll_properties/scroll_properties.cpp
    src/controls/scrollable_control/scrollable_control.cpp
    src/controls/ribbon_tool_tip/ribbon_tool_tip_layer/ribbon_tool_tip_layer.cpp
    src/controls/tool_tip/tool_tip.cpp
    src/controls/ribbon_tool_tip/ribbon_tool_tip_bubble.cpp
    src/controls/ribbon_tool_tip/ribbon_tool_tip.cpp
)
target_include_directories(gui_forms_controls PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# STXlib reaches only the controls' private row height index
# (src/controls/collections/row_height_index.hpp); no public header includes
# it, and VirtualRowsView, TreeListView and DataGridView use that adapter only.
target_link_libraries(gui_forms_controls PRIVATE stxlib::algorithms)
# The plots package's controls (PlotView, its items, Sparkline) draw through
# the public plot primitives.
target_link_libraries(gui_forms_controls PUBLIC gui_forms_core gui_drawing_core gui_forms_plot_primitives)
set_target_properties(gui_forms_controls gui_forms_host_headless PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
# The files that register effect programs, verified by the plugin's effect
# pass (cmake/LocalizablePlugin.cmake, gui_forms_verify_effects).
gui_forms_verify_effects(gui_forms_controls
    SOURCES src/controls/range_control/progress_bar/progress_bar_effects.cpp
            src/controls/instruments/sweep_scope/sweep_scope_effects.cpp
            src/controls/link_flow/link_flow_effects.cpp
            src/controls/interaction_cues/interaction_cue_effects.cpp)

# The lab library (audit F9): controls a product uses that are not part of
# the installed library. CorrespondenceView, ReviewCard and InstrumentRack
# (with its private RackModulePanel and the sort kernels, F10) serve the File
# Manager demoboard; their headers are lab/include/gui_forms_lab/<name>.hpp.
# Always built with the tree; installed only with GUI_FORMS_INSTALL_LAB, as
# GUIForms::Lab in its own export set and component. Its sources reach the
# controls library's private helpers (src/controls), so it links that
# library; installed, it names no GUI.Forms library (the SDK's linking rule,
# as for the Gallery model): the program links GUIForms::Application or
# GUIForms::Controls, so a process holds one engine.
option(GUI_FORMS_INSTALL_LAB "Install the optional gui_forms_lab library (GUIForms::Lab)" OFF)
add_library(gui_forms_lab STATIC
    lab/src/correspondence_view.cpp
    lab/src/review_card.cpp
    lab/src/rack_module_panel.cpp
    lab/src/instrument_rack.cpp
)
target_include_directories(gui_forms_lab PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lab/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(gui_forms_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/controls)
target_link_libraries(gui_forms_lab PUBLIC $<BUILD_INTERFACE:gui_forms_controls>)
set_target_properties(gui_forms_lab PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Lab)
add_library(GUIForms::Lab ALIAS gui_forms_lab)
if(GUI_FORMS_INSTALL_LAB)
    install(TARGETS gui_forms_lab EXPORT GUIFormsLabTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT lab)
    install(DIRECTORY lab/include/gui_forms_lab DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT lab)
    install(EXPORT GUIFormsLabTargets
            FILE GUIFormsLabTargets.cmake
            NAMESPACE GUIForms::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms
            COMPONENT lab)
endif()

# The demonstration controls (audit F9): EasingPreview, TypographySpecimen,
# VisualInspectorView and VisualInspectorOverlay, which no product uses. They
# serve the Gallery, the lab programs and their tests; headers are
# demo/lab_controls/include/gui_forms_demo/<name>.hpp. Installed only with
# the Gallery model (GUI_FORMS_INSTALL_GALLERY_MODEL), in the demo_controls
# component, never with the core library; installed, it names no GUI.Forms
# library, like the model.
add_library(gui_forms_demo_controls STATIC
    demo/lab_controls/src/easing_preview.cpp
    demo/lab_controls/src/typography_specimen.cpp
    demo/lab_controls/src/visual_inspector_view.cpp
)
target_include_directories(gui_forms_demo_controls PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/demo/lab_controls/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(gui_forms_demo_controls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/controls)
target_link_libraries(gui_forms_demo_controls PUBLIC $<BUILD_INTERFACE:gui_forms_controls>)
set_target_properties(gui_forms_demo_controls PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME DemoControls)

if(GUI_FORMS_BUILD_FORM_COMPILER)
    # The form compiler (stage FD5): a form document's C++ header, generated
    # with the loader's parser, model and registry schema.
    add_executable(gui_forms_form_compiler
        tools/form_compiler/form_compiler.cpp
        tools/form_compiler/form_catalog.cpp
        tools/form_compiler/form_core_catalog.cpp
    )
    target_link_libraries(gui_forms_form_compiler PRIVATE gui_forms_controls)
    set_target_properties(gui_forms_form_compiler PROPERTIES EXPORT_NAME form_compiler)
    add_executable(GUIForms::form_compiler ALIAS gui_forms_form_compiler)
    # Its own export set, so a build without it installs no reference to it.
    install(TARGETS gui_forms_form_compiler EXPORT GUIFormsFormCompilerTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT GUIFormsFormCompilerTargets
            FILE GUIFormsFormCompilerTargets.cmake
            NAMESPACE GUIForms::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
    # The Gallery's form compiler (audit F9): the installed compiler's
    # program with the lab and demonstration controls as extension types
    # (form_lab_catalog.cpp), for the pages that name them. Never installed,
    # and not built in a high-integrity build, which has no Gallery.
    if(NOT GUI_FORMS_HIGH_INTEGRITY)
    add_executable(gui_forms_form_compiler_lab
        tools/form_compiler/form_compiler.cpp
        tools/form_compiler/form_catalog.cpp
        tools/form_compiler/form_lab_catalog.cpp
    )
    target_link_libraries(gui_forms_form_compiler_lab PRIVATE gui_forms_lab gui_forms_demo_controls)
    endif()
    # The theme check (owner request 2026-11-09): a program's theme packages
    # compiled at build time (gui_forms_add_theme_check), so a bad sheet
    # fails the build, not Application::create. Built and installed with
    # the form compiler, the SDK's other build-time tool.
    add_executable(gui_forms_theme_check tools/theme_check/theme_check.cpp)
    target_link_libraries(gui_forms_theme_check PRIVATE gui_forms_controls)
    set_target_properties(gui_forms_theme_check PROPERTIES EXPORT_NAME theme_check)
    add_executable(GUIForms::theme_check ALIAS gui_forms_theme_check)
    install(TARGETS gui_forms_theme_check EXPORT GUIFormsThemeCheckTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT GUIFormsThemeCheckTargets
            FILE GUIFormsThemeCheckTargets.cmake
            NAMESPACE GUIForms::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
endif()
# gui_forms_add_form(): form documents generated at build time, here and for
# SDK consumers.
include(cmake/GUIFormsForms.cmake)
install(FILES cmake/GUIFormsForms.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
include(cmake/GUIFormsThemes.cmake)
install(FILES cmake/GUIFormsThemes.cmake cmake/GUIFormsThemeEmbed.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)

add_library(GUIForms::Core ALIAS gui_forms_core)
add_library(GUIForms::Controls ALIAS gui_forms_controls)
add_library(GUIForms::Drawing ALIAS gui_drawing_core)
add_library(GUIForms::PlotPrimitives ALIAS gui_forms_plot_primitives)
set_target_properties(gui_forms_core PROPERTIES EXPORT_NAME Core)
set_target_properties(gui_forms_plot_primitives PROPERTIES EXPORT_NAME PlotPrimitives)
set_target_properties(gui_forms_controls PROPERTIES EXPORT_NAME Controls)
set_target_properties(gui_drawing_core PROPERTIES EXPORT_NAME Drawing)

if(GUI_FORMS_BUILD_PREPARED_TEXT)
    install(CODE "message(FATAL_ERROR \"The development prepared-text interface is not admitted for SDK installation\")")
endif()
# The audio loop transport and the text masks install as the AudioLoopTransport
# and TextMasks components (cmake/Audio.cmake, cmake/ApplicationText.cmake),
# with their headers; the main header install below leaves them to those files.
# The core and the controls link the stx package's stx_skia::swizzle privately, so a consumer of
# their static archives links its archive too: it is installed beside them, and GUIFormsConfig
# imports it as stx_skia::swizzle, the name GUIFormsTargets refers to.
install(FILES $<TARGET_FILE:stx_skia::swizzle> DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS gui_forms_threading gui_forms_core gui_forms_controls gui_drawing_core gui_forms_plot_primitives
        EXPORT GUIFormsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/gui_forms
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "prepared_text.hpp" EXCLUDE
        PATTERN "prepared_text" EXCLUDE
        PATTERN "text_mask.hpp" EXCLUDE
        PATTERN "text_mask" EXCLUDE
        PATTERN "loop_transport" EXCLUDE)
# The full consumer-macro shield, generated into the build tree and installed
# beside these headers (cmake/ConsumerShield.cmake).
include(cmake/ConsumerShield.cmake)
# The hosted program's end: installed in GUIFormsTargets as
# GUIForms::HostedClient, the library a program started by
# make_hosted_content links (tests/hosted_client_consumer).
if(TARGET gui_forms_hosted_client)
    set_target_properties(gui_forms_hosted_client PROPERTIES EXPORT_NAME HostedClient)
    install(TARGETS gui_forms_hosted_client
            EXPORT GUIFormsTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(EXPORT GUIFormsTargets
        FILE GUIFormsTargets.cmake
        NAMESPACE GUIForms::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
# `import gui_forms;` (cmake/Module.cmake), when GUI_FORMS_BUILD_MODULE is ON.
include(cmake/Module.cmake)
# The swap kernel's archive name, which GUIFormsConfig imports (the package
# was built in one configuration, so the first one it lists is the one).
get_target_property(gui_forms_swizzle_configurations stx_skia::swizzle IMPORTED_CONFIGURATIONS)
list(GET gui_forms_swizzle_configurations 0 gui_forms_swizzle_configuration)
get_target_property(gui_forms_swizzle_archive stx_skia::swizzle IMPORTED_LOCATION_${gui_forms_swizzle_configuration})
get_filename_component(GUI_FORMS_SWIZZLE_ARCHIVE_NAME "${gui_forms_swizzle_archive}" NAME)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GUIFormsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GUIFormsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/GUIFormsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/GUIFormsConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/GUIFormsConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)

if(GUI_FORMS_BUILD_FILE_MANAGER_DEMOBOARD)
    add_library(gui_forms_file_manager_demoboard_model STATIC
        file_manager_demoboard/src/fixture_model.cpp
        file_manager_demoboard/src/product_window.cpp
        file_manager_demoboard/src/controller_window.cpp
    )
    target_include_directories(gui_forms_file_manager_demoboard_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/file_manager_demoboard/include)
    # CorrespondenceView, ReviewCard and InstrumentRack are the lab
    # library's (audit F9).
    target_link_libraries(gui_forms_file_manager_demoboard_model
        PUBLIC GUIForms::Controls GUIForms::Lab)


    if(GUI_FORMS_ENABLE_MACOS_HOST)
        set_source_files_properties(
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
            PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/fonts")
        add_executable(gui_forms_file_manager_demoboard MACOSX_BUNDLE
            file_manager_demoboard/src/macos_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES})
        target_compile_options(gui_forms_file_manager_demoboard PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_file_manager_demoboard PRIVATE
            gui_forms_file_manager_demoboard_model gui_forms_host_macos)
        set_target_properties(gui_forms_file_manager_demoboard PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "File Manager Demoboard"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.file-manager-demoboard"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "File Manager Demoboard")
    endif()

    if(GUI_FORMS_ENABLE_WINDOWS_HOST)
        add_executable(gui_forms_file_manager_demoboard_windows
            file_manager_demoboard/src/windows_main.cpp)
        target_link_libraries(gui_forms_file_manager_demoboard_windows PRIVATE
            gui_forms_file_manager_demoboard_model gui_forms_host_windows)
        set_target_properties(gui_forms_file_manager_demoboard_windows PROPERTIES
            OUTPUT_NAME "File Manager Demoboard")
        # Its fonts are staged with the other programs' at the end of this file.
    endif()

    if(GUI_FORMS_ENABLE_LINUX_HOST)
        add_executable(gui_forms_file_manager_demoboard_linux
            file_manager_demoboard/src/linux_main.cpp)
        target_link_libraries(gui_forms_file_manager_demoboard_linux PRIVATE
            gui_forms_file_manager_demoboard_model gui_forms_host_linux)
        set_target_properties(gui_forms_file_manager_demoboard_linux PROPERTIES
            OUTPUT_NAME "gui-forms-explore")
        # Its fonts are staged with the other programs' at the end of this file.
    endif()

endif()

if(GUI_FORMS_ENABLE_SKIA)
    add_library(gui_forms_skia STATIC
        src/render/skia/raster/skia_raster.cpp
        src/render/skia/raster/skia_splash_bake.cpp
        # OffscreenRaster's renderer half (capture/offscreen_raster); its
        # font lookup half is src/runtime/offscreen, in the application.
        src/render/skia/offscreen/offscreen_raster.cpp
    )
    target_include_directories(gui_forms_skia
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/render/skia/raster
    )
    target_link_libraries(gui_forms_skia
        PUBLIC gui_forms_core
        PRIVATE stx_skia::stx_skia
    )
    # ADR-004: the Skia painter draws text only through the shared text
    # engine; Skia itself has no font backend.
    target_link_libraries(gui_forms_skia PRIVATE gui_forms_text_engine)
    if(TARGET gui_forms_prepared_text_impl)
        target_sources(gui_forms_skia PRIVATE
            src/render/skia/raster/prepared_skia_frame.cpp)
        target_link_libraries(gui_forms_skia PRIVATE gui_forms_prepared_text_impl)
        # Public so that every includer of skia_raster.hpp (the hosts,
        # Application, the raster tests) sees the same SkiaRaster class.
        target_compile_definitions(gui_forms_skia PUBLIC
            $<BUILD_INTERFACE:GUI_FORMS_PREPARED_TEXT>)
    endif()
    if(APPLE)
        find_library(GUI_FORMS_COREFOUNDATION CoreFoundation REQUIRED)
        find_library(GUI_FORMS_COREGRAPHICS CoreGraphics REQUIRED)
        find_library(GUI_FORMS_CORETEXT CoreText REQUIRED)
        target_link_libraries(gui_forms_skia PRIVATE
            ${GUI_FORMS_COREFOUNDATION}
            ${GUI_FORMS_COREGRAPHICS}
        )
    endif()

    # The drawing executor and the raster both GUI.Drawing raster entries
    # share: the per-thread state (drawing_raster_state.cpp) and the C++
    # seam's execute_recorder (drawing/recorder_raster/recorder_raster.hpp),
    # which the C ABI's raster service calls too. C++ programs take it in
    # the Application archive; the C ABI library links it once.
    add_library(gui_drawing_skia STATIC
        src/render/skia/executor/skia_executor.cpp
        src/render/skia/executor/drawing_raster_state.cpp
        src/render/skia/executor/recorder_raster.cpp
        src/render/skia/executor/shaded_triangle_raster.cpp
    )
    # The installed-face probes are not compiled in a high-integrity build
    # (cmake/HighIntegrity.cmake), whose faces all come from the font pack.
    if(GUI_FORMS_HIGH_INTEGRITY)
    elseif(APPLE)
        # Finds installed families nobody registered (ADR-004 phase 7).
        # CoreText types stay in src/surface/macos/.
        target_sources(gui_drawing_skia PRIVATE
            src/surface/macos/fonts/core_text_font_bytes.cpp)
    elseif(WIN32)
        # The same through GDI and the registry, kept with the Windows host
        # (Win32 types stay in src/surface/windows/).
        target_sources(gui_drawing_skia PRIVATE src/surface/windows/fonts/gdi_font_bytes.cpp)
        set_source_files_properties(src/surface/windows/fonts/gdi_font_bytes.cpp PROPERTIES
            COMPILE_DEFINITIONS "${GUI_FORMS_WINDOWS_DEFINITIONS}")
        target_include_directories(gui_drawing_skia PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/surface/windows/fonts)
    endif()
    target_include_directories(gui_drawing_skia
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/render/skia/executor
    )
    # ADR-004: the C drawing API draws text with the shared text engine and
    # compositor; STXlib's font reader chooses a collection's face by family
    # and weight.
    target_link_libraries(gui_drawing_skia
        PUBLIC gui_drawing_core
        PRIVATE
            stxlib::boundary
            stxlib::state
            gui_forms_text_engine
            gui_forms_core
            stxlib::algorithms
            stx_skia::stx_skia
    )
    if(APPLE)
        target_link_libraries(gui_drawing_skia PRIVATE
            ${GUI_FORMS_COREFOUNDATION}
            ${GUI_FORMS_COREGRAPHICS}
            ${GUI_FORMS_CORETEXT}
        )
    endif()

    # The lamps' and lit keys' baker (skeudoindustrial surfaces), ported from
    # the industrial-surfaces study: an offline tool the integrator runs to
    # make the light-plane bakes, its legends drawn by GUI.Drawing's text
    # engine in the bundled Portsmouth Mono (gui_forms_lamp_keys_legend_tests).
    # The executor decodes GUI.Drawing's bundled faces from Core's font pack into sealed pages
    # (SkiaExecutor::register_bundled_typeface); a program that links gui_drawing_skia without
    # a host also takes the platform's sealed pages. They stay out of gui_drawing_skia itself,
    # which the Windows Application library links whole beside the host's copy.
    add_executable(gui_forms_lamp_keys tools/industrial_surfaces/lamp_keys.cpp
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
    target_link_libraries(gui_forms_lamp_keys PRIVATE gui_drawing_skia gui_forms_core)
    target_compile_definitions(gui_forms_lamp_keys PRIVATE
        GUI_FORMS_LEGEND_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthMono.ttf")

    # The GUI.Drawing raster: its objects ship inside the C ABI library
    # (cmake/CAbi.cmake), whose drawing table's raster_initialize installs
    # it, and hold the drawing executor, the text engine and the Skia
    # derivative once, with the rest of the engine. The static archive made
    # of them is for the in-tree tests; it defines the raster installer the
    # drawing archive calls, so a test lists gui_drawing_c_api before it.
    add_library(gui_drawing_raster_c_api_objects OBJECT
        src/abi/drawing_skia_c_api.cpp
    )
    target_include_directories(gui_drawing_raster_c_api_objects PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/skia/executor
    )
    target_link_libraries(gui_drawing_raster_c_api_objects PRIVATE gui_drawing_skia gui_drawing_c_api_objects)
    # The thread's executor and font registrations are an STXlib thread
    # record (stxlib/thread_records.hpp).
    target_link_libraries(gui_drawing_raster_c_api_objects PRIVATE stxlib::boundary stxlib::state)
    set_target_properties(gui_drawing_raster_c_api_objects PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        POSITION_INDEPENDENT_CODE ON
    )
    add_library(gui_drawing_raster_c_api STATIC
        $<TARGET_OBJECTS:gui_drawing_raster_c_api_objects>
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
    target_link_libraries(gui_drawing_raster_c_api PUBLIC gui_drawing_skia gui_drawing_c_api)
    if(WIN32)
        target_link_libraries(gui_drawing_raster_c_api PUBLIC advapi32)
    endif()
    target_include_directories(gui_drawing_raster_c_api INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    set_target_properties(gui_drawing_raster_c_api PROPERTIES LINKER_LANGUAGE CXX)
    # The licences of the faces the substitution table names, and the
    # attribution, in the fonts directory beside the module that holds
    # the raster (stage F): the C ABI library once installed
    # (cmake/CAbi.cmake), the test program in the build. The faces themselves
    # come from Core's embedded font pack (font pack stage 4), so none is
    # copied.
    set(gui_drawing_raster_fonts
        ${GUI_FORMS_SUBSTITUTION_FONT_LICENCES}
        ATTRIBUTION.md)
    list(TRANSFORM gui_drawing_raster_fonts PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/)
endif()

if(GUI_FORMS_ENABLE_MACOS_HOST)
    enable_language(OBJCXX)
    find_library(GUI_FORMS_APPKIT AppKit REQUIRED)
    find_library(GUI_FORMS_QUARTZCORE QuartzCore REQUIRED)
    find_library(GUI_FORMS_IOSURFACE IOSurface REQUIRED)
    find_library(GUI_FORMS_COREGRAPHICS CoreGraphics REQUIRED)
    find_library(GUI_FORMS_UNIFORMTYPEIDENTIFIERS UniformTypeIdentifiers REQUIRED)
    find_library(GUI_FORMS_COREFOUNDATION CoreFoundation REQUIRED)
    add_library(gui_forms_host_macos STATIC
        src/surface/macos/application/macos_host.mm
        src/surface/macos/application/macos_application_run.mm
        src/surface/macos/application/macos_window_delegates.mm
        src/surface/macos/application/macos_window_requests.mm
        src/surface/macos/application/macos_main_work.mm
        src/surface/macos/application/macos_values.mm
        src/surface/macos/application/macos_bundled_fonts.mm
        src/surface/macos/application/macos_splashscreen.mm
        src/host/macos/application/live_surface_damage.cpp
        src/surface/macos/view/macos_view.mm
        src/surface/macos/view/macos_view_scheduling.mm
        src/surface/macos/view/macos_view_drawing.mm
        src/surface/macos/view/macos_iosurface_present.mm
        src/surface/macos/view/macos_view_input.mm
        src/surface/macos/view/macos_view_accessibility.mm
        src/surface/macos/view/macos_view_diagnostics.mm
        src/surface/macos/accessibility/macos_accessibility_element.mm
        src/surface/macos/accessibility/macos_accessibility_text.mm
        src/surface/macos/application/application_menu.mm
        src/surface/macos/services/appkit_host_services.mm
        src/surface/macos/services/appkit_clipboard_image.mm
        src/surface/macos/services/appkit_status_items.mm
        src/surface/macos/compositor/macos_compositor.mm
        src/host/macos/locale/icu_date_pattern.cpp
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
    )
    # The user's locale (CoreFoundation), which a high-integrity build does
    # not read (cmake/HighIntegrity.cmake).
    if(NOT GUI_FORMS_HIGH_INTEGRITY)
        target_sources(gui_forms_host_macos PRIVATE src/surface/macos/locale/macos_locale.cpp)
    endif()
    target_include_directories(gui_forms_host_macos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/macos
    )
    target_compile_options(gui_forms_host_macos PRIVATE -fobjc-arc)
    target_link_libraries(gui_forms_host_macos
        PUBLIC gui_forms_core gui_forms_skia
        PRIVATE ${GUI_FORMS_APPKIT} ${GUI_FORMS_QUARTZCORE} ${GUI_FORMS_IOSURFACE}
                ${GUI_FORMS_COREGRAPHICS}
                ${GUI_FORMS_UNIFORMTYPEIDENTIFIERS}
                ${GUI_FORMS_COREFOUNDATION}
    )

    # The Application library (gui_forms_application_library above): the
    # runtime's sources in a static archive, and the engine's archives linked
    # whole into the consumer, a program or the C ABI library.
    add_library(gui_forms_application STATIC
        src/runtime/application_bundle.cpp
        src/runtime/application/application.cpp
        src/runtime/offscreen/offscreen_raster_create.cpp
        src/runtime/stxlib_state_probe.cpp
    )
    add_library(GUIForms::Application ALIAS gui_forms_application)
    set_target_properties(gui_forms_application PROPERTIES
        EXPORT_NAME Application
        OUTPUT_NAME gui_forms_application
    )
    target_include_directories(gui_forms_application PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_features(gui_forms_application PUBLIC cxx_std_20)
    target_compile_definitions(gui_forms_application PRIVATE
        GUI_FORMS_APPLICATION_MACOS=1 GUI_FORMS_APPLICATION_SKIA=1)
    set(_gf_application_archives gui_forms_core gui_drawing_core gui_drawing_skia gui_forms_controls
        gui_forms_skia gui_forms_host_macos)
    set(_gf_application_libraries)
    if(TARGET gui_forms_text_engine)
        list(APPEND _gf_application_archives gui_forms_text_engine)
    endif()
    set(GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES ${GUI_FORMS_APPKIT} ${GUI_FORMS_QUARTZCORE} ${GUI_FORMS_IOSURFACE}
        ${GUI_FORMS_COREGRAPHICS} ${GUI_FORMS_UNIFORMTYPEIDENTIFIERS} ${GUI_FORMS_COREFOUNDATION})
    gui_forms_application_library(gui_forms_application
        ARCHIVES ${_gf_application_archives}
        LIBRARIES ${_gf_application_libraries} ${GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES})
    gui_forms_install_bundled_fonts()
endif()

if(GUI_FORMS_ENABLE_LINUX_HOST)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT GUI_FORMS_ENABLE_SKIA)
        message(FATAL_ERROR "The Linux X11 host requires Linux and the private CPU Skia adapter")
    endif()
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GUI_FORMS_ATK REQUIRED IMPORTED_TARGET atk atk-bridge-2.0 gio-2.0)
    if(TARGET X11::Xcursor)
        set(GUI_FORMS_XCURSOR_AVAILABLE ON)
    endif()
    # Stage T5 (owner decision T5-Q9): XFixes follows the compositing
    # manager's selection and XRender names the ARGB visual's alpha.
    if(NOT TARGET X11::Xfixes OR NOT TARGET X11::Xrender)
        message(FATAL_ERROR "The Linux X11 host requires libXfixes and libXrender")
    endif()
    add_library(gui_forms_host_linux STATIC
        src/surface/linux/application/linux_host.cpp
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
        src/surface/linux/application/x11_windowing.cpp
        src/surface/linux/application/x11_shm_present.cpp
        src/surface/linux/application/x11_present_direct.cpp
        src/host/linux/settings/xsettings_parse.cpp
        src/host/linux/locale/strftime_date_pattern.cpp
        src/host/linux/application/linux_input_rules.cpp
        src/surface/linux/settings/xsettings_watch.cpp
        src/surface/linux/accessibility/linux_accessibility.cpp
        src/surface/linux/services/linux_services.cpp
        src/surface/linux/services/x11_foreign_window.cpp
        src/surface/linux/services/x11_tray_icons.cpp
        # XDND drops: not compiled in a high-integrity build (cmake/HighIntegrity.cmake).
        $<$<NOT:$<BOOL:${GUI_FORMS_HIGH_INTEGRITY}>>:src/surface/linux/services/linux_drag_drop.cpp>
        src/surface/linux/services/linux_dialogs.cpp
        src/surface/linux/compositor/x11_compositor.cpp)
    # The environment's POSIX locale (LC_ALL, LC_TIME, LANG), which a
    # high-integrity build does not read (cmake/HighIntegrity.cmake).
    if(NOT GUI_FORMS_HIGH_INTEGRITY)
        target_sources(gui_forms_host_linux PRIVATE src/surface/linux/locale/posix_locale.cpp)
    endif()
    set_target_properties(gui_forms_host_linux PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(gui_forms_host_linux PUBLIC gui_forms_core gui_forms_controls gui_forms_skia
        PRIVATE X11::X11 X11::Xfixes X11::Xrender PkgConfig::GUI_FORMS_ATK
        # dladdr, for the fonts beside the GUI.Forms library (glibc before 2.34 keeps it in libdl).
        ${CMAKE_DL_LIBS})
    if(GUI_FORMS_XCURSOR_AVAILABLE)
        target_link_libraries(gui_forms_host_linux PUBLIC X11::Xcursor)
        target_compile_definitions(gui_forms_host_linux PRIVATE GUI_FORMS_HAS_XCURSOR=1)
    endif()
    # Audit N66: RandR 1.5 lists the monitors where libXrandr is present;
    # without it the host reports the default screen as one monitor.
    if(TARGET X11::Xrandr)
        target_link_libraries(gui_forms_host_linux PUBLIC X11::Xrandr)
        target_compile_definitions(gui_forms_host_linux PRIVATE GUI_FORMS_HAVE_XRANDR=1)
    endif()
    # Audit G4/N67: MIT-SHM presentation where libXext is present; without
    # it every present uses XPutImage.
    if(TARGET X11::Xext)
        target_link_libraries(gui_forms_host_linux PUBLIC X11::Xext)
        target_compile_definitions(gui_forms_host_linux PRIVATE GUI_FORMS_HAVE_XSHM=1)
    endif()
    # The X11 direct presentation path (docs/user/presentation.md): MIT-SHM
    # pixmaps presented with the Present extension, where libXpresent and
    # libXext are present; without them the path refuses as unsupported and
    # the window keeps the copy path.
    find_path(GUI_FORMS_XPRESENT_INCLUDE_DIR X11/extensions/Xpresent.h)
    find_library(GUI_FORMS_XPRESENT_LIBRARY Xpresent)
    if(GUI_FORMS_XPRESENT_INCLUDE_DIR AND GUI_FORMS_XPRESENT_LIBRARY AND TARGET X11::Xext)
        target_include_directories(gui_forms_host_linux PRIVATE ${GUI_FORMS_XPRESENT_INCLUDE_DIR})
        target_link_libraries(gui_forms_host_linux PUBLIC ${GUI_FORMS_XPRESENT_LIBRARY})
        target_compile_definitions(gui_forms_host_linux PRIVATE GUI_FORMS_HAVE_XPRESENT=1)
        set(GUI_FORMS_XPRESENT_AVAILABLE ON)
    endif()
    # Relative pointer mode (cp34): XI 2 raw motion where libXi is present;
    # without it the host reports no relative_pointer or raw_pointer_motion.
    if(TARGET X11::Xi)
        target_link_libraries(gui_forms_host_linux PUBLIC X11::Xi)
        target_compile_definitions(gui_forms_host_linux PRIVATE GUI_FORMS_HAVE_XI2=1)
    endif()
    add_library(gui_forms_application STATIC src/runtime/application_bundle.cpp src/runtime/application/application.cpp
        src/runtime/offscreen/offscreen_raster_create.cpp src/runtime/stxlib_state_probe.cpp)
    add_library(GUIForms::Application ALIAS gui_forms_application)
    set_target_properties(gui_forms_application PROPERTIES EXPORT_NAME Application OUTPUT_NAME gui_forms_application)
    target_include_directories(gui_forms_application PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(gui_forms_application PRIVATE
        GUI_FORMS_APPLICATION_LINUX=1 GUI_FORMS_APPLICATION_SKIA=1)
    set(_gf_application_archives gui_forms_core gui_drawing_core gui_drawing_skia gui_forms_controls
        gui_forms_host_linux gui_forms_skia)
    set(_gf_application_libraries)
    if(TARGET gui_forms_text_engine)
        list(APPEND _gf_application_archives gui_forms_text_engine)
    endif()
    # The host's system libraries, which the installed package names again
    # (cmake/ApplicationPackage.cmake).
    set(GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES X11::X11 X11::Xfixes X11::Xrender PkgConfig::GUI_FORMS_ATK ${CMAKE_DL_LIBS})
    foreach(_gf_x11 IN ITEMS Xcursor Xrandr Xext Xi)
        if(TARGET X11::${_gf_x11})
            list(APPEND GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES X11::${_gf_x11})
        endif()
    endforeach()
    if(GUI_FORMS_XPRESENT_AVAILABLE)
        list(APPEND GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES ${GUI_FORMS_XPRESENT_LIBRARY})
    endif()
    gui_forms_application_library(gui_forms_application
        ARCHIVES ${_gf_application_archives}
        LIBRARIES ${_gf_application_libraries} ${GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES})
    gui_forms_install_bundled_fonts()
endif()

# The Wayland direct presentation path (docs/user/presentation.md), written
# ahead of a Wayland host: wl_shm buffers with buffer damage. Off by default;
# on, it builds as its own archive over the core's presentation shim.
if(GUI_FORMS_ENABLE_WAYLAND_PRESENT)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "GUI_FORMS_ENABLE_WAYLAND_PRESENT requires Linux")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GUI_FORMS_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
    add_library(gui_forms_wayland_present STATIC src/surface/linux/wayland/wayland_shm_present.cpp)
    target_link_libraries(gui_forms_wayland_present PUBLIC gui_forms_core PRIVATE PkgConfig::GUI_FORMS_WAYLAND_CLIENT)
endif()

if(GUI_FORMS_ENABLE_SKIA)
    # GUI.Forms' own window manager for hosts with one surface (owner
    # decision, 2026-11-15): the browser now, a framebuffer appliance later.
    # Portable C++ over the CPU Skia raster; the hosts supply a surface,
    # input records and a present call (src/host/surface/README.md).
    add_library(gui_forms_host_surface STATIC
        src/host/surface/compose/surface_compose.cpp
        src/host/surface/desk/surface_desk.cpp
        src/host/surface/input/surface_input.cpp
        src/host/surface/manager/surface_capsule_wiring.cpp
        src/host/surface/manager/surface_manager.cpp
        src/host/surface/manager/surface_windowing.cpp
        src/host/surface/manager/surface_services.cpp
        src/host/surface/manager/surface_dialogs.cpp)
    target_link_libraries(gui_forms_host_surface PUBLIC gui_forms_core gui_forms_controls gui_forms_skia)
endif()

if(GUI_FORMS_ENABLE_WEB_HOST)
    if(NOT EMSCRIPTEN OR NOT GUI_FORMS_ENABLE_SKIA)
        message(FATAL_ERROR "The browser host requires the Emscripten toolchain (cmake/emscripten.cmake) and the private CPU Skia adapter")
    endif()
    add_library(gui_forms_host_web STATIC
        src/surface/web/application/web_host.cpp
        src/host/web/application/web_dialog_rules.cpp
        src/host/web/input/web_input_rules.cpp
        src/host/web/page/web_present.cpp
        src/surface/web/page/web_page_exports.cpp
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
    target_link_libraries(gui_forms_host_web PUBLIC gui_forms_host_surface gui_forms_core gui_forms_controls gui_forms_skia)
    # The page half, linked into every program that links the host.
    set(GUI_FORMS_WEB_HOST_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/src/surface/web/page/web_host.js)
    target_link_options(gui_forms_host_web INTERFACE "SHELL:--js-library ${GUI_FORMS_WEB_HOST_LIBRARY}")
    set_property(TARGET gui_forms_host_web APPEND PROPERTY INTERFACE_LINK_DEPENDS ${GUI_FORMS_WEB_HOST_LIBRARY})
    # The program runs on a worker (the page's main thread keeps the DOM and
    # WebGL2), its loop blocking there as on a desktop. Programs add their
    # memory, pool and file-system settings (src/host/web/README.md).
    target_link_options(gui_forms_host_web INTERFACE
        -pthread -sPROXY_TO_PTHREAD=1 -sENVIRONMENT=web,worker -sFORCE_FILESYSTEM=1 -sMAX_WEBGL_VERSION=2
        -sINVOKE_RUN=0 -lidbfs.js
        "SHELL:-sEXPORTED_FUNCTIONS=_main,_malloc,_free"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain,UTF8ToString,stringToNewUTF8,addRunDependency,removeRunDependency")
    # One static library, as on every platform (one module): the
    # Application library links the engine whole into the program.
    add_library(gui_forms_application STATIC src/runtime/application_bundle.cpp src/runtime/application/application.cpp
        src/runtime/offscreen/offscreen_raster_create.cpp)
    add_library(GUIForms::Application ALIAS gui_forms_application)
    set_target_properties(gui_forms_application PROPERTIES EXPORT_NAME Application OUTPUT_NAME gui_forms_application)
    target_include_directories(gui_forms_application PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(gui_forms_application PRIVATE
        GUI_FORMS_APPLICATION_WEB=1 GUI_FORMS_APPLICATION_SKIA=1)
    set(_gf_application_archives gui_forms_core gui_drawing_core gui_drawing_skia gui_forms_controls
        gui_forms_host_surface gui_forms_host_web gui_forms_skia)
    set(_gf_application_libraries)
    if(TARGET gui_forms_text_engine)
        list(APPEND _gf_application_archives gui_forms_text_engine)
    endif()
    set(GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES)
    gui_forms_application_library(gui_forms_application
        ARCHIVES ${_gf_application_archives} LIBRARIES ${_gf_application_libraries})
endif()

if(GUI_FORMS_ENABLE_WINDOWS_HOST)
    # Everything of the host but windows_host.cpp, which alone reads the
    # host's build switches (the DIB store, prepared text, the tests' hooks):
    # compiled once for the host and the DIB lifecycle test.
    add_library(gui_forms_windows_host_objects OBJECT
        src/surface/windows/accessibility/windows_accessibility.cpp
        src/surface/windows/accessibility/windows_semantic_source.cpp
        src/surface/windows/accessibility/windows_automation.cpp
        src/surface/windows/accessibility/windows_text_automation.cpp
        src/surface/windows/services/windows_clipboard_image.cpp
        src/surface/windows/services/windows_notify_icons.cpp
        src/surface/windows/locale/windows_date_pattern.cpp
        src/surface/windows/locale/windows_locale.cpp
        src/surface/windows/paint_endpoint/windows_compatibility_paint_endpoint.cpp
        src/surface/windows/compositor/windows_compositor.cpp
    )
    target_include_directories(gui_forms_windows_host_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/host/windows)
    target_compile_definitions(gui_forms_windows_host_objects PRIVATE ${GUI_FORMS_WINDOWS_DEFINITIONS})
    target_link_libraries(gui_forms_windows_host_objects PRIVATE gui_forms_core gui_forms_skia gui_forms_text_engine)
    set_target_properties(gui_forms_windows_host_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(gui_forms_host_windows STATIC
        src/surface/windows/application/windows_host.cpp
        $<TARGET_OBJECTS:gui_forms_windows_host_objects>
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
    )
    target_include_directories(gui_forms_host_windows PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/windows
    )
    target_compile_definitions(gui_forms_host_windows PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS}
    )
    if(GUI_FORMS_WINDOWS_AUTOMATION_ENVIRONMENT)
        target_compile_definitions(gui_forms_host_windows PRIVATE GUI_FORMS_WINDOWS_AUTOMATION_ENVIRONMENT=1)
    endif()
    if(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
        target_sources(gui_forms_host_windows PRIVATE
            $<TARGET_OBJECTS:gui_forms_dib_frame_store_objects>)
        target_compile_definitions(gui_forms_host_windows PRIVATE
            GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB=1)
        # With prepared text the window's Skia raster runs its own prepared
        # frames, published into the DIB store after they commit.
        if(TARGET gui_forms_prepared_text_impl)
            target_compile_definitions(gui_forms_host_windows PRIVATE
                GUI_FORMS_WINDOWS_PREPARED_TEXT=1)
        endif()
    endif()
    # Stage SK3: the Win32 host draws with the Skia CPU raster.
    if(NOT TARGET gui_forms_skia)
        message(FATAL_ERROR "The Win32 host requires GUI_FORMS_ENABLE_SKIA (stage SK3)")
    endif()
    target_link_libraries(gui_forms_host_windows
        PUBLIC gui_forms_core gui_forms_skia
        PRIVATE gui_forms_text_engine user32 gdi32 imm32 ole32 oleaut32 oleacc uiautomationcore uuid comdlg32 shell32
    )
    if(WIN32)
        add_library(gui_forms_application STATIC
            src/runtime/application_bundle.cpp
            src/runtime/application/application.cpp
            src/runtime/offscreen/offscreen_raster_create.cpp
            src/runtime/stxlib_state_probe.cpp)
        add_library(GUIForms::Application ALIAS gui_forms_application)
        set_target_properties(gui_forms_application PROPERTIES
            EXPORT_NAME Application OUTPUT_NAME gui_forms_application)
        target_include_directories(gui_forms_application PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
        target_compile_features(gui_forms_application PUBLIC cxx_std_20)
        target_compile_definitions(gui_forms_application PRIVATE
            GUI_FORMS_APPLICATION_WINDOWS=1 GUI_FORMS_APPLICATION_SKIA=1)
        set(GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES
            user32 gdi32 imm32 ole32 oleaut32 oleacc uiautomationcore uuid comdlg32 shell32)
        gui_forms_application_library(gui_forms_application
            ARCHIVES gui_forms_core gui_drawing_core gui_drawing_skia gui_forms_controls gui_forms_host_windows
                     gui_forms_skia gui_forms_text_engine
            LIBRARIES ${GUI_FORMS_APPLICATION_SYSTEM_LIBRARIES})
        gui_forms_install_bundled_fonts()
    endif()
    # The Win32 compatibility shim (src/surface/windows/win32_compat_shim.cpp)
    # is part of the C ABI library on Windows (one module, owner decision
    # 2026-11-15, "Fold it in"): cmake/CAbi.cmake compiles it with the
    # library's objects and exports its Win32 names.
    add_executable(gui_forms_windows_probe
        tools/windows_automation_probe.cpp
    )
    target_compile_definitions(gui_forms_windows_probe PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS}
    )
endif()

# Every archive the Application library holds
# whole (gui_forms_application_archive, beside the Threading library; the
# Application library itself is made by gui_forms_application_library in
# each host's block).
foreach(_gf_archive IN ITEMS
        gui_forms_core gui_drawing_core gui_forms_controls gui_forms_plot_primitives
        gui_forms_host_headless gui_forms_text_engine gui_forms_prepared_text_impl
        gui_forms_text_masks_impl gui_forms_sealed_pages_objects gui_forms_dib_frame_store_objects
        gui_forms_skia gui_drawing_skia gui_forms_host_macos gui_forms_host_linux
        gui_forms_windows_host_objects gui_forms_host_windows gui_forms_host_surface gui_forms_host_web)
    if(TARGET ${_gf_archive})
        gui_forms_application_archive(${_gf_archive})
    endif()
endforeach()

if(GUI_FORMS_BUILD_GALLERY_MODEL OR GUI_FORMS_BUILD_GALLERY)
    # One demonstration program (decision F.1): the registry-driven Gallery.
    # gallery_pages.cpp holds the shell; the pages migrated from the retired
    # Complete Showcase live one per file under src/controls/gallery/pages/.
    # All of them stay under the public-controls-only policy test below.
    set(gui_forms_gallery_page_sources
        src/controls/gallery/pages/control-spectrum.cpp
        src/controls/gallery/pages/controls-inspection.cpp
        src/controls/gallery/pages/ranges-and-progress.cpp
        src/controls/gallery/pages/containers-and-focus.cpp
        src/controls/gallery/pages/animation-laboratory.cpp
        src/controls/gallery/pages/states-and-diagnostics.cpp
        src/controls/gallery/pages/text-and-input.cpp
        src/controls/gallery/pages/collections-and-overlays.cpp
        src/controls/gallery/pages/values-and-spinners.cpp
        src/controls/gallery/pages/images-and-drawing.cpp
        src/controls/gallery/pages/tabs-and-pages.cpp
        src/controls/gallery/pages/checked-collections.cpp
        src/controls/gallery/pages/timing-and-tooltips.cpp
        src/controls/gallery/pages/dates-and-calendar.cpp
        src/controls/gallery/pages/dialogs-and-host-services.cpp
        src/controls/gallery/pages/retained-layout-panels.cpp
        src/controls/gallery/pages/css-layout-panels.cpp
        src/controls/gallery/pages/dock-and-anchor.cpp
        src/controls/gallery/pages/docking-workspace.cpp
        src/controls/gallery/pages/commands-and-menus.cpp
        src/controls/gallery/pages/tree-and-object-views.cpp
        src/controls/gallery/pages/choices-and-sections.cpp
        src/controls/gallery/pages/value-editors.cpp
        src/controls/gallery/pages/responsive-layouts.cpp
        src/controls/gallery/pages/anchored-overlays.cpp
        src/controls/gallery/pages/live-surfaces.cpp
        src/controls/gallery/pages/typography-and-inspection.cpp
        src/controls/gallery/pages/semantic-feedback.cpp
        src/controls/gallery/pages/notices-and-badges.cpp
        src/controls/gallery/pages/input-and-rules.cpp
        src/controls/gallery/pages/theme-settings.cpp
        src/controls/gallery/pages/spinners-and-rings.cpp
        src/controls/gallery/pages/instruments.cpp
        src/controls/gallery/pages/guided-tasks.cpp
        src/controls/gallery/pages/help-book.cpp
        src/controls/gallery/pages/industrial-surfaces.cpp
        src/controls/gallery/pages/node-graphs.cpp
        src/controls/gallery/pages/composite-controls.cpp
    )
    add_library(gui_forms_gallery_model STATIC
        src/controls/gallery_controls.cpp
        src/controls/gallery_pages.cpp
        ${gui_forms_gallery_page_sources}
        demo/gallery.cpp
        demo/gallery_model.cpp
        demo/gallery_instrument.cpp
        demo/gallery_parts.cpp
    )
    target_include_directories(gui_forms_gallery_model PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/demo>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/controls>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/gui_forms_gallery>
    )
    # The industrial-surfaces page reads the lamps' light-plane bakes from
    # the source tree (or GUI_FORMS_LIGHT_PLANES); none are shipped yet.
    target_compile_definitions(gui_forms_gallery_model PRIVATE
        GUI_FORMS_VERSION_STRING="${PROJECT_VERSION}"
        GUI_FORMS_LIGHT_PLANE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/light_planes")
    # Installed, the model names no GUI.Forms library: a program links it with
    # GUIForms::Application and a headless test with GUIForms::Controls, so a
    # process holds one engine (the SDK's linking rule).
    target_link_libraries(gui_forms_gallery_model PUBLIC $<BUILD_INTERFACE:gui_forms_controls>)
    # The lab and demonstration controls its pages show (audit F9).
    # Installed, the model names GUIForms::Lab (GUI_FORMS_INSTALL_LAB) and
    # GUIForms::DemoControls (installed beside it).
    target_link_libraries(gui_forms_gallery_model PUBLIC gui_forms_lab gui_forms_demo_controls)
    # The Gallery's window (stage B): demo/gallery.form, compiled into
    # gallery_window.hpp, the GalleryWindow class bound to its GalleryModel
    # (demo/gallery_model.hpp); gallery_context.hpp includes it, so its
    # directory is public.
    if(NOT GUI_FORMS_BUILD_FORM_COMPILER)
        message(FATAL_ERROR "The Gallery needs the form compiler: its window is "
                            "demo/gallery.form (set GUI_FORMS_BUILD_FORM_COMPILER=ON)")
    endif()
    gui_forms_add_form(gui_forms_gallery_model demo/gallery.form
        CLASS GalleryWindow NAMESPACE gui_forms::gallery
        OUTPUT gui_forms_forms/gallery_window.hpp
        MODEL gui_forms::gallery::GalleryModel MODEL_HEADER gallery_model.hpp)
    # The Gallery's own pages, apart from the window so each control is
    # attached once (in its page body): a document class over the model.
    gui_forms_add_form(gui_forms_gallery_model demo/gallery_form_pages.form
        CLASS GalleryFormPagesDocument NAMESPACE gui_forms::gallery
        TYPES
            "gallery_lifecycle_card,gui_forms::gallery::LifecycleCard,user_control,gallery_parts.hpp"
            "gallery_collection,gui_forms::gallery::CollectionList,list_box,gallery_parts.hpp"
            "gallery_instrument,gui_forms::gallery::Instrument,user_control,gallery_instrument.hpp")
    # The pages converted to form documents (FD6.3), one class each.
    gui_forms_add_form(gui_forms_gallery_model demo/pages/typography-and-inspection.form
        CLASS TypographyDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab
        USED_BY src/controls/gallery/pages/typography-and-inspection.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/animation-laboratory.form
        CLASS AnimationDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab
        USED_BY src/controls/gallery/pages/animation-laboratory.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/collections-and-overlays.form
        CLASS CollectionsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/collections-and-overlays.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/images-and-drawing.form
        CLASS ImagesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/images-and-drawing.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/ranges-and-progress.form
        CLASS RangesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/ranges-and-progress.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/checked-collections.form
        CLASS CheckedCollectionsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/checked-collections.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/choices-and-sections.form
        CLASS ChoicesDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab
        USED_BY src/controls/gallery/pages/choices-and-sections.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/tree-and-object-views.form
        CLASS TreeAndObjectViewsDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab
        USED_BY src/controls/gallery/pages/tree-and-object-views.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/text-and-input.form
        CLASS TextAndInputDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/text-and-input.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/states-and-diagnostics.form
        CLASS StatesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/states-and-diagnostics.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/timing-and-tooltips.form
        CLASS TimingDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/timing-and-tooltips.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/dates-and-calendar.form
        CLASS DatesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/dates-and-calendar.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/values-and-spinners.form
        CLASS ValuesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/values-and-spinners.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/value-editors.form
        CLASS ValueEditorsDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab
        USED_BY src/controls/gallery/pages/value-editors.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/dialogs-and-host-services.form
        CLASS HostServicesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/dialogs-and-host-services.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/semantic-feedback.form
        CLASS SemanticFeedbackDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/semantic-feedback.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/notices-and-badges.form
        CLASS NoticesAndBadgesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/notices-and-badges.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/input-and-rules.form
        CLASS InputAndRulesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/input-and-rules.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/theme-settings.form
        CLASS ThemeSettingsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/theme-settings.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/spinners-and-rings.form
        CLASS SpinnersAndRingsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/spinners-and-rings.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/instruments.form
        CLASS InstrumentsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/instruments.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/guided-tasks.form
        CLASS GuidedTasksDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/guided-tasks.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/industrial-surfaces.form
        CLASS IndustrialSurfacesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/industrial-surfaces.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/node-graphs.form
        CLASS NodeGraphsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/node-graphs.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/help-book.form
        CLASS HelpBookPageDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/help-book.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/control-spectrum.form
        CLASS ControlSpectrumDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/control-spectrum.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/retained-layout-panels.form
        CLASS RetainedLayoutDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/retained-layout-panels.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/responsive-layouts.form
        CLASS ResponsiveLayoutsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/responsive-layouts.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/css-layout-panels.form
        CLASS CssLayoutDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/css-layout-panels.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/dock-and-anchor.form
        CLASS DockAnchorDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/dock-and-anchor.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/docking-workspace.form
        CLASS DockingWorkspaceDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/docking-workspace.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/containers-and-focus.form
        CLASS ContainersDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/containers-and-focus.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/tabs-and-pages.form
        CLASS TabsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/tabs-and-pages.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/anchored-overlays.form
        CLASS AnchoredOverlaysDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/anchored-overlays.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/commands-and-menus.form
        CLASS CommandsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/commands-and-menus.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/live-surfaces.form
        CLASS LiveSurfacesDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/live-surfaces.cpp)
    # The composite-controls page (cp34): the worked examples generated as
    # classes, and a form naming them whose children fill the card's slot.
    # Each generated page header is included only by its page's source (the
    # composite classes through the composite-controls page's header), so
    # USED_BY keeps a changed document from rebuilding every gallery object.
    # The window and the form pages are included through gallery_context.hpp
    # by most sources, so they keep the default (every source).
    gui_forms_add_control(gui_forms_gallery_model demo/composites/toggle-pill.control
        CLASS TogglePill NAMESPACE gui_forms::gallery::composites
        USED_BY src/controls/gallery/pages/composite-controls.cpp)
    gui_forms_add_control(gui_forms_gallery_model demo/composites/card-with-action.control
        CLASS CardWithAction NAMESPACE gui_forms::gallery::composites
        USED_BY src/controls/gallery/pages/composite-controls.cpp)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/composite-controls.form
        CLASS CompositeControlsDocument NAMESPACE gui_forms::gallery::pages
        USED_BY src/controls/gallery/pages/composite-controls.cpp
        COMPOSITES
            "demo/composites/toggle-pill.control,gui_forms::gallery::composites::TogglePill,toggle_pill_control.hpp"
            "demo/composites/card-with-action.control,gui_forms::gallery::composites::CardWithAction,card_with_action_control.hpp")
    # The Gallery's project theme (owner rulings of 2026-11-04): the forms
    # name roles by class; the theme gives the roles their look.
    gui_forms_add_theme(gui_forms_gallery_model demo/themes/gallery CHECK
        FUNCTION gallery_theme_package NAMESPACE gui_forms::gallery
        FORMS demo/gallery.form demo/gallery_form_pages.form demo/pages/typography-and-inspection.form
            demo/pages/animation-laboratory.form demo/pages/collections-and-overlays.form
            demo/pages/images-and-drawing.form demo/pages/ranges-and-progress.form
            demo/pages/semantic-feedback.form demo/pages/control-spectrum.form
            demo/pages/notices-and-badges.form
            demo/pages/input-and-rules.form demo/pages/theme-settings.form
            demo/pages/spinners-and-rings.form
            demo/pages/instruments.form demo/pages/guided-tasks.form demo/pages/help-book.form
            demo/pages/retained-layout-panels.form demo/pages/responsive-layouts.form
            demo/pages/css-layout-panels.form
            demo/pages/checked-collections.form demo/pages/choices-and-sections.form
            demo/pages/tree-and-object-views.form demo/pages/text-and-input.form
            demo/pages/states-and-diagnostics.form demo/pages/timing-and-tooltips.form
            demo/pages/dates-and-calendar.form demo/pages/values-and-spinners.form
            demo/pages/value-editors.form demo/pages/dialogs-and-host-services.form
            demo/pages/dock-and-anchor.form demo/pages/docking-workspace.form
            demo/pages/containers-and-focus.form demo/pages/tabs-and-pages.form
            demo/pages/anchored-overlays.form demo/pages/commands-and-menus.form
            demo/pages/live-surfaces.form demo/pages/industrial-surfaces.form
            demo/pages/node-graphs.form)
    # The industrial-surfaces page's theme and the GOST palette it imports
    # (skeudoindustrial surfaces, phases 4 and 5). Unchecked here: the
    # industrial package needs the palette in its library, which
    # gui_forms_theme_gost_palette_tests supplies and checks.
    gui_forms_add_theme(gui_forms_gallery_model demo/themes/gost_paints
        FUNCTION gost_paints_theme_package NAMESPACE gui_forms::gallery)
    gui_forms_add_theme(gui_forms_gallery_model demo/themes/industrial
        FUNCTION industrial_theme_package NAMESPACE gui_forms::gallery)
    target_include_directories(gui_forms_gallery_model PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/gui_forms_forms>)
    # The Gallery is compiled with the localizable-literal plugin: its own
    # literals are the first table a translator reads.
    gui_forms_compile_with_localizable_plugin(gui_forms_gallery_model)
    # GUIForms::GalleryModel, for the programs repository
    # (malkuth-software/gui_forms_programs), which builds the Gallery
    # executables against an installed SDK: the static model and its one
    # entry header, gui_forms_gallery/gallery.hpp.
    if(GUI_FORMS_INSTALL_GALLERY_MODEL)
        set_target_properties(gui_forms_gallery_model PROPERTIES EXPORT_NAME GalleryModel)
        install(TARGETS gui_forms_gallery_model
                EXPORT GUIFormsGalleryModelTargets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(FILES demo/gallery.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gui_forms_gallery)
        # The demonstration controls the pages show (audit F9), in their
        # own component; the lab controls come from GUIForms::Lab.
        install(TARGETS gui_forms_demo_controls
                EXPORT GUIFormsGalleryModelTargets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT demo_controls)
        install(DIRECTORY demo/lab_controls/include/gui_forms_demo
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT demo_controls)
        # The Gallery's form compiler (GUIForms::form_compiler_lab), which
        # knows the lab and demonstration controls the pages name: an SDK
        # consumer that generates the page documents passes it as
        # gui_forms_add_form's COMPILER (examples/reference).
        set_target_properties(gui_forms_form_compiler_lab PROPERTIES EXPORT_NAME form_compiler_lab)
        install(TARGETS gui_forms_form_compiler_lab
                EXPORT GUIFormsGalleryModelTargets
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT demo_controls)
        install(EXPORT GUIFormsGalleryModelTargets
                FILE GUIFormsGalleryModelTargets.cmake
                NAMESPACE GUIForms::
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
    endif()
endif()
if(GUI_FORMS_INSTALL_GALLERY_MODEL AND NOT TARGET gui_forms_gallery_model)
    message(FATAL_ERROR "GUI_FORMS_INSTALL_GALLERY_MODEL needs GUI_FORMS_BUILD_GALLERY_MODEL")
endif()
# The installed Gallery model's pages show CorrespondenceView, ReviewCard and
# InstrumentRack, which only the lab library installs (audit F9): no page is
# dropped to make the install work.
if(GUI_FORMS_INSTALL_GALLERY_MODEL AND NOT GUI_FORMS_INSTALL_LAB)
    message(FATAL_ERROR "GUI_FORMS_INSTALL_GALLERY_MODEL needs GUI_FORMS_INSTALL_LAB=ON: the installed "
                        "Gallery model links the lab controls (GUIForms::Lab) its pages show")
endif()

if(GUI_FORMS_BUILD_GALLERY)
    add_library(gui_forms_vsync_lab_model STATIC
        demo/vsync_lab.cpp
    )
    target_include_directories(gui_forms_vsync_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_vsync_lab_model PUBLIC gui_forms_controls)

    add_library(gui_forms_visual_inspector_lab_model STATIC
        demo/visual_inspector_lab.cpp
    )
    target_include_directories(gui_forms_visual_inspector_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_visual_inspector_lab_model PUBLIC
        gui_forms_controls gui_forms_demo_controls)

    add_library(gui_forms_layered_material_lab_model STATIC
        demo/layered_material_lab.cpp
    )
    target_include_directories(gui_forms_layered_material_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_layered_material_lab_model PUBLIC
        gui_forms_controls gui_forms_demo_controls)

    # Stage T5's compositor lab (phase C8): a declared window with glass,
    # reflections and the backdrop named by --backdrop.
    add_library(gui_forms_compositor_lab_model STATIC
        demo/compositor_lab.cpp
    )
    target_include_directories(gui_forms_compositor_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_compositor_lab_model PUBLIC
        gui_forms_controls)

    add_library(gui_forms_custom_chrome_lab_model STATIC
        demo/custom_chrome_lab.cpp
    )
    target_include_directories(gui_forms_custom_chrome_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_custom_chrome_lab_model PUBLIC
        gui_forms_controls gui_forms_demo_controls)

    add_library(gui_forms_typography_scale_lab_model STATIC
        demo/typography_scale_lab.cpp
    )
    target_include_directories(gui_forms_typography_scale_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_typography_scale_lab_model PUBLIC
        gui_forms_controls gui_forms_demo_controls)

    add_library(gui_forms_responsive_tracks_lab_model STATIC
        demo/responsive_tracks_lab.cpp
    )
    target_include_directories(gui_forms_responsive_tracks_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_responsive_tracks_lab_model PUBLIC
        gui_forms_controls gui_forms_demo_controls)

    add_library(gui_forms_connected_controls_lab_model STATIC
        demo/connected_controls_lab.cpp
    )
    target_include_directories(gui_forms_connected_controls_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_connected_controls_lab_model PUBLIC
        gui_forms_controls gui_forms_demo_controls)

    add_library(gui_forms_seam_proximity_lab_model STATIC
        demo/seam_proximity_lab.cpp
    )
    target_include_directories(gui_forms_seam_proximity_lab_model PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/demo
    )
    target_link_libraries(gui_forms_seam_proximity_lab_model PUBLIC
        gui_forms_controls gui_forms_demo_controls)

    if(GUI_FORMS_ENABLE_MACOS_HOST)
        set_source_files_properties(
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
            PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/fonts"
        )
        # offscreen_raster_create.cpp and the sealed pages: the --headless whole-page
        # capture (demo/gallery_headless_capture.hpp) draws through OffscreenRaster.
        add_executable(gui_forms_gallery MACOSX_BUNDLE
            src/host/macos/gallery_main.mm
            src/runtime/offscreen/offscreen_raster_create.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_gallery PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_gallery PRIVATE
            gui_forms_gallery_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_gallery PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Gallery"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.gallery"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Gallery"
        )


        add_executable(gui_forms_vsync_lab MACOSX_BUNDLE
            src/host/macos/vsync_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_vsync_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_vsync_lab PRIVATE
            gui_forms_vsync_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_vsync_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms VSync + Waterfall Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.vsync-waterfall-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms VSync + Waterfall Lab"
        )

        add_executable(gui_forms_visual_inspector_lab MACOSX_BUNDLE
            src/host/macos/visual_inspector_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_visual_inspector_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_visual_inspector_lab PRIVATE
            gui_forms_visual_inspector_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_visual_inspector_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Visual Inspector Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.visual-inspector-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Visual Inspector Lab"
        )

        add_executable(gui_forms_layered_material_lab MACOSX_BUNDLE
            src/host/macos/layered_material_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_layered_material_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_layered_material_lab PRIVATE
            gui_forms_layered_material_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_layered_material_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Layered Material Fidelity Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.layered-material-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Layered Material Fidelity Lab"
        )

        add_executable(gui_forms_compositor_lab MACOSX_BUNDLE
            src/host/macos/compositor_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_compositor_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_compositor_lab PRIVATE
            gui_forms_compositor_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_compositor_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Compositor Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.compositor-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Compositor Lab"
        )

        add_executable(gui_forms_custom_chrome_lab MACOSX_BUNDLE
            src/host/macos/custom_chrome_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_custom_chrome_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_custom_chrome_lab PRIVATE
            gui_forms_custom_chrome_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_custom_chrome_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Custom Chrome Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.custom-chrome-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Custom Chrome Lab"
        )

        add_executable(gui_forms_typography_scale_lab MACOSX_BUNDLE
            src/host/macos/typography_scale_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_typography_scale_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_typography_scale_lab PRIVATE
            gui_forms_typography_scale_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_typography_scale_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Typography + Scale Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.typography-scale-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Typography + Scale Lab"
        )

        add_executable(gui_forms_responsive_tracks_lab MACOSX_BUNDLE
            src/host/macos/responsive_tracks_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_responsive_tracks_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_responsive_tracks_lab PRIVATE
            gui_forms_responsive_tracks_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_responsive_tracks_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Responsive Tracks Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.responsive-tracks-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Responsive Tracks Lab"
        )

        add_executable(gui_forms_connected_controls_lab MACOSX_BUNDLE
            src/host/macos/connected_controls_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_connected_controls_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_connected_controls_lab PRIVATE
            gui_forms_connected_controls_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_connected_controls_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Connected Controls Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.connected-controls-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Connected Controls Lab"
        )

        add_executable(gui_forms_seam_proximity_lab MACOSX_BUNDLE
            src/host/macos/seam_proximity_lab_main.mm
            ${GUI_FORMS_PROGRAM_FONT_NOTICES}
        )
        target_compile_options(gui_forms_seam_proximity_lab PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_seam_proximity_lab PRIVATE
            gui_forms_seam_proximity_lab_model
            gui_forms_host_macos
        )
        set_target_properties(gui_forms_seam_proximity_lab PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Physical Seam + Proximity Lab"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.seam-proximity-lab"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            OUTPUT_NAME "GUI.Forms Physical Seam + Proximity Lab"
        )
    endif()

    if(GUI_FORMS_ENABLE_WINDOWS_HOST)
        add_executable(gui_forms_gallery_windows
            src/host/windows/gallery_main.cpp
            src/runtime/offscreen/offscreen_raster_create.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
        )
        target_link_libraries(gui_forms_gallery_windows PRIVATE
            gui_forms_gallery_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_gallery_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Gallery"
        )

        # The Gallery under Application::run, so menus open in Popup
        # windows (tools/menu_probe.py drives it).
        # Application::run is built in (not the shared application
        # library), since the Gallery model links the static controls.
        add_executable(gui_forms_menu_probe_windows
            src/host/windows/menu_probe_main.cpp
            src/runtime/application/application.cpp
        )
        target_compile_definitions(gui_forms_menu_probe_windows PRIVATE
            GUI_FORMS_APPLICATION_WINDOWS=1 GUI_FORMS_APPLICATION_SKIA=1)
        target_link_libraries(gui_forms_menu_probe_windows PRIVATE
            gui_forms_gallery_model
            gui_forms_host_windows
        )

        add_executable(gui_forms_vsync_lab_windows
            src/host/windows/vsync_lab_main.cpp
        )
        target_link_libraries(gui_forms_vsync_lab_windows PRIVATE
            gui_forms_vsync_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_vsync_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms VSync + Waterfall Lab"
        )

        add_executable(gui_forms_visual_inspector_lab_windows
            src/host/windows/visual_inspector_lab_main.cpp
        )
        target_link_libraries(gui_forms_visual_inspector_lab_windows PRIVATE
            gui_forms_visual_inspector_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_visual_inspector_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Visual Inspector Lab"
        )

        add_executable(gui_forms_layered_material_lab_windows
            src/host/windows/layered_material_lab_main.cpp
        )
        target_link_libraries(gui_forms_layered_material_lab_windows PRIVATE
            gui_forms_layered_material_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_layered_material_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Layered Material Fidelity Lab"
        )

        add_executable(gui_forms_compositor_lab_windows
            src/host/windows/compositor_lab_main.cpp
        )
        target_link_libraries(gui_forms_compositor_lab_windows PRIVATE
            gui_forms_compositor_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_compositor_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Compositor Lab"
        )

        add_executable(gui_forms_custom_chrome_lab_windows
            src/host/windows/custom_chrome_lab_main.cpp
        )
        target_link_libraries(gui_forms_custom_chrome_lab_windows PRIVATE
            gui_forms_custom_chrome_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_custom_chrome_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Custom Chrome Lab"
        )

        add_executable(gui_forms_typography_scale_lab_windows
            src/host/windows/typography_scale_lab_main.cpp
        )
        target_link_libraries(gui_forms_typography_scale_lab_windows PRIVATE
            gui_forms_typography_scale_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_typography_scale_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Typography + Scale Lab"
        )

        add_executable(gui_forms_responsive_tracks_lab_windows
            src/host/windows/responsive_tracks_lab_main.cpp
        )
        target_link_libraries(gui_forms_responsive_tracks_lab_windows PRIVATE
            gui_forms_responsive_tracks_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_responsive_tracks_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Responsive Tracks Lab"
        )

        add_executable(gui_forms_connected_controls_lab_windows
            src/host/windows/connected_controls_lab_main.cpp
        )
        target_link_libraries(gui_forms_connected_controls_lab_windows PRIVATE
            gui_forms_connected_controls_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_connected_controls_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Connected Controls Lab"
        )

        add_executable(gui_forms_seam_proximity_lab_windows
            src/host/windows/seam_proximity_lab_main.cpp
        )
        target_link_libraries(gui_forms_seam_proximity_lab_windows PRIVATE
            gui_forms_seam_proximity_lab_model
            gui_forms_host_windows
        )
        set_target_properties(gui_forms_seam_proximity_lab_windows PROPERTIES
            OUTPUT_NAME "GUI.Forms Physical Seam + Proximity Lab"
        )
        # The Gallery and the labs carry GUI_FORMS_PROGRAM_FONT_NOTICES beside
        # them; the staging at the end of this file copies them.
        find_program(GUI_FORMS_WINDOWS_OBJDUMP
            NAMES x86_64-w64-mingw32-objdump
            NO_CMAKE_FIND_ROOT_PATH
        )
        if(GUI_FORMS_WINDOWS_OBJDUMP)
            add_custom_target(gui_forms_windows_boundary_audit
                COMMAND ${CMAKE_COMMAND}
                    -DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
                    -DWINDOWS_GALLERY=$<TARGET_FILE:gui_forms_gallery_windows>
                    -DOBJDUMP_EXECUTABLE=${GUI_FORMS_WINDOWS_OBJDUMP}
                    -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/windows-boundary-audit.txt
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_windows_boundaries.cmake
                DEPENDS gui_forms_gallery_windows
                VERBATIM
            )
        endif()

        if(CMAKE_HOST_APPLE)
            find_program(GUI_FORMS_WINE_EXECUTABLE
                NAMES wine
                PATHS /opt/homebrew/bin
                NO_CMAKE_FIND_ROOT_PATH
            )
            if(GUI_FORMS_WINE_EXECUTABLE)
                add_custom_target(gui_forms_windows_wine_smoke
                    COMMAND ${CMAKE_COMMAND} -E env
                        WINE_BINARY=${GUI_FORMS_WINE_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_windows_wine_smoke.sh
                        ${CMAKE_CURRENT_BINARY_DIR}
                    DEPENDS gui_forms_gallery_windows gui_forms_windows_probe
                    USES_TERMINAL
                    VERBATIM
                )
                add_custom_target(gui_forms_windows_gallery_services_smoke
                    COMMAND ${CMAKE_COMMAND} -E env
                        WINE_BINARY=${GUI_FORMS_WINE_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_windows_gallery_services_smoke.sh
                        ${CMAKE_CURRENT_BINARY_DIR}
                    DEPENDS gui_forms_gallery_windows gui_forms_windows_probe
                    USES_TERMINAL
                    VERBATIM
                )
            endif()
        endif()
    endif()

    if(GUI_FORMS_ENABLE_LINUX_HOST)
        add_executable(gui_forms_gallery_linux
            src/host/linux/gallery_main.cpp
            src/runtime/offscreen/offscreen_raster_create.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
        )
        target_link_libraries(gui_forms_gallery_linux PRIVATE
            gui_forms_gallery_model
            gui_forms_host_linux
        )
        set_target_properties(gui_forms_gallery_linux PROPERTIES
            OUTPUT_NAME "gui-forms-gallery"
        )

        add_executable(gui_forms_compositor_lab_linux
            src/host/linux/compositor_lab_main.cpp
        )
        target_link_libraries(gui_forms_compositor_lab_linux PRIVATE
            gui_forms_compositor_lab_model
            gui_forms_host_linux
        )
        set_target_properties(gui_forms_compositor_lab_linux PROPERTIES
            OUTPUT_NAME "gui-forms-compositor-lab"
        )

        # The X11 host reads its faces from the embedded pack; the font
        # licences are staged in fonts/ at the end of this file.
    endif()

    include(demo/correspondence/correspondence.cmake)
endif()

if(GUI_FORMS_BUILD_TEXT_LAYOUT_PROBE)
    add_executable(gui_forms_text_layout_probe benchmarks/text_layout_probe.cpp)
    target_link_libraries(gui_forms_text_layout_probe PRIVATE gui_forms_text_engine)
endif()

if(GUI_FORMS_BUILD_LABEL_LAYOUT_PROBE)
    add_executable(gui_forms_label_layout_probe benchmarks/label_layout_probe.cpp)
    target_link_libraries(gui_forms_label_layout_probe PRIVATE gui_forms_controls gui_forms_text_engine)
endif()

if(GUI_FORMS_BUILD_TEXT_WORKER_PROBE)
    find_package(Threads REQUIRED)
    add_executable(gui_forms_text_worker_probe
        benchmarks/text_worker_probe.cpp
        src/render/text/worker_probe/worker_probe.cpp)
    target_include_directories(gui_forms_text_worker_probe PRIVATE src/render/text/worker_probe)
    target_link_libraries(gui_forms_text_worker_probe PRIVATE gui_forms_text_engine Threads::Threads)
endif()

if(GUI_FORMS_BUILD_TEXT_PAINT_PROBE)
    find_package(Threads REQUIRED)
    add_executable(gui_forms_text_paint_probe
        benchmarks/text_paint_probe.cpp
        src/render/text/paint_probe/paint_probe.cpp
        src/render/text/worker_probe/worker_probe.cpp)
    target_include_directories(gui_forms_text_paint_probe PRIVATE
        src/render/text/paint_probe src/render/text/worker_probe)
    target_link_libraries(gui_forms_text_paint_probe PRIVATE gui_forms_text_engine stxlib::algorithms Threads::Threads)
endif()

if(GUI_FORMS_BUILD_TEXT_ENGINE_BENCHMARK)
    add_executable(gui_forms_text_engine_benchmark benchmarks/text_engine_benchmark.cpp)
    target_link_libraries(gui_forms_text_engine_benchmark PRIVATE gui_forms_text_engine gui_forms_core)
endif()

if(GUI_FORMS_BUILD_DOCUMENT_VIEW_BENCHMARK)
    add_executable(gui_forms_document_view_benchmark benchmarks/document_view_benchmark.cpp)
    target_link_libraries(gui_forms_document_view_benchmark PRIVATE gui_forms_core)
endif()

# Phase E, E5: the Gallery painted through the CPU Skia adapter, for
# .github/workflows/profile-linux.yml to sample (tools/profile_hot_paths.py).
if(GUI_FORMS_BUILD_PAINT_PROFILE)
    if(NOT TARGET gui_forms_skia OR NOT TARGET gui_forms_gallery_model)
        message(FATAL_ERROR "The paint profile workload requires Skia and GUI_FORMS_BUILD_GALLERY")
    endif()
    add_executable(gui_forms_paint_profile_workload benchmarks/paint_profile_workload.cpp)
    target_compile_definitions(gui_forms_paint_profile_workload PRIVATE
        GUI_FORMS_PROFILE_FONT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
    target_link_libraries(gui_forms_paint_profile_workload PRIVATE gui_forms_gallery_model gui_forms_skia)
endif()

# The blend and opaque study (2026-11-16): the Gallery's blend counters and
# frame times through the CPU Skia adapter, for tools/blend_study.py.
if(GUI_FORMS_BUILD_BLEND_STUDY)
    if(NOT TARGET gui_forms_skia OR NOT TARGET gui_forms_gallery_model)
        message(FATAL_ERROR "The blend study workload requires Skia and GUI_FORMS_BUILD_GALLERY")
    endif()
    add_executable(gui_forms_blend_study_workload benchmarks/blend_study_workload.cpp)
    target_compile_definitions(gui_forms_blend_study_workload PRIVATE
        GUI_FORMS_BLEND_STUDY_FONT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
    target_link_libraries(gui_forms_blend_study_workload PRIVATE gui_forms_gallery_model gui_forms_skia)
endif()

# Stage C5: the cost of shadow frames, alone and over a window workload.
# Built only when asked for.
add_executable(gui_forms_shadow_frames_benchmark EXCLUDE_FROM_ALL benchmarks/shadow_frames_benchmark.cpp)
target_link_libraries(gui_forms_shadow_frames_benchmark PRIVATE gui_forms_core)

# IMGUI_IMPLOT_STUDY B.7: the plots package's costs (pyramid build, pan and
# live frames, scatter thinning at a million points). Built only when asked
# for; run in Release.
add_executable(gui_forms_plot_performance_probe EXCLUDE_FROM_ALL benchmarks/plot_performance_probe.cpp)
target_link_libraries(gui_forms_plot_performance_probe PRIVATE gui_forms_plot_primitives)

# Audit addendum batch d: the per-operation cost of a window of several
# thousand controls (attach, pointer, focus, layout, paint, inherited reads,
# bulk clear). Built only when asked for; run in Release.
add_executable(gui_forms_tree_cost_benchmark EXCLUDE_FROM_ALL benchmarks/tree_cost_benchmark.cpp)
target_link_libraries(gui_forms_tree_cost_benchmark PRIVATE gui_forms_core)

# APP_WINDOW_RULES_DESIGN K.2: the creation-cost gate (a bare window, a popup
# and a generated form window; time and allocations against the committed
# baseline benchmarks/creation/creation_baseline.txt). Built only when asked
# for; run in Release on the M4.
if(GUI_FORMS_BUILD_FORM_COMPILER)
    add_executable(gui_forms_creation_benchmark EXCLUDE_FROM_ALL benchmarks/creation_benchmark.cpp)
    target_link_libraries(gui_forms_creation_benchmark PRIVATE gui_forms_controls gui_forms_host_headless)
    target_include_directories(gui_forms_creation_benchmark PRIVATE src/host/headless)
    gui_forms_add_form(gui_forms_creation_benchmark benchmarks/creation/creation_form.form
        CLASS CreationSettingsForm NAMESPACE form_generated)
endif()

# SK2: the time each host spends presenting a frame, which the native jobs
# measure (.github/workflows/native-platform.yml). Built only when asked for.
if(TARGET gui_forms_application)
    add_executable(gui_forms_present_profile_workload EXCLUDE_FROM_ALL benchmarks/present_profile_workload.cpp)
    target_link_libraries(gui_forms_present_profile_workload PRIVATE gui_forms_application)
endif()

if(GUI_FORMS_BUILD_BENCHMARKS)
    if(NOT APPLE OR NOT TARGET gui_forms_skia OR NOT TARGET gui_forms_gallery_model)
        message(FATAL_ERROR
            "The renderer benchmark requires macOS, Skia, and the Gallery model")
    endif()
    add_executable(gui_forms_renderer_benchmark
        benchmarks/renderer_benchmark.cpp
    )
    target_compile_definitions(gui_forms_renderer_benchmark PRIVATE
        GUI_FORMS_BENCH_REGULAR_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids.ttf"
        GUI_FORMS_BENCH_BOLD_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids-Bold.ttf"
    )
    target_link_libraries(gui_forms_renderer_benchmark PRIVATE
        gui_forms_gallery_model
        gui_forms_skia
    )
    add_custom_target(gui_forms_renderer_boundary_audit
        COMMAND ${CMAKE_COMMAND}
            -DCORE_LIBRARY=$<TARGET_FILE:gui_forms_core>
            -DSKIA_ADAPTER=$<TARGET_FILE:gui_forms_skia>
            -DBENCHMARK_BINARY=$<TARGET_FILE:gui_forms_renderer_benchmark>
            -DPUBLIC_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -DSKIA_ARCHIVE=$<TARGET_FILE:stx_skia::stx_skia>
            -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/renderer-boundary-audit.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_renderer_boundaries.cmake
        DEPENDS gui_forms_renderer_benchmark
        VERBATIM
    )
endif()


if(GUI_FORMS_BUILD_FUZZERS)
    # The codec fuzzers compile stx sources, so they need the stx checkout.
    if(GUI_FORMS_STX_SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "GUI_FORMS_BUILD_FUZZERS needs the stx source (cmake/Stx.cmake fetches it)")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "GUI_FORMS_BUILD_FUZZERS currently requires Clang libFuzzer")
    endif()
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
    check_cxx_source_compiles(
        "#include <cstddef>\nextern \"C\" int LLVMFuzzerTestOneInput(const unsigned char*, std::size_t) { return 0; }"
        GUI_FORMS_HAS_LIBFUZZER_RUNTIME
    )
    unset(CMAKE_REQUIRED_FLAGS)
    # The inflate and PNG decode harnesses are stx's (stx_zlib/fuzz, stx_png/fuzz).
    set(stx_zlib_dir ${GUI_FORMS_STX_SOURCE_DIR}/stx_zlib)
    set(stx_png_dir ${GUI_FORMS_STX_SOURCE_DIR}/stx_png)
    gui_forms_add_libfuzzer_target(inflate
        SOURCES ${stx_zlib_dir}/fuzz/inflate_fuzz.cpp ${stx_zlib_dir}/src/inflate.cpp ${stx_zlib_dir}/src/checksum.cpp
        INCLUDES ${stx_zlib_dir}/include
        STANDALONE_DEFINITION STX_ZLIB_FUZZ_STANDALONE)
    gui_forms_add_libfuzzer_target(inflate_protocol
        SOURCES fuzz/inflate_protocol_fuzz.cpp ${stx_zlib_dir}/src/inflate.cpp ${stx_zlib_dir}/src/checksum.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include ${stx_zlib_dir}/include)
    gui_forms_add_libfuzzer_target(deflate
        SOURCES fuzz/deflate_fuzz.cpp ${stx_zlib_dir}/src/inflate.cpp ${stx_zlib_dir}/src/deflate.cpp
                ${stx_zlib_dir}/src/checksum.cpp
        INCLUDES ${stx_zlib_dir}/include)
    gui_forms_add_libfuzzer_target(png_decode
        SOURCES ${stx_png_dir}/fuzz/png_decode_fuzz.cpp ${stx_png_dir}/src/decoder.cpp ${stx_png_dir}/src/encoder.cpp
                ${stx_zlib_dir}/src/inflate.cpp ${stx_zlib_dir}/src/deflate.cpp ${stx_zlib_dir}/src/checksum.cpp
        INCLUDES ${stx_png_dir}/include ${stx_zlib_dir}/include
        STANDALONE_DEFINITION STX_PNG_FUZZ_STANDALONE)
    gui_forms_add_libfuzzer_target(png_registry
        SOURCES fuzz/png_registry_fuzz.cpp src/core/resources/image_registry/image_registry.cpp
                ${stx_zlib_dir}/src/checksum.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include ${stx_zlib_dir}/include)

    # The theme sheet tokenizer, parser and value reader (stage T3), from
    # their own sources.
    gui_forms_add_libfuzzer_target(theme_sheet
        SOURCES
            fuzz/theme_sheet_fuzz.cpp
            src/core/theme_language/source_text/theme_source_text.cpp
            src/core/theme_language/diagnostics/theme_diagnostics.cpp
            src/core/theme_language/number/theme_number.cpp
            src/core/theme_language/tokenizer/theme_tokenizer.cpp
            src/core/theme_language/syntax_tree/theme_syntax_tree.cpp
            src/core/theme_language/syntax_tree/theme_sheet_parser.cpp
            src/core/theme_language/custom_properties/theme_custom_properties.cpp
            src/core/theme_language/value/theme_value.cpp
            src/core/theme_language/media/theme_media_query.cpp
            src/core/engine/engine_expression/engine_expression.cpp
            src/core/engine/theme_rules/colour_roles.cpp
            src/core/anatomy/types/anatomy_types.cpp
            src/core/schema/types/schema_types.cpp
            src/core/xml/xml_characters/xml_characters.cpp
            src/core/xml/types/xml_types.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # The layout XML reader (DK decision 43), built from its own sources so
    # the sanitizers cover exactly the reader and writer.
    gui_forms_add_libfuzzer_target(xml_reader
        SOURCES fuzz/xml_reader_fuzz.cpp src/core/xml/types/xml_types.cpp src/core/xml/xml_characters/xml_characters.cpp
                src/core/xml/xml_reader/xml_reader.cpp src/core/xml/xml_writer/xml_writer.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # The theme manifest's JSON reader (stage T3, P10), from its own source.
    gui_forms_add_libfuzzer_target(json_reader
        SOURCES fuzz/json_reader_fuzz.cpp src/core/json/json_reader/json_reader.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # Whole subsystems, linked rather than rebuilt (audit N115, I3): the
    # sanitizers reach into the libraries only when the tree is configured
    # with them (cmake/FuzzTargets.cmake).
    gui_forms_add_libfuzzer_target(theme_compile SOURCES fuzz/theme_compile_fuzz.cpp LIBRARIES gui_forms_controls)
    gui_forms_add_libfuzzer_target(theme_manifest SOURCES fuzz/theme_manifest_fuzz.cpp LIBRARIES gui_forms_core)
    gui_forms_add_libfuzzer_target(form_document SOURCES fuzz/form_document_fuzz.cpp LIBRARIES gui_forms_controls)
    gui_forms_add_libfuzzer_target(dock_layout SOURCES fuzz/dock_layout_fuzz.cpp LIBRARIES gui_forms_core)
    gui_forms_add_libfuzzer_target(clipboard_image SOURCES fuzz/clipboard_image_fuzz.cpp LIBRARIES gui_forms_core)
    gui_forms_add_libfuzzer_target(rtf_import SOURCES fuzz/rtf_import_fuzz.cpp
        LIBRARIES gui_forms_core stx_rtf2md::stx_rtf2md)
    if(TARGET stx_vorbis::stx_vorbis_stb)
        gui_forms_add_libfuzzer_target(vorbis SOURCES fuzz/vorbis_fuzz.cpp LIBRARIES stx_vorbis::stx_vorbis_stb)
    endif()
endif()

# Installed consumers need the notices for the code incorporated in the library.
if(GUI_FORMS_BUILD_AUDIO)
    include(cmake/Audio.cmake)
endif()

# No installed GUI.Forms program or library needs a run-time search path for
# another: every first-party library is a static archive linked into its
# module (one module, 2026-11-15), and the C ABI library holds the engine
# itself.

if(TARGET gui_forms_application)
    target_link_libraries(gui_forms_application PUBLIC GUIForms::Threading)
    # STXlib's state (cmake/Stx.cmake): a static archive since the one-module
    # decision, so the program holds the one allocator, identity issuer and
    # SIMD selection, which the engine's copy of STXlib's static code (the
    # row height index, src/runtime/stxlib_state_probe.cpp) and the program's
    # own STXlib code both reach.
    target_link_libraries(gui_forms_application PUBLIC stxlib::boundary stxlib::state)
endif()
# The text masks join the Application library's archives
# (GUI_FORMS_APPLICATION_ARCHIVES) before the package reads them, so the
# installed libgui_forms_application.a holds them.
include(cmake/ApplicationText.cmake)
include(cmake/ApplicationPackage.cmake)

include(cmake/CAbi.cmake)

install(FILES cmake/llvm22.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
# The SDK carries the stx prefix it was built with whole (libraries, headers,
# package configs, and the licences and provenance under share/licenses/<package>),
# so GUIFormsConfig's find_dependency calls find the packages beside GUI.Forms.
# The licences are also copied beside GUI.Forms' others.
install(DIRECTORY "${GUI_FORMS_STX_PREFIX_DIR}/" DESTINATION .
        PATTERN gui-forms-stx.stamp EXCLUDE)
set(gui_forms_stx_licenses "${GUI_FORMS_STX_PREFIX_DIR}/share/licenses")
install(FILES ${gui_forms_stx_licenses}/stx_zlib/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_zlib-LICENSE.txt)
install(FILES ${gui_forms_stx_licenses}/stx_zlib/LICENSE-puff.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_zlib-LICENSE-puff.txt)
install(FILES ${gui_forms_stx_licenses}/stx_zlib/LICENSE-zlib.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_zlib-LICENSE-zlib.txt)
install(FILES ${gui_forms_stx_licenses}/stx_zlib/PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_zlib-PROVENANCE.md)
install(FILES ${gui_forms_stx_licenses}/stx_png/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_png-LICENSE.txt)
install(FILES ${gui_forms_stx_licenses}/stx_rtf2md/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_rtf2md-LICENSE.txt)
install(FILES ${gui_forms_stx_licenses}/stx_rtf2md/PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_rtf2md-PROVENANCE.md)
install(FILES ${gui_forms_stx_licenses}/stx_png/PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_png-PROVENANCE.md)
install(FILES ${gui_forms_stx_licenses}/stx_conv/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_conv-LICENSE.txt)
install(FILES ${gui_forms_stx_licenses}/stx_conv/PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_conv-PROVENANCE.md)
install(FILES ${gui_forms_stx_licenses}/stx_raster3d/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_raster3d-LICENSE.txt)
install(FILES ${gui_forms_stx_licenses}/stx_raster3d/PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_raster3d-PROVENANCE.md)
# STXlib's licences, as stx installs them beside its packages (cmake/Stxlib.cmake
# there): STXlib, stx_btree (the row height index's sequence tree),
# stx_minmallocpp (STXlib's allocator, STXlib's own code; its NOTICE carries
# mimalloc's MIT notice, which it derives from), tzdata (its zone data) and
# the Unicode License v3 of its case-folding tables (CaseFolding.txt). That
# one is installed as stxlib-unicode-LICENSE.txt: Unicode-LICENSE.txt, the
# grapheme tables' licence below, differs from unicode-LICENSE.txt only in
# case, and the two would overwrite each other on macOS and Windows.
install(FILES ${gui_forms_stx_licenses}/stxlib/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stxlib-LICENSE.txt)
install(FILES ${gui_forms_stx_licenses}/stxlib/stx_btree-LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_btree-LICENSE.txt)
install(FILES ${gui_forms_stx_licenses}/stxlib/stx_btree-NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_btree-NOTICE.txt)
install(FILES ${gui_forms_stx_licenses}/stxlib/stx_minmallocpp-NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_minmallocpp-NOTICE.txt)
install(FILES ${gui_forms_stx_licenses}/stxlib/tzdata-LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME tzdata-LICENSE.txt)
install(FILES ${gui_forms_stx_licenses}/stxlib/unicode-LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stxlib-unicode-LICENSE.txt)
# STXlib 23a0025 (layout 13): the glyph face the text rasterizers draw with
# (stxlib::glyphs; its NOTICE carries the notices of ttf-parser, Skrifa and
# read-fonts and the FreeType Project's design it converts), and
# stxlib::markdown (MD4C's licence in its NOTICE, and the HTML Standard's
# named character references), both compiled into stxlib_algorithms.
install(FILES ${gui_forms_stx_licenses}/stxlib/glyphs-NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stxlib-glyphs-NOTICE.txt)
install(FILES ${gui_forms_stx_licenses}/stxlib/markdown-NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stxlib-markdown-NOTICE.txt)
install(FILES ${gui_forms_stx_licenses}/stxlib/whatwg-entities-LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME whatwg-entities-LICENSE.txt)
# The stock icons are Heroicons vectors converted into the library at build
# time (assets/icons/heroicons/PROVENANCE.md); their MIT licence ships with it.
install(FILES assets/icons/heroicons/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME Heroicons-LICENSE.txt)
install(FILES assets/icons/heroicons/PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME Heroicons-PROVENANCE.md)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms)
# Every third-party component the SDK and the programs ship, with its licence
# (THIRD_PARTY_NOTICES.md).
install(FILES THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms)
# The stx package keeps the stx source's licence files at their source paths.
set(gui_forms_stx_notices "${GUI_FORMS_STX_BUILD_PREFIX_DIR}/share/stx_text/notices")
install(FILES "${gui_forms_stx_notices}/third_party/unicode/LICENSE.txt" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME Unicode-LICENSE.txt)
if(GUI_FORMS_ENABLE_SKIA)
    install(FILES "${gui_forms_stx_notices}/stx_skia/LICENSE-Skia" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME Skia-LICENSE.txt)
    install(FILES "${gui_forms_stx_notices}/stx_skia/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_skia-LICENSE.txt)
endif()


# Windows: the installed DLLs embed the LLVM runtimes, winpthreads and the
# MinGW-w64 CRT (add_link_options(-static) above). Their licences come from
# the toolchain prefix, the directory above the compiler's bin (the CLANG64
# tree); the LLVM exception does not require them for embedded runtime code,
# and they are installed anyway. The MSVC target embeds Microsoft's runtime
# objects instead, under the Visual Studio licence (Stage B, temporary).
if(MINGW)
    get_filename_component(_gf_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_gf_toolchain_prefix "${_gf_toolchain_bin}" DIRECTORY)
    set(_gf_runtime_licence_sources
        libc++/LICENSE libunwind/LICENSE compiler-rt/LICENSE winpthreads/COPYING
        crt/COPYING.MinGW-w64-runtime.txt)
    set(_gf_runtime_licence_names
        libc++-LICENSE.txt libunwind-LICENSE.txt compiler-rt-LICENSE.txt winpthreads-COPYING.txt
        mingw-w64-runtime-COPYING.txt)
    foreach(_gf_source _gf_name IN ZIP_LISTS _gf_runtime_licence_sources _gf_runtime_licence_names)
        set(_gf_licence "${_gf_toolchain_prefix}/share/licenses/${_gf_source}")
        if(EXISTS "${_gf_licence}")
            install(FILES "${_gf_licence}" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME ${_gf_name})
        else()
            message(WARNING "Runtime licence ${_gf_licence} not found; the installed SDK will lack ${_gf_name}")
        endif()
    endforeach()
endif()
# Linux: the installed shared libraries embed libc++, libc++abi and LLVM's
# libunwind (cmake/llvm22.cmake links their archives), so the SDK carries
# their notices, the checked-in LLVM 22.1.8 files (packaging/licenses/llvm).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES packaging/licenses/llvm/libcxx-LICENSE.TXT DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME libcxx-LICENSE.txt)
    install(FILES packaging/licenses/llvm/libcxxabi-LICENSE.TXT DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME libcxxabi-LICENSE.txt)
    install(FILES packaging/licenses/llvm/libunwind-LICENSE.TXT DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME libunwind-LICENSE.txt)
endif()



# Programs that carry the font licences share one output directory, so one
# fonts/ beside them holds the licences and the attribution
# (GUI_FORMS_PROGRAM_FONT_NOTICES) for the Gallery and its labs, the
# Correspondence demonstration and the File Manager demoboard. No face file is
# staged: the programs read the faces from the embedded pack.
# Each program used to copy its list there after linking, so on a cold
# parallel build several post-build steps wrote the same files at once and
# one could fail ("Error copying file"; seen on Windows and on Linux). One
# target stages every file once, each through its own build rule (one writer
# per file, recopied when the source changes), and the programs depend on it.
# macOS bundles carry their fonts as resources instead.
set(gui_forms_font_programs)
set(gui_forms_font_sources)
foreach(gallery_program IN ITEMS
        gui_forms_gallery_windows
        gui_forms_vsync_lab_windows
        gui_forms_visual_inspector_lab_windows
        gui_forms_layered_material_lab_windows
        gui_forms_custom_chrome_lab_windows
        gui_forms_typography_scale_lab_windows
        gui_forms_responsive_tracks_lab_windows
        gui_forms_connected_controls_lab_windows
        gui_forms_seam_proximity_lab_windows
        gui_forms_correspondence_windows
        gui_forms_gallery_linux
        gui_forms_correspondence_linux)
    if(TARGET ${gallery_program})
        list(APPEND gui_forms_font_programs ${gallery_program})
        list(APPEND gui_forms_font_sources ${GUI_FORMS_PROGRAM_FONT_NOTICES})
    endif()
endforeach()
foreach(demoboard_program IN ITEMS gui_forms_file_manager_demoboard_windows gui_forms_file_manager_demoboard_linux)
    if(TARGET ${demoboard_program})
        list(APPEND gui_forms_font_programs ${demoboard_program})
        list(APPEND gui_forms_font_sources ${GUI_FORMS_PROGRAM_FONT_NOTICES})
    endif()
endforeach()
if(gui_forms_font_programs)
    set(gui_forms_program_directory "${CMAKE_CURRENT_BINARY_DIR}")
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(gui_forms_program_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
    list(REMOVE_DUPLICATES gui_forms_font_sources)
    set(gui_forms_staged_fonts)
    set(gui_forms_staged_font_names)
    foreach(font_file IN LISTS gui_forms_font_sources)
        get_filename_component(font_name "${font_file}" NAME)
        if(font_name IN_LIST gui_forms_staged_font_names)
            message(FATAL_ERROR "Two bundled font files share the name ${font_name}")
        endif()
        list(APPEND gui_forms_staged_font_names "${font_name}")
        set(staged_font "${gui_forms_program_directory}/fonts/${font_name}")
        add_custom_command(OUTPUT "${staged_font}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${gui_forms_program_directory}/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${font_file}" "${staged_font}"
            DEPENDS "${font_file}"
            VERBATIM)
        list(APPEND gui_forms_staged_fonts "${staged_font}")
    endforeach()
    add_custom_target(gui_forms_program_fonts DEPENDS ${gui_forms_staged_fonts})
    foreach(font_target IN LISTS gui_forms_font_programs)
        get_target_property(font_target_directory ${font_target} RUNTIME_OUTPUT_DIRECTORY)
        if(font_target_directory AND NOT font_target_directory STREQUAL gui_forms_program_directory)
            message(FATAL_ERROR "${font_target} is built outside ${gui_forms_program_directory}, where its fonts are staged")
        endif()
        add_dependencies(${font_target} gui_forms_program_fonts)
    endforeach()
endif()

# The targets that compile the red/blue swap loops' replacements link stx_skia::swizzle: the
# core (material images), the controls (the raster canvas), the Windows drawing C API (its
# objects, which the C ABI library holds) and the Windows host (the test programs that
# compile windows_host.cpp link it in gui_forms_tests).
foreach(gui_forms_swizzle_target IN ITEMS gui_forms_core gui_forms_controls
        gui_drawing_c_api_objects gui_drawing_c_api gui_forms_host_windows)
    if(TARGET ${gui_forms_swizzle_target})
        target_link_libraries(${gui_forms_swizzle_target} PRIVATE stx_skia::swizzle)
    endif()
endforeach()


# Every test carries one area label (cmake/TestAreas.cmake, tools/test_areas.json).

# The focus labs (claude/perf-focus-labs, benchmarks/focus_labs): measurement programs,
# not tests, so no CTest entry. Each checks its rearranged kernels against the library's
# own for equal output, then times both. composite_lab compiles the compositor's own
# translation unit; bake_lab links as gui_forms_engine_material_tests does, exec_lab as
# gui_drawing_skia_tests does. Since the perf fold (cp29) the library carries the
# rearrangements, so the ratios measure what is left.
option(GUI_FORMS_BUILD_FOCUS_LABS "Build the benchmarks/focus_labs programs" OFF)
if(GUI_FORMS_BUILD_FOCUS_LABS)
    add_executable(focus_composite_lab benchmarks/focus_labs/composite_lab.cpp
        src/core/text/composite/text_composite.cpp)
    target_include_directories(focus_composite_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core/text/composite)
    target_compile_features(focus_composite_lab PRIVATE cxx_std_20)
    add_executable(focus_bake_lab benchmarks/focus_labs/bake_lab.cpp)
    target_link_libraries(focus_bake_lab PRIVATE gui_forms_controls)
    if(TARGET gui_drawing_skia)
        add_executable(focus_exec_lab benchmarks/focus_labs/exec_lab.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
        target_link_libraries(focus_exec_lab PRIVATE gui_drawing_skia gui_forms_core)
        target_include_directories(focus_exec_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    endif()
endif()

# Floating-point contraction. Clang's default fuses a * b + c within an
# expression where the target has a fused multiply-add, which arm64 always has
# and the x86-64 baseline never does, so the same source gives different bits
# on the two. The owner's ruling of 2026-11-01 turned contraction off in every
# first-party target; the ruling of 2026-11-16 keeps that for referencing only
# ("turn it off for referencing, and leave it on for actual deployment"):
# - GUI_FORMS_REFERENCE_ARITHMETIC ON (reference and conformance builds):
#   every target this directory defines gets -ffp-contract=off and the
#   definition GUI_FORMS_REFERENCE_ARITHMETIC, which the raster, engine,
#   material, theme-kernel and effect sources test to add
#   `#pragma clang fp contract(off)` as well;
# - OFF (deployment): neither, so those paths contract as the compiler
#   chooses, and their bytes may differ between CPUs by a step where a
#   fused multiply-add rounds differently.
# Gradient colours are the same either way: they interpolate in linear light
# in integers (gui_forms/types/linear_light/linear_light.hpp).
# A few sources keep contraction off in every build, listed with their reasons
# after the loop below: engine_math, whose stated error bounds assume unfused
# operations, and the value paths, whose results are numbers (parsed text,
# layout lengths, theme values, motion samples), not pixels.
# A kernel that wants fusion spells it std::fma, so the choice is visible and
# gives the same bits everywhere. The stx libraries (stx_zlib, stx_png,
# stx_conv, stx_rtf2md, stx_vorbis) set -ffp-contract=off on their own targets
# in their own build, and stx_skia on its library, in every build: the stx
# package is built apart (cmake/stx_package/) and does not read this option;
# its raster pipeline's explicit fused stages are the FMA log's entries.
# miniaudio is compiled inside src/audio/audio.cpp and so follows the option
# with it.
# One static test runtime for the test programs that use only the toolkit's
# own libraries, before the options below read their links
# (cmake/TestRuntime.cmake).
include(cmake/Hardening.cmake)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    get_property(gui_forms_first_party_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(gui_forms_first_party_target IN LISTS gui_forms_first_party_targets)
        get_target_property(gui_forms_target_type ${gui_forms_first_party_target} TYPE)
        if(gui_forms_target_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            if(GUI_FORMS_REFERENCE_ARITHMETIC)
                target_compile_options(${gui_forms_first_party_target} PRIVATE
                    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-ffp-contract=off>)
                target_compile_definitions(${gui_forms_first_party_target} PRIVATE
                    GUI_FORMS_REFERENCE_ARITHMETIC=1)
            endif()
            # Hardening and the warning level (cmake/Hardening.cmake, D11).
            gui_forms_harden_target(${gui_forms_first_party_target} ${gui_forms_target_type})
            # Warnings are failures (owner, 2026-11-17) for the library and its
            # programs; gui_forms_tests turns them on for the tests.
            if(GUI_FORMS_WARNINGS_AS_ERRORS)
                # A #warning is a deliberate notice (the unverified effect
                # programs say so on purpose): it stays a warning.
                target_compile_options(${gui_forms_first_party_target} PRIVATE
                    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Werror>
                    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wno-error=\#warnings>)
            endif()
            # A view (std::string_view, std::span) bound or assigned to a
            # temporary dangles: PartPaint::text twice held a temporary
            # std::string (CorrespondenceView, PropertyList) and painting read
            # freed memory. Clang reports these on by default; here they fail
            # the build. A value assigned to itself is never meant: a property
            # forwarder whose parameter shadowed its cell
            # (`set_visible(bool visible) { visible = visible; }`) stored
            # nothing and the Gallery hid nothing. -Wself-assign is off by
            # default; here it fails the build.
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                target_compile_options(${gui_forms_first_party_target} PRIVATE
                    $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Werror=dangling-assignment-gsl -Werror=dangling-gsl
                        -Werror=self-assign -Werror=self-assign-field>)
            endif()
        endif()
    endforeach()
    # Contraction off in every build, whatever GUI_FORMS_REFERENCE_ARITHMETIC
    # says (each source also carries the Clang pragma; GCC reads only the flag):
    # - engine_math: its stated error bounds (engine_math.hpp) assume each
    #   operation rounds on its own;
    # - engine_expression and theme_value: theme values and expressions, the
    #   same text giving the same number on every host;
    # - value_text: a ValueBar's typed value, parsed as the expression engine
    #   computes;
    # - scrub_gesture: a pointer script's scrubbed values;
    # - layout_style_reader: layout lengths, which place edges and hit tests;
    # - theme_motion_lowering and motion_value: motion parameters and samples,
    #   whose settling decides when an animation ends.
    set_source_files_properties(
        src/core/engine/engine_math/engine_math.cpp
        src/core/engine/engine_expression/engine_expression.cpp
        src/core/theme_language/value/theme_value.cpp
        src/controls/range_control/value_bar/value_text.cpp
        src/core/drag_engine/scrub_gesture.cpp
        src/core/layout/style_reader/layout_style_reader.cpp
        src/core/theme_language/motion/theme_motion_lowering.cpp
        src/core/engine/motion_value/motion_value.cpp
        PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()

# The test cache's description, after the last target (cmake/TestCache.cmake).
gui_forms_describe_test_cache()
