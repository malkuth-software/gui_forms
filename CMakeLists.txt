cmake_minimum_required(VERSION 3.25)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum supported macOS")
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Supported Apple architecture")
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "GUI.Forms supports Apple silicon arm64 only")
    endif()
endif()
project(GUIForms VERSION 0.6.0 LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   NOT CMAKE_CXX_COMPILER_VERSION MATCHES "^22\\.1\\.")
    message(FATAL_ERROR "GUI.Forms requires LLVM 22.1.x; use cmake/llvm22.cmake")
endif()
if(APPLE)
    add_compile_options(-Werror=unguarded-availability -Werror=unguarded-availability-new)
endif()
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GUI_FORMS_BUILD_GALLERY "Build the native control gallery" ${PROJECT_IS_TOP_LEVEL})
option(GUI_FORMS_BUILD_TESTS "Build automated proving-slice tests" ${PROJECT_IS_TOP_LEVEL})
# The Gallery model (the control specimen catalogue: gui_forms_gallery_model)
# apart from the programs. GUI.Forms' own checks read it (the schema export,
# anatomy coverage, the Gallery page, interaction and accessibility tests),
# so it builds with the tests; the programs (GUI_FORMS_BUILD_GALLERY) link it.
option(GUI_FORMS_BUILD_GALLERY_MODEL "Build the Gallery model and the tests that read it" ${GUI_FORMS_BUILD_TESTS})
option(GUI_FORMS_INSTALL_GALLERY_MODEL "Install the Gallery model as the GalleryModel package component" OFF)
# Checked borrowed views (borrow sanitizer, gui_forms/borrow/borrowed_view/
# borrowed_view.hpp; owner rulings 2026-11-11): the sanitizer tree turns it
# on (tools/ci_build.py --sanitize). It changes the borrowed and lending
# types, so the whole program, consumers included, is built one way: the
# definition is public on gui_forms_core and every target here sees it.
option(GUI_FORMS_CHECKED_BORROWS "Check borrowed views against their owners (the sanitizer tree)" OFF)
if(GUI_FORMS_CHECKED_BORROWS)
    add_compile_definitions(GUI_FORMS_CHECKED_BORROWS=1)
endif()

# Tests that open native windows which take focus, capture or activation fall
# in two groups, by what they need from the desktop:
# - `foreground`: a desktop of their own is enough. On a development machine
#   they run on a private desktop of the same window station (projects
#   tools/run_on_desktop.py), where nothing is ever foreground, so they never
#   take the user's foreground and may run in parallel, without a lock.
# - `input_desktop`: they need the desktop the user sees. The UI Automation
#   and MSAA clients (off the input desktop every UI Automation pattern call
#   waits 2 s inside UIAutomationCore) and the compositor probe (DWM does not
#   composite another desktop). They carry the desktop_foreground resource
#   lock, which serializes them within one ctest run; across ctest runs
#   (several workers on one machine) a launcher holds a machine-wide lock.
# Each group has a launcher, a CMake list run in front of the test executable
# (CMake 3.29 or later, for TEST_LAUNCHER). The cache value wins; when it is
# empty, the environment variable of the same name is read at every
# configure. For example:
#   GUI_FORMS_FOREGROUND_LAUNCHER="python;-B;D:/malkuth/projects/tools/run_on_desktop.py;--"
#   GUI_FORMS_INPUT_DESKTOP_LAUNCHER="python;-B;D:/malkuth/projects/tools/foreground_lock.py;--"
# CI leaves both empty: each runner has a desktop of its own.
set(GUI_FORMS_FOREGROUND_LAUNCHER "" CACHE STRING
    "Command that runs a foreground test on a private desktop (empty: the environment's, else none)")
set(GUI_FORMS_INPUT_DESKTOP_LAUNCHER "" CACHE STRING
    "Command that runs an input-desktop test under a machine-wide lock (empty: the environment's, else none)")
# Sets the TEST_LAUNCHER of test `test_name` (whose executable target has the
# same name) from cache variable `variable`, or from the environment variable
# of that name when the cache value is empty.
function(gui_forms_desktop_launcher test_name variable)
    set(launcher "${${variable}}")
    if(NOT launcher)
        set(launcher "$ENV{${variable}}")
    endif()
    if(NOT launcher)
        return()
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.29)
        message(FATAL_ERROR "${variable} needs CMake 3.29 or later (TEST_LAUNCHER)")
    endif()
    set_property(TARGET ${test_name} PROPERTY TEST_LAUNCHER ${launcher})
endfunction()
function(gui_forms_foreground_test test_name)
    set_property(TEST ${test_name} APPEND PROPERTY LABELS foreground)
    gui_forms_desktop_launcher(${test_name} GUI_FORMS_FOREGROUND_LAUNCHER)
endfunction()
function(gui_forms_input_desktop_test test_name)
    set_property(TEST ${test_name} APPEND PROPERTY LABELS input_desktop)
    set_property(TEST ${test_name} APPEND PROPERTY RESOURCE_LOCK desktop_foreground)
    gui_forms_desktop_launcher(${test_name} GUI_FORMS_INPUT_DESKTOP_LAUNCHER)
endfunction()
option(GUI_FORMS_BUILD_AUDIO "Build portable PCM audio service" OFF)
set(GUI_FORMS_AUDIO_VORBIS_BACKEND "stx" CACHE STRING "Ogg decoder: stx (default) or stb")
set_property(CACHE GUI_FORMS_AUDIO_VORBIS_BACKEND PROPERTY STRINGS stx stb)
option(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT "Build the development audio loop transport" OFF)
option(GUI_FORMS_ENABLE_SKIA "Build the private CPU-only Skia adapter" ON)
option(GUI_FORMS_ENABLE_MACOS_HOST "Build the native AppKit host" ${APPLE})
option(GUI_FORMS_ENABLE_WINDOWS_HOST "Build the native Win32 host" ${WIN32})
option(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB
       "Enable bounded transactional DIB frames in the Windows development host" OFF)
option(GUI_FORMS_ENABLE_LINUX_HOST "Build the native X11 host with the private CPU renderer" OFF)
# ADR-004: the Skia painter, the C drawing API and the Win32 host all draw
# text with the shared HarfBuzz/FreeType engine.
if(GUI_FORMS_ENABLE_SKIA OR GUI_FORMS_ENABLE_WINDOWS_HOST)
    set(GUI_FORMS_HARFBUZZ_TEXT_DEFAULT ON)
else()
    set(GUI_FORMS_HARFBUZZ_TEXT_DEFAULT OFF)
endif()
option(GUI_FORMS_ENABLE_HARFBUZZ_TEXT
       "Build the pinned HarfBuzz/FreeType text engine"
       ${GUI_FORMS_HARFBUZZ_TEXT_DEFAULT})
option(GUI_FORMS_BUILD_FUZZERS "Build libFuzzer entry points" OFF)
# Stage FD5: the program that generates a form document's C++ header; the SDK
# installs it. A cross build has no host to run it on.
if(CMAKE_CROSSCOMPILING)
    set(GUI_FORMS_FORM_COMPILER_DEFAULT OFF)
else()
    set(GUI_FORMS_FORM_COMPILER_DEFAULT ON)
endif()
option(GUI_FORMS_BUILD_FORM_COMPILER "Build and install the form compiler" ${GUI_FORMS_FORM_COMPILER_DEFAULT})
# The tests whose form header the form compiler generates at build time
# (gui_forms_add_form). The state-machine gate turns them off: it parses
# sources syntax-only and builds only generated headers, and this header
# would make it compile and link the whole library to run the compiler.
option(GUI_FORMS_BUILD_GENERATED_FORM_TESTS "Build the tests that generate a form header at build time" ON)
option(GUI_FORMS_BUILD_BENCHMARKS "Build measurement harnesses" OFF)
option(GUI_FORMS_BUILD_DOCUMENT_VIEW_BENCHMARK "Build the renderer-neutral D1 measurement harness" OFF)
option(GUI_FORMS_BUILD_TEXT_LAYOUT_PROBE "Build the private text shaping diagnostic" OFF)
option(GUI_FORMS_BUILD_LABEL_LAYOUT_PROBE "Build the fixed Label measurement diagnostic" OFF)
option(GUI_FORMS_BUILD_TEXT_WORKER_PROBE "Build the private text worker lifecycle proof" OFF)
option(GUI_FORMS_BUILD_TEXT_PAINT_PROBE "Build the private prepared glyph raster proof" OFF)
option(GUI_FORMS_BUILD_TEXT_RASTER_PROFILE_PROBE "Build the diagnostic text rasterization profile probe" OFF)
option(GUI_FORMS_BUILD_TEXT_ENGINE_BENCHMARK "Build the shared text engine cost benchmark" OFF)
option(GUI_FORMS_BUILD_PAINT_PROFILE "Build the Gallery paint workload that the Linux profiling job samples" OFF)
option(GUI_FORMS_BUILD_PREPARED_TEXT "Build the development prepared-text service" OFF)
option(GUI_FORMS_BUILD_TEXT_MASKS "Build the development logical text-mask service" OFF)
option(GUI_FORMS_INSTALL_TEXT_STACK "Install and export the pinned FreeType and HarfBuzz (cmake/TextStackExport.cmake)" OFF)
option(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS "Instrument private text shaping phases" OFF)
option(GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE "Record private run directions for geometry comparison" OFF)
option(GUI_FORMS_BUILD_FILE_MANAGER_DEMOBOARD
       "Build the deterministic native File Manager first-party consumer" OFF)

# HarfBuzz's CMakeLists sets THREADS_PREFER_PTHREAD_FLAG and caches that
# -pthread works. Without the same preference here, a tree's first configure
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
# (libgcc_s, libstdc++). Each DLL then holds its own copy of those runtimes;
# the C++ boundaries between DLLs and their consumers (Threading,
# Application) pass heap blocks, which every copy takes from the one UCRT
# heap, and exceptions, whose propagation across the Threading boundary
# gui_forms_threading_tests checks (a worker entry's failure is rethrown by
# join in the test executable). tools/package_programs.py rejects a package
# that imports a GCC or LLVM runtime DLL.
if(WIN32 AND NOT MSVC)
    add_link_options(-static)
endif()
# Linker pruning for the Release DLLs (MinGW). Every function and datum gets
# its own section and each DLL drops the sections nothing reaches from its
# exports. Sections the localizable-literal plugin and the effect pass write
# (.gfloc) are not COMDAT and are always kept; the code that remains is
# unchanged. Identical-code folding is left off: it would merge functions
# whose addresses the code may compare, and it saved nothing measurable here.
if(MINGW)
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    add_link_options($<$<AND:$<CONFIG:Release>,$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>>:LINKER:--gc-sections>)
endif()
include(CMakePackageConfigHelpers)
# Everything from malkuth-software/stx at stx.lock's commit, as one built
# package this build links and never compiles (cmake/Stx.cmake,
# cmake/stx_package/): the stx libraries (stx_zlib, stx_png, stx_conv with its
# SIMD level stx_auxlib::simd, the rich-text library stx_rtf2md of
# notes:planning/stages/RT-rich-text/PLAN.md and, for audio builds, stx_vorbis);
# stx_skia, the CPU Skia derivative the Skia adapters draw with (without the
# adapters only its red/blue swap kernel, stx_skia::swizzle, with which the
# core, the controls and the hosts swap RGBA and BGRA rows); and stx_text, the
# third-party sources of stx third_party/ (owner, 2026-11-05: this repository
# carries none) compiled with GUI.Forms' options. Imported targets' headers are
# system headers here.
include(cmake/Stx.cmake)
# stx_btree's header-only sequence tree from malkuth-software/stx_auxlib at
# stx_auxlib.lock's commit, which the controls' row height index adapts
# (cmake/StxAuxlib.cmake).
include(cmake/StxAuxlib.cmake)
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

add_library(gui_forms_threading SHARED
    src/core/threading/atomic_pool/atomic_pool.c
    src/core/threading/worker.cpp)
add_library(GUIForms::Threading ALIAS gui_forms_threading)
set_target_properties(gui_forms_threading PROPERTIES
    EXPORT_NAME Threading OUTPUT_NAME gui_forms_threading VERSION 0.1.0 SOVERSION 0)
target_include_directories(gui_forms_threading PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(gui_forms_threading PUBLIC cxx_std_20)
target_link_libraries(gui_forms_threading PRIVATE Threads::Threads)
# Exports only what gui_forms/threading.hpp marks GUI_FORMS_THREADING_API
# (gui_forms/export/threading_export.hpp; CP29 handoff section 2.2): through
# the generated export list at the link on ELF and Mach-O
# (gui_forms_export_list below), dllexport on Windows. Inline functions are
# hidden: each consumer compiles its own copy, and the exceptions crossing
# the boundary (a worker entry's failure, rethrown by join, and
# std::logic_error) carry the typeinfo of their own image or of the C++
# runtime, never of an inline function here.
target_compile_definitions(gui_forms_threading PRIVATE GUI_FORMS_THREADING_BUILD=1)
set_target_properties(gui_forms_threading PROPERTIES VISIBILITY_INLINES_HIDDEN YES)
if(WIN32)
    target_compile_definitions(gui_forms_threading PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(gui_forms_threading PRIVATE winpthread)
endif()

# A shipped shared library exports only the declarations its macro marks
# (CP29 handoff section 2.2; coordinator, 2026-11-12): on ELF and Mach-O
# through the export list tools/export_annotations.py generates from the
# marks (cmake/exports/<library>.map, a version script, and
# cmake/exports/<library>.exports, an -exported_symbols_list; `check`
# fails when a list and the marks differ), on Windows through the macro's
# dllexport. The archives are compiled with default visibility, because the
# shared test runtime (cmake/TestRuntime.cmake) holds the same archives and
# exports everything to the tests, which call private functions; hidden
# visibility is fixed per object at compile time and would hide them from
# it as well. The list leaves everything else local, so -dead_strip and
# --gc-sections still drop what no export reaches.
function(gui_forms_export_list target)
    set(list_stem ${CMAKE_CURRENT_SOURCE_DIR}/cmake/exports/${target})
    if(APPLE)
        target_link_options(${target} PRIVATE "LINKER:-exported_symbols_list,${list_stem}.exports")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${list_stem}.exports)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${target} PRIVATE "LINKER:--version-script=${list_stem}.map")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${list_stem}.map)
    endif()
endfunction()
gui_forms_export_list(gui_forms_threading)

# The archives the Application library (and the shared test runtime) holds
# whole, compiled with GUI_FORMS_API_BUILD (dllexport on Windows). Inline
# functions are hidden: each consumer compiles its own copy, no first-party
# code compares the address of an inline function across images, and a
# static local of an inline function keeps default visibility (Clang's
# -fvisibility-inlines-hidden-static-local-var is not used). Hiding them
# keeps the many inline definitions out of the test runtime's dynamic
# symbol table and spares dyld their weak-definition coalescing at load; on
# ELF the compiler also calls them directly instead of through the PLT. On
# ELF each function and datum gets its own section, so the Application
# library's --gc-sections drops what no export reaches (Mach-O's -dead_strip
# needs no flag; MinGW Release has the sections already, above).
function(gui_forms_application_archive target)
    target_compile_definitions(${target} PRIVATE GUI_FORMS_API_BUILD=1)
    set_target_properties(${target} PROPERTIES VISIBILITY_INLINES_HIDDEN YES)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    endif()
endfunction()

# The usage requirement of a shipped shared library holding those archives:
# its consumers import the marked declarations (dllimport on Windows), it
# exports only them (its export list on ELF and Mach-O), and the linker drops
# the sections no export reaches.
function(gui_forms_application_library target)
    gui_forms_application_archive(${target})
    target_compile_definitions(${target} INTERFACE GUI_FORMS_API_IMPORT=1)
    gui_forms_export_list(${target})
    if(APPLE)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:LINKER:-dead_strip>)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:LINKER:--gc-sections>)
    endif()
endfunction()

# The bundled fallback faces (owner ruling 2026-11-11): the seven Noto faces and their OFL texts
# form their own install component, fallback_fonts, installed when GUI_FORMS_INSTALL_FALLBACK_FONTS
# is ON (the default, so the SDK carries them). An app package chooses for itself
# ("fallback_fonts" per program in packaging/programs.json). The runtime works without them: they
# are optional faces, so text only they cover falls back as for any missing face, and
# bundled_font_diagnostics reports how many the font directory holds. Every other face and notice
# is the fonts component. gui_forms_install_bundled_fonts() is the one install rule the hosts use.
option(GUI_FORMS_INSTALL_FALLBACK_FONTS
       "Install the bundled Noto fallback faces and their licences (install component fallback_fonts)" ON)
set(GUI_FORMS_FALLBACK_FONT_FILES
    NotoSansCJKjp-Regular.otf
    NotoEmoji-Regular.ttf
    NotoSansArabic-Regular.ttf
    NotoSansHebrew-Regular.ttf
    NotoSansDevanagari-Regular.ttf
    NotoSansBengali-Regular.ttf
    NotoSansGurmukhi-Regular.ttf)
set(GUI_FORMS_FALLBACK_FONT_NOTICES
    OFL-Noto.txt
    OFL-NotoSansCJKjp.txt
    OFL-NotoEmoji.txt)
function(gui_forms_install_bundled_fonts)
    set(_gf_font_source ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)
    set(_gf_font_destination ${CMAKE_INSTALL_DATADIR}/GUIForms/fonts)
    set(_gf_excluded)
    set(_gf_fallback)
    foreach(_gf_name IN LISTS GUI_FORMS_FALLBACK_FONT_FILES GUI_FORMS_FALLBACK_FONT_NOTICES)
        list(APPEND _gf_excluded PATTERN ${_gf_name} EXCLUDE)
        list(APPEND _gf_fallback ${_gf_font_source}/${_gf_name})
    endforeach()
    install(DIRECTORY ${_gf_font_source}/ DESTINATION ${_gf_font_destination}
            COMPONENT fonts ${_gf_excluded})
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

add_library(gui_forms_core STATIC
    src/core/application/application.cpp
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
    src/core/types/paint_recipes/paint_recipes.cpp
    src/core/types/rect/rect.cpp
    src/core/theme/theme/theme.cpp
    src/core/theme/contrast/theme_contrast.cpp
    src/core/theme/dark_variant/dark_variant.cpp
    src/core/theme/familiar_themes/familiar_themes.cpp
    src/core/theme/gui_forms_theme/gui_forms_theme.cpp
    src/core/theme/stock_anatomies/stock_anatomies.cpp
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
    src/core/diagnostics/invalidation_overlay/invalidation_overlay.cpp
    src/core/metrics/metrics/metrics.cpp
    src/core/metrics/snapshot/metrics_snapshot.cpp
    src/core/visual_inspection/snapshot/visual_inspection_snapshot.cpp
    src/core/window/inspection/window_visual_inspection.cpp
    src/core/allocation/statistics/allocation_statistics.cpp
    src/core/resources/image_registry/image_registry.cpp
    src/core/resources/image_atlas/image_atlas.cpp
    src/core/resources/application_resources/application_resources.cpp
    src/runtime/user_themes/theme_files.cpp
    src/runtime/user_themes/user_themes.cpp
    src/core/resources/integrity/sha256.cpp
    src/core/resources/fonts/bundled_font_set.cpp
    ${GUI_FORMS_BUNDLED_FONT_MANIFEST_RECORDS}
    src/core/live_surface/buffer/live_surface_buffer.cpp
    src/core/live_surface/frame/live_surface_frame.cpp
    src/core/live_surface/pacer/live_surface_pacer.cpp
    src/core/live_surface/surface/live_surface.cpp
    src/core/live_surface/wake_connection/live_surface_wake_connection.cpp
    src/core/live_surface/write_lease/live_surface_write_lease.cpp
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
    src/core/form_language/form_model/form_model.cpp
    src/core/form_language/form_loader/form_loader.cpp
    src/core/form_language/form_loader/form_window_loader.cpp
    src/core/form_language/form_editor/form_editor.cpp
    src/core/form_language/form_generator/form_generator.cpp
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
    src/core/host/frame_capture/frame_capture.cpp
    src/core/host/compositor/window_backdrop.cpp
    src/core/host/types/host_types.cpp
    src/core/host/types/host_capabilities/host_capabilities.cpp
    src/core/host/types/host_services_snapshot/host_services_snapshot.cpp
    src/core/host/types/host_session_snapshot/host_session_snapshot.cpp
    src/core/host/services/host_services.cpp
    src/core/host/cursor_interaction/cursor_interaction.cpp
    src/core/types/cursor_image/cursor_image.cpp
    src/core/host/session/host_session.cpp
    src/core/host/windowing/desktop_coordinates.cpp
    src/core/host/windowing/host_windowing.cpp
    src/core/feedback/semantic_feedback/semantic_feedback.cpp
    src/core/display/chunk/display_chunk.cpp
    src/core/display/recording_painter/recording_painter.cpp
    src/core/display/replay/replay_display_chunk.cpp
    src/core/display/snapshot/display_snapshot.cpp
    src/core/display/snapshot/splash_baker.cpp
    src/core/dispatcher/operation/dispatch_operation.cpp
    src/core/dispatcher/state/post_dispatch.cpp
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
    src/core/window/accelerator/accelerator_token.cpp
    src/core/control/dispatcher/control_dispatcher.cpp
    src/core/component/component/component.cpp
    src/core/component/component_container/component_container.cpp
    src/core/timer/timer/timer.cpp
    src/core/background_worker/background_worker.cpp
    src/core/control/content_region/content_region.cpp
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
# MD4C 0.6.0 (stx third_party/md4c, MIT; stx third_party/MD4C_INTAKE_2026-10-28.md), the
# markdown framework's CommonMark parser, compiled in the stx package. Its
# objects are part of Core, so no target is added to the exports; its header
# stays private to markdown.cpp.
target_sources(gui_forms_core PRIVATE $<TARGET_OBJECTS:stx_text::md4c>)
set_source_files_properties(src/core/text/markdown/markdown.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${GUI_FORMS_STX_BUILD_PREFIX_DIR}/include/md4c")
set_property(TARGET gui_forms_core PROPERTY INTERFACE_GUI_FORMS_CORE_LINKAGE static)
set_property(TARGET gui_forms_core APPEND PROPERTY COMPATIBLE_INTERFACE_STRING GUI_FORMS_CORE_LINKAGE)
# Compatibility properties alone do not traverse LINK_ONLY dependencies of
# private static adapters. Link options do. Deliberately diagnose the forbidden
# combination at generation by querying a named, nonexistent diagnostic target.
# This contributes no linker option to valid Core or Application consumers.
target_link_options(gui_forms_core INTERFACE
    "$<$<STREQUAL:$<TARGET_PROPERTY:GUI_FORMS_CORE_LINKAGE>,application>:$<TARGET_PROPERTY:GUIForms::ERROR_do_not_link_static_Core_with_Application,TYPE>>")
if(GUI_FORMS_CHECKED_BORROWS)
    target_compile_definitions(gui_forms_core PUBLIC GUI_FORMS_CHECKED_BORROWS=1)
endif()
# dladdr names the code addresses in the fail-fast and borrow reports
# (src/core/failure/fail_fast.cpp; glibc before 2.34 keeps it in libdl).
if(UNIX)
    target_link_libraries(gui_forms_core PRIVATE ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    target_compile_definitions(gui_forms_core PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(gui_forms_core PUBLIC winpthread)
endif()
target_include_directories(gui_forms_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(gui_forms_core PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated/bundled_fonts)
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
    add_library(gui_forms_sealed_pages_objects OBJECT src/host/windows/fonts/sealed_pages_windows.cpp)
    target_compile_definitions(gui_forms_sealed_pages_objects PRIVATE ${GUI_FORMS_WINDOWS_DEFINITIONS})
else()
    add_library(gui_forms_sealed_pages_objects OBJECT src/host/posix/fonts/sealed_pages_posix.cpp)
endif()
target_link_libraries(gui_forms_sealed_pages_objects PRIVATE gui_forms_core)
set_target_properties(gui_forms_sealed_pages_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32 AND GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    # The transactional DIB frame store: the Win32 host and three tests.
    add_library(gui_forms_dib_frame_store_objects OBJECT src/host/windows/raster/dib_frame_store.cpp)
    target_include_directories(gui_forms_dib_frame_store_objects PRIVATE include src/host/windows/raster)
    target_compile_definitions(gui_forms_dib_frame_store_objects PRIVATE ${GUI_FORMS_WINDOWS_DEFINITIONS})
    target_compile_features(gui_forms_dib_frame_store_objects PRIVATE cxx_std_20)
endif()

if(GUI_FORMS_ENABLE_HARFBUZZ_TEXT)
    # FreeType, HarfBuzz, SheenBidi and libunibreak 8.0 (UAX #14 line breaking,
    # admitted to the text engine by the owner on 2026-10-26; stx
    # third_party/LIBUNIBREAK_INTAKE_2026-10-01.md) come compiled from the stx
    # package (stx_text::), with the options cmake/stx_package/ gives them.
    include(cmake/LineBreak.cmake)
    add_library(gui_forms_text_engine STATIC
        src/render/text/harfbuzz/harfbuzz_font_engine.cpp
        src/render/text/harfbuzz/bounded_shape_workspace.cpp
        src/render/text/harfbuzz/reusable_shape_workspace.cpp
        src/render/text/glyph_run/glyph_run_raster.cpp
    )
    target_include_directories(gui_forms_text_engine
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/render/text/harfbuzz
    )
    target_link_libraries(gui_forms_text_engine
        PUBLIC gui_forms_core
        PRIVATE stx_text::freetype stx_text::harfbuzz stx_text::sheenbidi stx_text::linebreak
    )
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
        PRIVATE gui_forms_text_engine stx_text::freetype)
    add_executable(gui_forms_font_metrics_differential_tool
        tools/font_metrics_differential/font_metrics_differential_main.cpp)
    set_target_properties(gui_forms_font_metrics_differential_tool PROPERTIES
        OUTPUT_NAME gui_forms_font_metrics_differential)
    target_link_libraries(gui_forms_font_metrics_differential_tool PRIVATE
        gui_forms_font_metrics_differential)
endif()

if(GUI_FORMS_BUILD_PREPARED_TEXT)
    target_sources(gui_forms_core PRIVATE
        src/core/text/prepared_window/prepared_window_input.cpp
        src/core/text/prepared_window/prepared_window_batch.cpp)
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "GUI_FORMS_BUILD_PREPARED_TEXT requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
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
        PRIVATE gui_forms_text_engine stx_text::freetype Threads::Threads)
    target_compile_features(gui_forms_prepared_text_impl PUBLIC cxx_std_20)
    set_target_properties(gui_forms_prepared_text_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(GUI_FORMS_BUILD_TEXT_MASKS)
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "GUI_FORMS_BUILD_TEXT_MASKS requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
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
        PRIVATE Threads::Threads stx_text::freetype stx_text::harfbuzz stx_text::sheenbidi stx_text::linebreak)
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
)
target_include_directories(gui_drawing_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(gui_drawing_core PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated/font_substitution)
target_compile_features(gui_drawing_core PUBLIC cxx_std_20)
set_target_properties(gui_drawing_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(gui_drawing_c_api SHARED
    src/abi/drawing_c_api.cpp
)
if(WIN32)
    target_sources(gui_drawing_c_api PRIVATE src/abi/drawing_platform_windows.cpp)
    target_link_libraries(gui_drawing_c_api PRIVATE gdi32 user32)
else()
    target_sources(gui_drawing_c_api PRIVATE src/abi/drawing_platform_stub.cpp)
endif()
target_link_libraries(gui_drawing_c_api PRIVATE gui_drawing_core)
target_include_directories(gui_drawing_c_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(gui_drawing_c_api PRIVATE GD_C_API_BUILD)
set_target_properties(gui_drawing_c_api PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    OUTPUT_NAME gui_drawing_abi0
    VERSION 0.1.0
    SOVERSION 0
)
if(WIN32)
    set_target_properties(gui_drawing_c_api PROPERTIES PREFIX "")
endif()
if(APPLE)
    target_link_options(gui_drawing_c_api PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/drawing_c_api_exports_macos.txt"
    )
elseif(UNIX)
    # Without this the library also exports Drawing Core's C++ symbols, which
    # duplicate a static Drawing Core in the same process.
    target_link_options(gui_drawing_c_api PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/drawing_c_api_exports_linux.map"
    )
    set_property(TARGET gui_drawing_c_api APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/drawing_c_api_exports_linux.map)
endif()

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
    src/controls/text_block/text_block.cpp
    src/controls/badge/badge.cpp
    src/controls/indicator_lamp/indicator_lamp.cpp
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
    src/controls/virtual_rows_view/virtual_rows_view.cpp
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
    src/controls/panel/scaled_panel/scaled_panel.cpp
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
    src/controls/instruments/sparkline/sparkline.cpp
    src/controls/plots/plot_axis_item/plot_axis_item.cpp
    src/controls/plots/plot_series_item/plot_series_item.cpp
    src/controls/plots/plot_annotation_item/plot_annotation_item.cpp
    src/controls/plots/plot_view/plot_engine.cpp
    src/controls/plots/plot_view/plot_view.cpp
    src/controls/plots/plot_view/plot_view_live.cpp
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
# stx_btree's headers reach only the controls' private row height index; no
# public header includes them.
target_include_directories(gui_forms_controls SYSTEM PRIVATE "${GUI_FORMS_STX_BTREE_INCLUDE_DIR}")
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
            src/controls/link_flow/link_flow_effects.cpp)

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
    # (form_lab_catalog.cpp), for the pages that name them. Never installed.
    add_executable(gui_forms_form_compiler_lab
        tools/form_compiler/form_compiler.cpp
        tools/form_compiler/form_catalog.cpp
        tools/form_compiler/form_lab_catalog.cpp
    )
    target_link_libraries(gui_forms_form_compiler_lab PRIVATE gui_forms_lab gui_forms_demo_controls)
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
install(EXPORT GUIFormsTargets
        FILE GUIFormsTargets.cmake
        NAMESPACE GUIForms::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
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

    set(GUI_FORMS_DEMOBOARD_FONTS
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids-Bold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Bold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Italic.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-BoldItalic.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-Bold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-Italic.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-BoldItalic.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansCJKjp-Regular.otf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoEmoji-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansArabic-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansHebrew-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansDevanagari-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansBengali-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansGurmukhi-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Selawik-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Selawik-Semibold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Selawik-Bold.ttf)
    set(GUI_FORMS_DEMOBOARD_FONT_NOTICES
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/ATTRIBUTION.md
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE-PortsmouthRapids.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Noto.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Carlito.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Cousine.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-NotoSansCJKjp.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-NotoEmoji.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Selawik.txt)

    if(GUI_FORMS_ENABLE_MACOS_HOST)
        set_source_files_properties(
            ${GUI_FORMS_DEMOBOARD_FONTS}
            ${GUI_FORMS_DEMOBOARD_FONT_NOTICES}
            PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/fonts")
        add_executable(gui_forms_file_manager_demoboard MACOSX_BUNDLE
            file_manager_demoboard/src/macos_main.mm
            ${GUI_FORMS_DEMOBOARD_FONTS}
            ${GUI_FORMS_DEMOBOARD_FONT_NOTICES})
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

    if(GUI_FORMS_BUILD_TESTS)
        add_executable(gui_forms_file_manager_demoboard_tests
            file_manager_demoboard/tests/demoboard_tests.cpp)
        target_link_libraries(gui_forms_file_manager_demoboard_tests PRIVATE
            gui_forms_file_manager_demoboard_model)
        add_test(NAME gui_forms_file_manager_demoboard_tests
                 COMMAND gui_forms_file_manager_demoboard_tests)
        add_test(NAME gui_forms_file_manager_demoboard_public_policy
            COMMAND ${CMAKE_COMMAND}
                -DDEMOBOARD_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/file_manager_demoboard
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_demoboard_boundaries.cmake)
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
    # ADR-004: the Skia painter draws text only through the shared
    # HarfBuzz/FreeType engine; Skia itself has no font backend.
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "The Skia renderer requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT (ADR-004)")
    endif()
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

    add_library(gui_drawing_skia STATIC
        src/render/skia/executor/skia_executor.cpp
    )
    if(APPLE)
        # Finds installed families nobody registered (ADR-004 phase 7).
        target_sources(gui_drawing_skia PRIVATE
            src/render/skia/executor/core_text_font_bytes.cpp)
    endif()
    target_include_directories(gui_drawing_skia
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/render/skia/executor
    )
    # ADR-004: the C drawing API draws text with the shared HarfBuzz/FreeType
    # engine and compositor.
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "GUI.Drawing's Skia executor requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
    target_link_libraries(gui_drawing_skia
        PUBLIC gui_drawing_core
        PRIVATE
            gui_forms_text_engine
            gui_forms_core
            stx_text::freetype
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
    add_executable(gui_forms_lamp_keys tools/industrial_surfaces/lamp_keys.cpp)
    target_link_libraries(gui_forms_lamp_keys PRIVATE gui_drawing_skia gui_forms_core)
    target_compile_definitions(gui_forms_lamp_keys PRIVATE
        GUI_FORMS_LEGEND_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthMono.ttf")

    add_library(gui_drawing_raster_c_api SHARED
        src/abi/drawing_skia_c_api.cpp
    )
    target_include_directories(gui_drawing_raster_c_api PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/skia/executor
    )
    # One copy of the renderer. Where the application DLL is built (MinGW,
    # below), it carries the drawing executor (gui_drawing_skia), the text
    # engine and the Skia derivative, and this module calls into it instead
    # of linking a second copy of all three. The two modules pass heap
    # blocks only through the one UCRT heap (see add_link_options(-static)).
    # The application's import library comes first on the link line, so no
    # static archive member is drawn in for a symbol it exports. Elsewhere
    # the module links the executor statically.
    if(MINGW AND GUI_FORMS_ENABLE_WINDOWS_HOST)
        target_link_libraries(gui_drawing_raster_c_api PRIVATE
            gui_forms_application
            gui_drawing_c_api
        )
        target_include_directories(gui_drawing_raster_c_api PRIVATE
            $<TARGET_PROPERTY:gui_drawing_core,INTERFACE_INCLUDE_DIRECTORIES>
        )
        target_compile_features(gui_drawing_raster_c_api PRIVATE cxx_std_20)
    else()
        target_link_libraries(gui_drawing_raster_c_api PRIVATE
            gui_drawing_skia
            gui_drawing_c_api
        )
    endif()
    set_target_properties(gui_drawing_raster_c_api PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME gui_drawing_raster0
    )
    if(WIN32)
        set_target_properties(gui_drawing_raster_c_api PROPERTIES PREFIX "")
        target_link_libraries(gui_drawing_raster_c_api PRIVATE advapi32)
    endif()
    # Every face the substitution table names, with its licence, the table
    # itself and the attribution, beside the raster module (stage F).
    set(gui_drawing_raster_fonts
        ${GUI_FORMS_SUBSTITUTION_FONT_FILES}
        ${GUI_FORMS_SUBSTITUTION_FONT_LICENCES}
        font-substitution.json
        ATTRIBUTION.md)
    list(TRANSFORM gui_drawing_raster_fonts PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/)
    add_custom_command(TARGET gui_drawing_raster_c_api POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:gui_drawing_raster_c_api>/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${gui_drawing_raster_fonts}
            "$<TARGET_FILE_DIR:gui_drawing_raster_c_api>/fonts"
        VERBATIM
    )
endif()

if(GUI_FORMS_ENABLE_MACOS_HOST)
    enable_language(OBJCXX)
    find_library(GUI_FORMS_APPKIT AppKit REQUIRED)
    find_library(GUI_FORMS_QUARTZCORE QuartzCore REQUIRED)
    find_library(GUI_FORMS_COREGRAPHICS CoreGraphics REQUIRED)
    find_library(GUI_FORMS_UNIFORMTYPEIDENTIFIERS UniformTypeIdentifiers REQUIRED)
    add_library(gui_forms_host_macos STATIC
        src/host/macos/application/macos_host.mm
        src/host/macos/application/macos_application_run.mm
        src/host/macos/application/macos_window_delegates.mm
        src/host/macos/application/macos_window_requests.mm
        src/host/macos/application/macos_main_work.mm
        src/host/macos/application/macos_values.mm
        src/host/macos/application/macos_bundled_fonts.mm
        src/host/macos/application/macos_splashscreen.mm
        src/host/macos/application/live_surface_damage.cpp
        src/host/macos/view/macos_view.mm
        src/host/macos/view/macos_view_scheduling.mm
        src/host/macos/view/macos_view_drawing.mm
        src/host/macos/view/macos_view_input.mm
        src/host/macos/view/macos_view_accessibility.mm
        src/host/macos/view/macos_view_diagnostics.mm
        src/host/macos/accessibility/macos_accessibility_element.mm
        src/host/macos/accessibility/macos_accessibility_text.mm
        src/host/macos/application/application_menu.mm
        src/host/macos/services/appkit_host_services.mm
        src/host/macos/services/appkit_clipboard_image.mm
        src/host/macos/services/appkit_status_items.mm
        src/host/macos/compositor/macos_compositor.mm
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
    )
    target_include_directories(gui_forms_host_macos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/macos
    )
    target_compile_options(gui_forms_host_macos PRIVATE -fobjc-arc)
    target_link_libraries(gui_forms_host_macos
        PUBLIC gui_forms_core gui_forms_skia
        PRIVATE ${GUI_FORMS_APPKIT} ${GUI_FORMS_QUARTZCORE}
                ${GUI_FORMS_COREGRAPHICS}
                ${GUI_FORMS_UNIFORMTYPEIDENTIFIERS}
    )

    # First-party native applications consume one installed dynamic boundary.
    # The renderer, text stack, controls, and AppKit adapter remain private
    # implementation details; force-loading the internal archives keeps the
    # public C++ surface available without exporting source-tree-only targets.
    add_library(gui_forms_application SHARED
        src/runtime/application_bundle.cpp
        src/runtime/application/application.cpp
        src/runtime/offscreen/offscreen_raster_create.cpp
    )
    add_library(GUIForms::Application ALIAS gui_forms_application)
    set_target_properties(gui_forms_application PROPERTIES
        EXPORT_NAME Application
        OUTPUT_NAME gui_forms_application
        VERSION 0.1.0
        SOVERSION 0
    )
    target_include_directories(gui_forms_application PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_features(gui_forms_application PUBLIC cxx_std_20)
    target_compile_definitions(gui_forms_application PRIVATE
        GUI_FORMS_APPLICATION_MACOS=1 GUI_FORMS_APPLICATION_SKIA=1)
    target_link_libraries(gui_forms_application PRIVATE
        gui_forms_core
        gui_drawing_core
        gui_forms_controls
        gui_forms_skia
        gui_forms_host_macos
        ${GUI_FORMS_APPKIT}
        ${GUI_FORMS_QUARTZCORE}
        ${GUI_FORMS_COREGRAPHICS}
        ${GUI_FORMS_UNIFORMTYPEIDENTIFIERS}
    )
    target_link_options(gui_forms_application PRIVATE
        "LINKER:-force_load,$<TARGET_FILE:gui_forms_core>"
        "LINKER:-force_load,$<TARGET_FILE:gui_drawing_core>"
        "LINKER:-force_load,$<TARGET_FILE:gui_forms_controls>"
        "LINKER:-force_load,$<TARGET_FILE:gui_forms_skia>"
        "LINKER:-force_load,$<TARGET_FILE:gui_forms_host_macos>"
    )
    if(TARGET gui_forms_text_engine)
        target_link_libraries(gui_forms_application PRIVATE
            gui_forms_text_engine stx_text::freetype stx_text::harfbuzz)
        target_link_options(gui_forms_application PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:gui_forms_text_engine>")
    endif()

    install(TARGETS gui_forms_application
            EXPORT GUIFormsApplicationTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT GUIFormsApplicationTargets
            FILE GUIFormsApplicationTargets.cmake
            NAMESPACE GUIForms::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
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
        src/host/linux/application/linux_host.cpp
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
        src/host/linux/application/x11_windowing.cpp
        src/host/linux/application/x11_shm_present.cpp
        src/host/linux/settings/xsettings_parse.cpp
        src/host/linux/application/linux_input_rules.cpp
        src/host/linux/settings/xsettings_watch.cpp
        src/host/linux/accessibility/linux_accessibility.cpp
        src/host/linux/services/linux_services.cpp
        src/host/linux/services/x11_tray_icons.cpp
        src/host/linux/services/linux_drag_drop.cpp
        src/host/linux/services/linux_dialogs.cpp
        src/host/linux/compositor/x11_compositor.cpp)
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
    add_library(gui_forms_application SHARED src/runtime/application_bundle.cpp src/runtime/application/application.cpp
        src/runtime/offscreen/offscreen_raster_create.cpp)
    add_library(GUIForms::Application ALIAS gui_forms_application)
    set_target_properties(gui_forms_application PROPERTIES EXPORT_NAME Application OUTPUT_NAME gui_forms_application VERSION 0.1.0 SOVERSION 0)
    target_include_directories(gui_forms_application PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(gui_forms_application PRIVATE
        GUI_FORMS_APPLICATION_LINUX=1 GUI_FORMS_APPLICATION_SKIA=1)
    target_link_libraries(gui_forms_application PRIVATE
        "-Wl,--whole-archive" gui_forms_core gui_drawing_core gui_forms_controls gui_forms_host_linux gui_forms_skia
        "-Wl,--no-whole-archive" X11::X11 X11::Xfixes X11::Xrender)
    if(GUI_FORMS_XCURSOR_AVAILABLE)
        target_link_libraries(gui_forms_application PRIVATE X11::Xcursor)
    endif()
    if(TARGET X11::Xrandr)
        target_link_libraries(gui_forms_application PRIVATE X11::Xrandr)
    endif()
    if(TARGET X11::Xext)
        target_link_libraries(gui_forms_application PRIVATE X11::Xext)
    endif()
    if(TARGET gui_forms_text_engine)
        target_link_libraries(gui_forms_application PRIVATE gui_forms_text_engine stx_text::freetype stx_text::harfbuzz)
    endif()
    install(TARGETS gui_forms_application EXPORT GUIFormsApplicationTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(EXPORT GUIFormsApplicationTargets FILE GUIFormsApplicationTargets.cmake
        NAMESPACE GUIForms:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
    gui_forms_install_bundled_fonts()
endif()

if(GUI_FORMS_ENABLE_WINDOWS_HOST)
    # Everything of the host but windows_host.cpp, which alone reads the
    # host's build switches (the DIB store, prepared text, the tests' hooks):
    # compiled once for the host and the DIB lifecycle test.
    add_library(gui_forms_windows_host_objects OBJECT
        src/host/windows/accessibility/windows_accessibility.cpp
        src/host/windows/accessibility/windows_semantic_source.cpp
        src/host/windows/accessibility/windows_automation.cpp
        src/host/windows/accessibility/windows_text_automation.cpp
        src/host/windows/services/windows_clipboard_image.cpp
        src/host/windows/services/windows_notify_icons.cpp
        src/host/windows/paint_endpoint/windows_compatibility_paint_endpoint.cpp
        src/host/windows/compositor/windows_compositor.cpp
    )
    target_include_directories(gui_forms_windows_host_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/host/windows)
    target_compile_definitions(gui_forms_windows_host_objects PRIVATE ${GUI_FORMS_WINDOWS_DEFINITIONS})
    target_link_libraries(gui_forms_windows_host_objects PRIVATE gui_forms_core gui_forms_skia gui_forms_text_engine)
    set_target_properties(gui_forms_windows_host_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(gui_forms_host_windows STATIC
        src/host/windows/application/windows_host.cpp
        $<TARGET_OBJECTS:gui_forms_windows_host_objects>
        $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
    )
    target_include_directories(gui_forms_host_windows PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/windows
    )
    target_compile_definitions(gui_forms_host_windows PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS}
    )
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
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "The Win32 host requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT (ADR-004)")
    endif()
    # Stage SK3: the Win32 host draws with the Skia CPU raster.
    if(NOT TARGET gui_forms_skia)
        message(FATAL_ERROR "The Win32 host requires GUI_FORMS_ENABLE_SKIA (stage SK3)")
    endif()
    target_link_libraries(gui_forms_host_windows
        PUBLIC gui_forms_core gui_forms_skia
        PRIVATE gui_forms_text_engine user32 gdi32 imm32 ole32 oleaut32 oleacc uiautomationcore uuid comdlg32 shell32
    )
    if(MINGW)
        add_library(gui_forms_application SHARED
            src/runtime/application_bundle.cpp
            src/runtime/application/application.cpp
            src/runtime/offscreen/offscreen_raster_create.cpp)
        add_library(GUIForms::Application ALIAS gui_forms_application)
        set_target_properties(gui_forms_application PROPERTIES
            EXPORT_NAME Application OUTPUT_NAME gui_forms_application
            VERSION 0.1.0 SOVERSION 0)
        target_include_directories(gui_forms_application PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
        target_compile_features(gui_forms_application PUBLIC cxx_std_20)
        target_compile_definitions(gui_forms_application PRIVATE
            GUI_FORMS_APPLICATION_WINDOWS=1 GUI_FORMS_APPLICATION_SKIA=1)
        # gui_drawing_skia: the drawing executor gui_drawing_raster0 calls
        # into, so the renderer exists once (gui_drawing_raster_c_api above).
        target_link_libraries(gui_forms_application PRIVATE
            "-Wl,--whole-archive" gui_forms_core gui_drawing_core
            gui_forms_controls gui_forms_host_windows gui_forms_skia
            $<TARGET_NAME_IF_EXISTS:gui_drawing_skia> "-Wl,--no-whole-archive"
            gui_forms_text_engine stx_text::freetype stx_text::harfbuzz stx_text::sheenbidi
            user32 gdi32 ole32 oleaut32 oleacc uiautomationcore uuid comdlg32 shell32)
        install(TARGETS gui_forms_application EXPORT GUIFormsApplicationTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(EXPORT GUIFormsApplicationTargets FILE GUIFormsApplicationTargets.cmake
            NAMESPACE GUIForms:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
        gui_forms_install_bundled_fonts()
    endif()
    add_library(gui_forms_win32_compat SHARED
        src/host/windows/win32_compat_shim.cpp
        src/host/windows/win32_compat_shim.def
    )
    target_compile_features(gui_forms_win32_compat PRIVATE cxx_std_20)
    # Declarations only: the shim checks its run-time entry-point types
    # against gf_abi.h and resolves the ABI by name; it does not link it.
    target_include_directories(gui_forms_win32_compat PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(gui_forms_win32_compat PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS}
    )
    target_link_libraries(gui_forms_win32_compat PRIVATE user32 gdi32)
    set_target_properties(gui_forms_win32_compat PROPERTIES
        PREFIX ""
        OUTPUT_NAME "gui_forms_win32_compat")
    add_executable(gui_forms_windows_probe
        tools/windows_automation_probe.cpp
    )
    target_compile_definitions(gui_forms_windows_probe PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS}
    )
    add_executable(gui_drawing_windows_probe
        tests/drawing_windows_probe.c
    )
    target_compile_definitions(gui_drawing_windows_probe PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS}
    )
    target_link_libraries(gui_drawing_windows_probe PRIVATE
        gui_drawing_c_api gdi32 user32
    )
    if(GUI_FORMS_BUILD_TESTS)
        add_test(NAME gui_drawing_windows_probe COMMAND gui_drawing_windows_probe)
        add_executable(gui_forms_windows_live_surface_endpoint_tests
            tests/windows_live_surface_endpoint_tests.cpp
        )
        target_compile_definitions(
            gui_forms_windows_live_surface_endpoint_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS}
        )
        target_link_libraries(gui_forms_windows_live_surface_endpoint_tests PRIVATE
            gui_forms_host_windows
        )
        add_test(NAME gui_forms_windows_live_surface_endpoint_tests
                 COMMAND gui_forms_windows_live_surface_endpoint_tests)
        add_executable(gui_forms_windows_gdi_compat_shim_tests
            tests/windows_gdi_compat_shim_tests.cpp
        )
        target_compile_definitions(
            gui_forms_windows_gdi_compat_shim_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS}
        )
        target_link_libraries(gui_forms_windows_gdi_compat_shim_tests PRIVATE
            gui_forms_abi gdi32 user32
        )
        add_dependencies(gui_forms_windows_gdi_compat_shim_tests
                         gui_forms_win32_compat)
        add_test(NAME gui_forms_windows_gdi_compat_shim_tests
                 COMMAND gui_forms_windows_gdi_compat_shim_tests)
    endif()
endif()

# Export annotations (CP29 handoff section 2.2): every archive the
# Application library or the shared test runtime holds whole, and the
# Application library itself. gui_forms_application_archive and
# gui_forms_application_library are defined beside the Threading library.
foreach(_gf_archive IN ITEMS
        gui_forms_core gui_drawing_core gui_forms_controls gui_forms_plot_primitives
        gui_forms_host_headless gui_forms_text_engine gui_forms_prepared_text_impl
        gui_forms_text_masks_impl gui_forms_sealed_pages_objects gui_forms_dib_frame_store_objects
        gui_forms_skia gui_drawing_skia gui_forms_host_macos gui_forms_host_linux
        gui_forms_windows_host_objects gui_forms_host_windows)
    if(TARGET ${_gf_archive})
        gui_forms_application_archive(${_gf_archive})
    endif()
endforeach()
if(TARGET gui_forms_application)
    gui_forms_application_library(gui_forms_application)
endif()

if(GUI_FORMS_BUILD_GALLERY OR (GUI_FORMS_BUILD_TESTS AND GUI_FORMS_ENABLE_MACOS_HOST))
    set(GUI_FORMS_GALLERY_FONTS
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids-Bold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Bold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Italic.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-BoldItalic.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-Bold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-Italic.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-BoldItalic.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansCJKjp-Regular.otf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoEmoji-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansArabic-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansHebrew-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansDevanagari-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansBengali-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansGurmukhi-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Selawik-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Selawik-Semibold.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Selawik-Bold.ttf
    )
    set(GUI_FORMS_GALLERY_FONT_NOTICES
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/ATTRIBUTION.md
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE-PortsmouthRapids.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Noto.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Carlito.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Cousine.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-NotoSansCJKjp.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-NotoEmoji.txt
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL-Selawik.txt
    )

endif()

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
        CLASS TypographyDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/animation-laboratory.form
        CLASS AnimationDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/collections-and-overlays.form
        CLASS CollectionsDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/images-and-drawing.form
        CLASS ImagesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/ranges-and-progress.form
        CLASS RangesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/checked-collections.form
        CLASS CheckedCollectionsDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/choices-and-sections.form
        CLASS ChoicesDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/tree-and-object-views.form
        CLASS TreeAndObjectViewsDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/text-and-input.form
        CLASS TextAndInputDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/states-and-diagnostics.form
        CLASS StatesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/timing-and-tooltips.form
        CLASS TimingDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/dates-and-calendar.form
        CLASS DatesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/values-and-spinners.form
        CLASS ValuesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/value-editors.form
        CLASS ValueEditorsDocument NAMESPACE gui_forms::gallery::pages COMPILER gui_forms_form_compiler_lab)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/dialogs-and-host-services.form
        CLASS HostServicesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/semantic-feedback.form
        CLASS SemanticFeedbackDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/notices-and-badges.form
        CLASS NoticesAndBadgesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/input-and-rules.form
        CLASS InputAndRulesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/theme-settings.form
        CLASS ThemeSettingsDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/spinners-and-rings.form
        CLASS SpinnersAndRingsDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/instruments.form
        CLASS InstrumentsDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/guided-tasks.form
        CLASS GuidedTasksDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/industrial-surfaces.form
        CLASS IndustrialSurfacesDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/help-book.form
        CLASS HelpBookPageDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/control-spectrum.form
        CLASS ControlSpectrumDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/retained-layout-panels.form
        CLASS RetainedLayoutDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/responsive-layouts.form
        CLASS ResponsiveLayoutsDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/css-layout-panels.form
        CLASS CssLayoutDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/dock-and-anchor.form
        CLASS DockAnchorDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/docking-workspace.form
        CLASS DockingWorkspaceDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/containers-and-focus.form
        CLASS ContainersDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/tabs-and-pages.form
        CLASS TabsDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/anchored-overlays.form
        CLASS AnchoredOverlaysDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/commands-and-menus.form
        CLASS CommandsDocument NAMESPACE gui_forms::gallery::pages)
    gui_forms_add_form(gui_forms_gallery_model demo/pages/live-surfaces.form
        CLASS LiveSurfacesDocument NAMESPACE gui_forms::gallery::pages)
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
            demo/pages/live-surfaces.form demo/pages/industrial-surfaces.form)
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
            PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/fonts"
        )
        add_executable(gui_forms_gallery MACOSX_BUNDLE
            src/host/macos/gallery_main.mm
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
            ${GUI_FORMS_GALLERY_FONTS}
            ${GUI_FORMS_GALLERY_FONT_NOTICES}
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
        # The Gallery and the labs bundle GUI_FORMS_GALLERY_FONTS beside them;
        # the staging at the end of this file copies them.
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

        # The X11 host discovers bundled faces in <executable dir>/fonts (or
        # GUI_FORMS_FONT_DIR, or ../share/GUIForms/fonts once installed);
        # they are staged at the end of this file.
    endif()

    include(demo/correspondence/correspondence.cmake)
endif()

if(GUI_FORMS_BUILD_TEXT_LAYOUT_PROBE)
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "The text layout probe requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
    add_executable(gui_forms_text_layout_probe benchmarks/text_layout_probe.cpp)
    target_link_libraries(gui_forms_text_layout_probe PRIVATE gui_forms_text_engine)
endif()

if(GUI_FORMS_BUILD_LABEL_LAYOUT_PROBE)
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "The Label layout probe requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
    add_executable(gui_forms_label_layout_probe benchmarks/label_layout_probe.cpp)
    target_link_libraries(gui_forms_label_layout_probe PRIVATE gui_forms_controls gui_forms_text_engine)
endif()

if(GUI_FORMS_BUILD_TEXT_WORKER_PROBE)
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "The text worker probe requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
    find_package(Threads REQUIRED)
    add_executable(gui_forms_text_worker_probe
        benchmarks/text_worker_probe.cpp
        src/render/text/worker_probe/worker_probe.cpp)
    target_include_directories(gui_forms_text_worker_probe PRIVATE src/render/text/worker_probe)
    target_link_libraries(gui_forms_text_worker_probe PRIVATE gui_forms_text_engine Threads::Threads)
endif()

if(GUI_FORMS_BUILD_TEXT_PAINT_PROBE)
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "The text paint probe requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
    find_package(Threads REQUIRED)
    add_executable(gui_forms_text_paint_probe
        benchmarks/text_paint_probe.cpp
        src/render/text/paint_probe/paint_probe.cpp
        src/render/text/worker_probe/worker_probe.cpp)
    target_include_directories(gui_forms_text_paint_probe PRIVATE
        src/render/text/paint_probe src/render/text/worker_probe)
    target_link_libraries(gui_forms_text_paint_probe PRIVATE gui_forms_text_engine stx_text::freetype Threads::Threads)
endif()

if(GUI_FORMS_BUILD_TEXT_RASTER_PROFILE_PROBE)
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "The text raster profile probe requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
    add_executable(gui_forms_text_raster_profile_probe benchmarks/text_raster_profile_probe.cpp)
    target_link_libraries(gui_forms_text_raster_profile_probe PRIVATE gui_forms_text_engine stx_text::freetype stx_text::harfbuzz)
    if(TARGET gui_forms_skia)
        target_link_libraries(gui_forms_text_raster_profile_probe PRIVATE gui_forms_controls gui_forms_skia)
        target_compile_definitions(gui_forms_text_raster_profile_probe PRIVATE GUI_FORMS_PROFILE_PROBE_SKIA_REFERENCE)
    endif()
endif()

if(GUI_FORMS_BUILD_TEXT_ENGINE_BENCHMARK)
    if(NOT TARGET gui_forms_text_engine)
        message(FATAL_ERROR "The text engine benchmark requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
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

if(GUI_FORMS_BUILD_TESTS)
    enable_testing()
    if(TARGET gui_forms_lamp_keys)
        add_test(NAME gui_forms_lamp_keys_legend_tests COMMAND gui_forms_lamp_keys --legend-test)
    endif()
    if(TARGET gui_forms_text_masks_impl)
        add_executable(gui_forms_text_mask_cost_probe EXCLUDE_FROM_ALL tests/text_mask_cost_probe.cpp)
        target_link_libraries(gui_forms_text_mask_cost_probe PRIVATE gui_forms_text_masks_impl)
        add_executable(gui_forms_text_mask_native_tests tests/text_mask_native_tests.cpp)
        target_link_libraries(gui_forms_text_mask_native_tests PRIVATE
            gui_forms_text_masks_impl stx_text::freetype stx_text::harfbuzz stx_text::sheenbidi)
        add_test(NAME gui_forms_text_mask_native_tests
            COMMAND gui_forms_text_mask_native_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fonts/amiri/Amiri-Regular.ttf")
        set_tests_properties(gui_forms_text_mask_native_tests PROPERTIES TIMEOUT 30)
        add_executable(gui_forms_text_mask_wrapping_tests tests/text_mask_wrapping_tests.cpp)
        target_link_libraries(gui_forms_text_mask_wrapping_tests PRIVATE gui_forms_text_masks_impl)
        add_test(NAME gui_forms_text_mask_wrapping_tests
            COMMAND gui_forms_text_mask_wrapping_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fonts/amiri/Amiri-Regular.ttf")
        set_tests_properties(gui_forms_text_mask_wrapping_tests PROPERTIES TIMEOUT 30)
        add_executable(gui_forms_text_mask_lifecycle_tests tests/text_mask_lifecycle_tests.cpp)
        target_link_libraries(gui_forms_text_mask_lifecycle_tests PRIVATE gui_forms_text_masks_impl)
        add_test(NAME gui_forms_text_mask_lifecycle_tests
            COMMAND gui_forms_text_mask_lifecycle_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        set_tests_properties(gui_forms_text_mask_lifecycle_tests PROPERTIES TIMEOUT 30)
    endif()
    if(TARGET gui_forms_prepared_text_impl)
        add_executable(gui_forms_prepared_window_input_tests tests/prepared_window_input_tests.cpp)
        target_link_libraries(gui_forms_prepared_window_input_tests PRIVATE gui_forms_core)
        add_test(NAME gui_forms_prepared_window_input_tests COMMAND gui_forms_prepared_window_input_tests)
        add_executable(gui_forms_prepared_window_batch_tests tests/prepared_window_batch_tests.cpp)
        target_link_libraries(gui_forms_prepared_window_batch_tests PRIVATE gui_forms_core)
        add_test(NAME gui_forms_prepared_window_batch_tests COMMAND gui_forms_prepared_window_batch_tests)
        add_executable(gui_forms_prepared_window_shape_tests tests/prepared_window_shape_tests.cpp)
        target_link_libraries(gui_forms_prepared_window_shape_tests PRIVATE gui_forms_prepared_text_impl gui_forms_text_engine stx_text::freetype)
        add_test(NAME gui_forms_prepared_window_shape_tests
            COMMAND gui_forms_prepared_window_shape_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        add_executable(gui_forms_prepared_window_session_tests tests/prepared_window_session_tests.cpp)
        target_link_libraries(gui_forms_prepared_window_session_tests PRIVATE gui_forms_prepared_text_impl gui_forms_text_engine)
        add_test(NAME gui_forms_prepared_window_session_tests
            COMMAND gui_forms_prepared_window_session_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        set_tests_properties(gui_forms_prepared_window_session_tests PROPERTIES TIMEOUT 60)
        add_executable(gui_forms_prepared_text_service_tests tests/prepared_text_service_tests.cpp)
        target_link_libraries(gui_forms_prepared_text_service_tests PRIVATE gui_forms_prepared_text_impl)
        add_test(NAME gui_forms_prepared_text_service_tests
            COMMAND gui_forms_prepared_text_service_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        add_executable(gui_forms_prepared_text_raster_tests tests/prepared_text_raster_tests.cpp)
        target_link_libraries(gui_forms_prepared_text_raster_tests PRIVATE gui_forms_prepared_text_impl)
        add_test(NAME gui_forms_prepared_text_raster_tests
            COMMAND gui_forms_prepared_text_raster_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        if(TARGET gui_forms_skia)
            add_executable(gui_forms_prepared_text_skia_tests tests/prepared_text_skia_tests.cpp)
            target_link_libraries(gui_forms_prepared_text_skia_tests PRIVATE
                gui_forms_skia gui_forms_prepared_text_impl stx_skia::stx_skia)
            add_test(NAME gui_forms_prepared_text_skia_tests
                COMMAND gui_forms_prepared_text_skia_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
            set_tests_properties(gui_forms_prepared_text_skia_tests PROPERTIES TIMEOUT 30)
        endif()
        add_executable(gui_forms_prepared_display_tests tests/prepared_display_tests.cpp)
        target_link_libraries(gui_forms_prepared_display_tests PRIVATE gui_forms_prepared_text_impl)
        add_test(NAME gui_forms_prepared_display_tests
            COMMAND gui_forms_prepared_display_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
    endif()
    add_executable(gui_forms_cursor_interaction_tests tests/cursor_interaction_tests.cpp)
    target_link_libraries(gui_forms_cursor_interaction_tests PRIVATE gui_forms_core)
    add_test(NAME cursor_interaction COMMAND gui_forms_cursor_interaction_tests)
    add_executable(gui_forms_audio_loop_scheduler_tests
        tests/audio/audio_loop_scheduler_tests.cpp
        src/audio/loop_transport/scheduler.cpp)
    target_include_directories(gui_forms_audio_loop_scheduler_tests PRIVATE src/audio/loop_transport)
    target_compile_features(gui_forms_audio_loop_scheduler_tests PRIVATE cxx_std_20)
    add_test(NAME gui_forms_audio_loop_scheduler_tests COMMAND gui_forms_audio_loop_scheduler_tests)
    if(WIN32)
        add_executable(gui_forms_windows_message_queue_tests tests/windows_message_queue_tests.cpp)
        target_include_directories(gui_forms_windows_message_queue_tests PRIVATE src/host/windows/input)
        target_compile_features(gui_forms_windows_message_queue_tests PRIVATE cxx_std_20)
        target_link_libraries(gui_forms_windows_message_queue_tests PRIVATE user32)
        add_test(NAME gui_forms_windows_message_queue_tests COMMAND gui_forms_windows_message_queue_tests)
        # Explicit local probe only: needs ownership of an interactive desktop
        # window before injecting a bounded native key sequence. Not a CTest.
        add_executable(gui_forms_windows_message_queue_native_probe tests/windows_message_queue_native_probe.cpp)
        target_include_directories(gui_forms_windows_message_queue_native_probe PRIVATE src/host/windows/input)
        target_compile_features(gui_forms_windows_message_queue_native_probe PRIVATE cxx_std_20)
        target_link_libraries(gui_forms_windows_message_queue_native_probe PRIVATE user32)
        add_executable(gui_forms_windows_notify_icon_tests
            tests/windows_notify_icon_tests.cpp
            src/host/windows/services/windows_notify_icons.cpp)
        target_include_directories(gui_forms_windows_notify_icon_tests PRIVATE src/host/windows)
        target_compile_definitions(gui_forms_windows_notify_icon_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_notify_icon_tests PRIVATE gui_forms_core user32 gdi32 shell32)
        add_test(NAME gui_forms_windows_notify_icon_tests COMMAND gui_forms_windows_notify_icon_tests)
                add_executable(gui_forms_windows_cursor_tests tests/windows_cursor_tests.cpp)
        target_link_libraries(gui_forms_windows_cursor_tests PRIVATE gui_forms_core user32 gdi32)
        add_test(NAME gui_forms_windows_cursors COMMAND gui_forms_windows_cursor_tests)
        add_executable(gui_forms_windows_key_translation_tests tests/windows_key_translation_tests.cpp)
        target_include_directories(gui_forms_windows_key_translation_tests PRIVATE src/host/windows/input)
        target_link_libraries(gui_forms_windows_key_translation_tests PRIVATE gui_forms_core)
        add_test(NAME gui_forms_windows_key_translation_tests COMMAND gui_forms_windows_key_translation_tests)
        add_executable(gui_forms_windows_dib_band_present_tests tests/windows_dib_band_present_tests.cpp)
        target_include_directories(gui_forms_windows_dib_band_present_tests PRIVATE src/host/windows/raster)
        target_compile_definitions(gui_forms_windows_dib_band_present_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_dib_band_present_tests PRIVATE gdi32 user32)
        add_test(NAME gui_forms_windows_dib_band_present COMMAND gui_forms_windows_dib_band_present_tests)
        if(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
            add_executable(gui_forms_windows_dib_frame_store_tests
                tests/windows_dib_frame_store_tests.cpp
                $<TARGET_OBJECTS:gui_forms_dib_frame_store_objects>)
            target_include_directories(gui_forms_windows_dib_frame_store_tests PRIVATE
                include src/host/windows/raster)
            target_compile_definitions(gui_forms_windows_dib_frame_store_tests PRIVATE
                ${GUI_FORMS_WINDOWS_DEFINITIONS}
                GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB=1)
            target_link_libraries(gui_forms_windows_dib_frame_store_tests PRIVATE gdi32 user32)
            add_test(NAME gui_forms_windows_dib_frame_store_tests
                     COMMAND gui_forms_windows_dib_frame_store_tests)
            add_executable(gui_forms_windows_dib_lifecycle_tests
                tests/windows_dib_lifecycle_tests.cpp
                src/host/windows/application/windows_host.cpp
                $<TARGET_OBJECTS:gui_forms_windows_host_objects>
                $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>
                $<TARGET_OBJECTS:gui_forms_dib_frame_store_objects>)
            target_include_directories(gui_forms_windows_dib_lifecycle_tests PRIVATE
                include src/host/windows)
            target_compile_definitions(gui_forms_windows_dib_lifecycle_tests PRIVATE
                ${GUI_FORMS_WINDOWS_DEFINITIONS}
                GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB=1 GUI_FORMS_DIB_LIFECYCLE_TEST=1)
            target_link_libraries(gui_forms_windows_dib_lifecycle_tests PRIVATE
                gui_forms_core gui_forms_skia gui_forms_text_engine user32 gdi32 imm32 ole32 oleaut32 oleacc uiautomationcore uuid
                comdlg32 shell32)
            add_test(NAME gui_forms_windows_dib_lifecycle_tests
                     COMMAND gui_forms_windows_dib_lifecycle_tests)
            set_tests_properties(gui_forms_windows_dib_lifecycle_tests PROPERTIES TIMEOUT 30)
            if(TARGET gui_forms_prepared_text_impl)
                add_executable(gui_forms_windows_prepared_text_tests
                    tests/windows_prepared_text_tests.cpp
                    src/host/windows/application/windows_host.cpp
                    src/host/windows/fonts/sealed_pages_windows.cpp
                    src/host/windows/accessibility/windows_accessibility.cpp
                    src/host/windows/accessibility/windows_semantic_source.cpp
                    src/host/windows/accessibility/windows_automation.cpp
                    src/host/windows/accessibility/windows_text_automation.cpp
                    src/host/windows/services/windows_clipboard_image.cpp
                    src/host/windows/services/windows_notify_icons.cpp
                    src/host/windows/paint_endpoint/windows_compatibility_paint_endpoint.cpp
                    src/host/windows/compositor/windows_compositor.cpp
                    $<TARGET_OBJECTS:gui_forms_dib_frame_store_objects>)
                target_include_directories(gui_forms_windows_prepared_text_tests PRIVATE
                    include src/host/windows)
                target_compile_definitions(gui_forms_windows_prepared_text_tests PRIVATE
                    ${GUI_FORMS_WINDOWS_DEFINITIONS}
                    GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB=1
                    GUI_FORMS_WINDOWS_PREPARED_TEXT=1 GUI_FORMS_WINDOWS_PREPARED_TEXT_TEST=1)
                target_link_libraries(gui_forms_windows_prepared_text_tests PRIVATE
                    gui_forms_prepared_text_impl gui_forms_skia gui_forms_text_engine user32 gdi32 imm32 ole32 oleaut32 oleacc uiautomationcore uuid
                    comdlg32 shell32)
                add_test(NAME gui_forms_windows_prepared_text_tests
                    COMMAND gui_forms_windows_prepared_text_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
                set_tests_properties(gui_forms_windows_prepared_text_tests PROPERTIES TIMEOUT 30)
            endif()
        endif()
    endif()
    if(GUI_FORMS_ENABLE_LINUX_HOST)
        find_package(X11 QUIET)
        if(TARGET X11::Xcursor AND TARGET X11::Xfixes)
            add_executable(gui_forms_x11_cursor_tests tests/x11_cursor_tests.cpp)
            target_link_libraries(gui_forms_x11_cursor_tests PRIVATE gui_forms_core X11::Xcursor X11::Xfixes X11::X11)
            add_test(NAME gui_forms_x11_cursors COMMAND gui_forms_x11_cursor_tests)
        endif()
        # Stage T5, C4's measurement: what the X server and a compositing
        # manager do with premultiplied alpha, the 32-bit visual and the
        # _NET_WM_CM_Sn selection (XWayland included). Xlib, XFixes and XInput;
        # prints JSON lines (kept in CI's LastTest.log evidence) and fails
        # only when it cannot open the display. Serial: it reads the screen
        # and may start xcompmgr.
        if(TARGET X11::Xfixes AND TARGET X11::Xi)
            add_executable(gui_forms_x11_compositor_probe tests/x11_compositor_probe.cpp)
            target_link_libraries(gui_forms_x11_compositor_probe PRIVATE X11::Xfixes X11::Xi X11::X11)
            add_test(NAME gui_forms_x11_compositor_probe COMMAND gui_forms_x11_compositor_probe)
            set_tests_properties(gui_forms_x11_compositor_probe PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
            # The same probe under a headless Weston's Xwayland; it reports
            # the case not exercised where Weston is missing.
            add_test(NAME gui_forms_xwayland_compositor_probe
                COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/xwayland_compositor_probe.sh
                    $<TARGET_FILE:gui_forms_x11_compositor_probe>)
            set_tests_properties(gui_forms_xwayland_compositor_probe PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
        endif()
    endif()
    add_executable(gui_forms_cursor_image_tests tests/cursor_image_tests.cpp)
    target_link_libraries(gui_forms_cursor_image_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_cursor_images COMMAND gui_forms_cursor_image_tests)

    add_executable(gui_forms_application_contract_tests tests/application_contract_tests.cpp)
    target_link_libraries(gui_forms_application_contract_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_application_contract_tests COMMAND gui_forms_application_contract_tests)
    if(TARGET gui_forms_application)
        add_executable(gui_forms_application_native_tests tests/application_native_tests.cpp)
        target_link_libraries(gui_forms_application_native_tests PRIVATE gui_forms_application)
        add_test(NAME gui_forms_application_native_tests COMMAND gui_forms_application_native_tests)
        set_tests_properties(gui_forms_application_native_tests PROPERTIES TIMEOUT 30)
        if(WIN32 AND GUI_FORMS_ENABLE_WINDOWS_HOST)
            # Stage T5, phase C2's gate: a non-opaque window on the Windows
            # host through each grant, read off the composed screen. Serial,
            # as on Linux and macOS: another test's window over the sampled
            # points changes the composed colour.
            add_executable(gui_forms_windows_compositor_tests tests/windows_compositor_tests.cpp)
            target_compile_definitions(gui_forms_windows_compositor_tests PRIVATE
                ${GUI_FORMS_WINDOWS_DEFINITIONS})
            target_link_libraries(gui_forms_windows_compositor_tests PRIVATE gui_forms_application dwmapi
                runtimeobject)
            add_test(NAME gui_forms_windows_compositor_tests COMMAND gui_forms_windows_compositor_tests)
            set_tests_properties(gui_forms_windows_compositor_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
        endif()
        if(GUI_FORMS_ENABLE_LINUX_HOST)
            # Stage T5, phase C4's gate: a non-opaque window under Xvfb with no
            # compositing manager, then with xcompmgr started, stopped and
            # started again. Serial: it starts a compositor for the display.
            add_executable(gui_forms_linux_compositor_tests tests/linux_compositor_tests.cpp)
            target_link_libraries(gui_forms_linux_compositor_tests PRIVATE gui_forms_application X11::X11)
            add_test(NAME gui_forms_linux_compositor_tests COMMAND gui_forms_linux_compositor_tests)
            set_tests_properties(gui_forms_linux_compositor_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
        endif()
        if(GUI_FORMS_ENABLE_MACOS_HOST)
            # Stage T5, phase C3's gate: a non-opaque window over a green
            # backdrop, read back from AppKit and off the composed screen.
            # Serial: it floats its windows over the desktop while it reads.
            add_executable(gui_forms_macos_compositor_tests tests/macos_compositor_tests.mm)
            target_compile_options(gui_forms_macos_compositor_tests PRIVATE -fobjc-arc)
            target_link_libraries(gui_forms_macos_compositor_tests PRIVATE gui_forms_application
                ${GUI_FORMS_APPKIT} ${GUI_FORMS_COREGRAPHICS})
            add_test(NAME gui_forms_macos_compositor_tests COMMAND gui_forms_macos_compositor_tests)
            set_tests_properties(gui_forms_macos_compositor_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

            # Native AppKit readback of the window chrome, modal windows and
            # composite focus (Mac handoff 2026-11-09, item 2). Launched
            # through LaunchServices so their windows can become key; serial,
            # since they need the key window. The focus probe writes its
            # captures to handoff_focus_captures/ in this directory.
            foreach(handoff_probe IN ITEMS window_chrome_probe modal_probe composite_focus_probe)
                set(handoff_target gui_forms_macos_${handoff_probe}_tests)
                add_executable(${handoff_target} MACOSX_BUNDLE tests/macos_${handoff_probe}_tests.mm
                    ${GUI_FORMS_GALLERY_FONTS} ${GUI_FORMS_GALLERY_FONT_NOTICES})
                target_compile_options(${handoff_target} PRIVATE -fobjc-arc)
                target_include_directories(${handoff_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
                target_link_libraries(${handoff_target} PRIVATE gui_forms_application ${GUI_FORMS_APPKIT})
                add_test(NAME ${handoff_target}
                         COMMAND ${CMAKE_COMMAND}
                             -DBUNDLE=$<TARGET_BUNDLE_DIR:${handoff_target}>
                             -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_macos_activated_test.cmake)
                set_tests_properties(${handoff_target} PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
            endforeach()
            # Files opened through application:openURLs: (Finder, the Dock),
            # sent as open-documents Apple Events by `open -a`, at launch and
            # during the run.
            add_executable(gui_forms_macos_open_files_probe_app MACOSX_BUNDLE
                tests/macos_open_files_probe_app.mm
                ${GUI_FORMS_GALLERY_FONTS} ${GUI_FORMS_GALLERY_FONT_NOTICES})
            target_compile_options(gui_forms_macos_open_files_probe_app PRIVATE -fobjc-arc)
            target_include_directories(gui_forms_macos_open_files_probe_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
            target_link_libraries(gui_forms_macos_open_files_probe_app PRIVATE gui_forms_application ${GUI_FORMS_APPKIT})
            set_target_properties(gui_forms_macos_open_files_probe_app PROPERTIES
                MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.open-files-probe")
            add_test(NAME gui_forms_macos_open_files_probe_tests
                     COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_macos_open_files_probe.sh
                         $<TARGET_BUNDLE_DIR:gui_forms_macos_open_files_probe_app>
                         ${CMAKE_CURRENT_BINARY_DIR})
            set_tests_properties(gui_forms_macos_open_files_probe_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
            # Quit, logout and restart checks (Mac handoff 2026-11-09, item
            # 2e; cp29 row 14): a primary and a secondary window whose closes
            # are refused while /tmp/gf_term_refuse and
            # /tmp/gf_term_secondary_refuse exist; it logs to /tmp/gf_term.log.
            # The driver test covers the quit Apple Event (Cmd-Q); logout and
            # restart are started by hand with `open -n -a`.
            add_executable(gui_forms_macos_termination_fixture MACOSX_BUNDLE
                tests/macos_termination_fixture.mm
                ${GUI_FORMS_GALLERY_FONTS} ${GUI_FORMS_GALLERY_FONT_NOTICES})
            target_compile_options(gui_forms_macos_termination_fixture PRIVATE -fobjc-arc)
            target_link_libraries(gui_forms_macos_termination_fixture PRIVATE gui_forms_application ${GUI_FORMS_APPKIT})
            set_target_properties(gui_forms_macos_termination_fixture PROPERTIES
                MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.termination-fixture")
            add_test(NAME gui_forms_macos_termination_fixture_tests
                     COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_macos_termination_fixture.sh
                         $<TARGET_BUNDLE_DIR:gui_forms_macos_termination_fixture>)
            set_tests_properties(gui_forms_macos_termination_fixture_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
        endif()
        # Stage DK W0 measurement (notes:results/2026-10-18-dk-w0-capture-drag/): it
        # puts windows on the desktop, so it is built but never run by ctest.
        add_executable(gui_forms_frame_drag_probe tests/frame_drag_probe.cpp)
        target_link_libraries(gui_forms_frame_drag_probe PRIVATE gui_forms_application)
    endif()
    if(GUI_FORMS_ENABLE_LINUX_HOST)
        add_executable(gui_forms_linux_host_tests tests/linux_host_tests.cpp)
        target_link_libraries(gui_forms_linux_host_tests PRIVATE gui_forms_host_linux X11::X11)
        add_test(NAME gui_forms_linux_host_tests COMMAND gui_forms_linux_host_tests)
        set_tests_properties(gui_forms_linux_host_tests PROPERTIES TIMEOUT 45)
        # Audit N66, option a: the host at Xft.dpi 192 under Xvfb, then at 96
        # and 144 during the run. Serial: it rewrites the display's
        # RESOURCE_MANAGER, which every other runtime on the display reads.
        add_executable(gui_forms_linux_hidpi_tests tests/linux_hidpi_tests.cpp)
        target_link_libraries(gui_forms_linux_hidpi_tests PRIVATE gui_forms_host_linux X11::X11)
        add_test(NAME gui_forms_linux_hidpi_tests COMMAND gui_forms_linux_hidpi_tests)
        set_tests_properties(gui_forms_linux_hidpi_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT 45)
        if(TARGET gui_forms_prepared_text_impl)
            add_executable(gui_forms_linux_prepared_text_host_tests
                tests/linux_prepared_text_host_tests.cpp)
            target_link_libraries(gui_forms_linux_prepared_text_host_tests PRIVATE
                gui_forms_host_linux gui_forms_prepared_text_impl X11::X11)
            add_test(NAME gui_forms_linux_prepared_text_host_tests
                COMMAND gui_forms_linux_prepared_text_host_tests
                    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
            set_tests_properties(gui_forms_linux_prepared_text_host_tests PROPERTIES TIMEOUT 45)
        endif()
    endif()
    add_library(gui_forms_reference_examples OBJECT
        examples/reference/clipboard_image.cpp
    )
    target_link_libraries(gui_forms_reference_examples PRIVATE gui_forms_core)
    if(TARGET GUIForms::form_compiler)
        # The windowed references: each window is a form document (forms/),
        # generated at build time as examples/reference/CMakeLists.txt does.
        set(reference_forms examples/reference)
        add_library(gui_forms_reference_form_examples OBJECT
            ${reference_forms}/owned_events.cpp
            ${reference_forms}/bound_values.cpp
            ${reference_forms}/shared_state.cpp
            ${reference_forms}/collection_state.cpp
            ${reference_forms}/multiline_scrollbars.cpp
            ${reference_forms}/window_rules.cpp
            ${reference_forms}/mouse_events.cpp
            ${reference_forms}/collection_gallery.cpp)
        target_link_libraries(gui_forms_reference_form_examples PRIVATE gui_forms_core)
        target_include_directories(gui_forms_reference_form_examples PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/reference)
        gui_forms_add_form(gui_forms_reference_form_examples ${reference_forms}/forms/owned_events.form
            CLASS ExampleWindow NAMESPACE reference DERIVED)
        gui_forms_add_form(gui_forms_reference_form_examples ${reference_forms}/forms/bound_values.form
            CLASS SettingsWindow NAMESPACE reference DERIVED)
        gui_forms_add_form(gui_forms_reference_form_examples ${reference_forms}/forms/shared_state.form
            CLASS SharedWindow NAMESPACE reference
            MODEL reference::SharedModel MODEL_HEADER shared_state_model.hpp)
        gui_forms_add_form(gui_forms_reference_form_examples ${reference_forms}/forms/collection_state.form
            CLASS CollectionsWindow NAMESPACE reference DERIVED)
        gui_forms_add_form(gui_forms_reference_form_examples ${reference_forms}/forms/multiline_scrollbars.form
            CLASS ViewerWindow NAMESPACE reference DERIVED)
        gui_forms_add_form(gui_forms_reference_form_examples ${reference_forms}/forms/window_rules.form
            CLASS RulesWindow NAMESPACE reference DERIVED)
        gui_forms_add_form(gui_forms_reference_form_examples ${reference_forms}/forms/mouse_events.form
            CLASS MouseWindow NAMESPACE reference DERIVED)
        gui_forms_add_form(gui_forms_reference_form_examples ${reference_forms}/forms/collection_gallery.form
            CLASS GalleryWindow NAMESPACE reference
            MODEL reference::GalleryModel MODEL_HEADER collection_gallery_model.hpp)
        gui_forms_add_theme(gui_forms_reference_form_examples ${reference_forms}/themes/collection_gallery
            FUNCTION collection_gallery_theme NAMESPACE reference
            FORMS ${reference_forms}/forms/collection_gallery.form)
    endif()

    find_package(Python3 COMPONENTS Interpreter QUIET)
    find_program(GUI_FORMS_REFERENCE_CLANG NAMES clang++)
    if(Python3_Interpreter_FOUND AND GUI_FORMS_REFERENCE_CLANG)
        add_test(NAME gui_forms_owned_event_compile_failures
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/owned_event_compile_failures.py
                ${GUI_FORMS_REFERENCE_CLANG} ${CMAKE_CURRENT_SOURCE_DIR}/include)
        add_test(NAME gui_forms_stable_id_compile_failures
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/stable_id_compile_failures.py
                ${GUI_FORMS_REFERENCE_CLANG} ${CMAKE_CURRENT_SOURCE_DIR}/include)
        add_test(NAME gui_forms_api_reference_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/api_reference_tests.py)
        # The include directories a consumer of the public headers compiles
        # with: gui_forms_core's own and those of its public dependencies
        # (stx_zlib, stx_png, ...), evaluated transitively by CMake, one per
        # line. tools/api_reference.py --build reads them from here.
        file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/api_reference_include_directories.txt
            CONTENT "$<JOIN:$<TARGET_PROPERTY:gui_forms_core,INTERFACE_INCLUDE_DIRECTORIES>,\n>\n")
        add_custom_target(gui_forms_api_reference
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/api_reference.py
                --clang ${GUI_FORMS_REFERENCE_CLANG}
                --build ${CMAKE_CURRENT_BINARY_DIR}
                --output ${CMAKE_CURRENT_BINARY_DIR}/reference
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Extracting native C++ API and rendering authored contracts"
            VERBATIM)
    endif()
    # No test checks with bare assert(): a Release (NDEBUG) build compiles
    # it out, and the test then checks nothing.
    if(Python3_Interpreter_FOUND)
        add_test(NAME gui_forms_test_assert_check
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_test_asserts.py
                --root ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME gui_forms_test_assert_check_unit
            COMMAND ${Python3_EXECUTABLE} -B -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tools
                -p test_check_test_asserts.py)
        # Every native entry (window procedure, COM method, ATK callback,
        # AppKit selector) runs through the fail-fast boundary, or is listed
        # in tools/native_boundary_baseline.txt, which may only shrink.
        add_test(NAME gui_forms_native_boundary_check
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_boundaries.py
                --root ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME gui_forms_native_boundary_check_self_test
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_native_boundaries.py
                --self-test)
    endif()
    add_executable(gui_drawing_core_tests tests/drawing_core_tests.cpp)
    target_link_libraries(gui_drawing_core_tests PRIVATE gui_drawing_core)
    add_test(NAME gui_drawing_core_tests COMMAND gui_drawing_core_tests)
    add_executable(gui_drawing_font_substitution_tests tests/drawing_font_substitution_tests.cpp)
    target_link_libraries(gui_drawing_font_substitution_tests PRIVATE gui_drawing_core)
    add_test(NAME gui_drawing_font_substitution_tests
             COMMAND gui_drawing_font_substitution_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")

    add_executable(gui_forms_canvas_tests tests/canvas_tests.cpp)
    target_link_libraries(gui_forms_canvas_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_canvas_tests COMMAND gui_forms_canvas_tests)
    if(GUI_FORMS_ENABLE_WINDOWS_HOST)
        add_executable(gui_forms_windows_control_snapshot_tests tests/windows_control_snapshot_tests.cpp)
        target_compile_definitions(gui_forms_windows_control_snapshot_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_control_snapshot_tests PRIVATE
            gui_forms_controls gui_forms_host_windows)
        add_test(NAME gui_forms_windows_control_snapshot_tests COMMAND gui_forms_windows_control_snapshot_tests)
        add_executable(gui_forms_windows_canvas_raster_tests tests/windows_canvas_raster_tests.cpp)
        target_compile_definitions(gui_forms_windows_canvas_raster_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_canvas_raster_tests PRIVATE
            gui_forms_controls gui_forms_host_windows)
        add_test(NAME gui_forms_windows_canvas_raster_tests COMMAND gui_forms_windows_canvas_raster_tests)
        add_executable(gui_forms_windows_damage_rectangles_tests tests/windows_damage_rectangles_tests.cpp)
        target_compile_definitions(gui_forms_windows_damage_rectangles_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_damage_rectangles_tests PRIVATE
            gui_forms_controls gui_forms_host_windows)
        add_test(NAME gui_forms_windows_damage_rectangles_tests
                 COMMAND gui_forms_windows_damage_rectangles_tests)
        add_executable(gui_forms_windows_scroll_copy_tests tests/windows_scroll_copy_tests.cpp)
        target_compile_definitions(gui_forms_windows_scroll_copy_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS}
            GUI_FORMS_WINDOWS_SCROLL_COPY_TEST=1)
        target_link_libraries(gui_forms_windows_scroll_copy_tests PRIVATE
            gui_forms_controls gui_forms_host_windows)
        add_test(NAME gui_forms_windows_scroll_copy_tests
                 COMMAND gui_forms_windows_scroll_copy_tests)
        add_executable(gui_forms_windows_live_idle_tests tests/windows_live_idle_tests.cpp)
        target_compile_definitions(gui_forms_windows_live_idle_tests PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00
            GUI_FORMS_WINDOWS_LIVE_IDLE_TEST=1)
        target_link_libraries(gui_forms_windows_live_idle_tests PRIVATE
            gui_forms_controls gui_forms_host_windows)
        add_test(NAME gui_forms_windows_live_idle_tests
                 COMMAND gui_forms_windows_live_idle_tests)
        add_executable(gui_forms_windows_text_tests tests/windows_text_tests.cpp)
        target_compile_definitions(gui_forms_windows_text_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_text_tests PRIVATE
            gui_forms_controls gui_forms_host_windows)
        add_test(NAME gui_forms_windows_text_tests COMMAND gui_forms_windows_text_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        add_executable(gui_forms_windows_backplane_replay_tests tests/windows_backplane_replay_tests.cpp)
        target_compile_definitions(gui_forms_windows_backplane_replay_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_backplane_replay_tests PRIVATE
            gui_forms_controls gui_forms_host_windows)
        add_test(NAME gui_forms_windows_backplane_replay_tests COMMAND gui_forms_windows_backplane_replay_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        # Stage T5, phase C2's measurement: what DWM composes from a
        # premultiplied client through each documented route. Prints JSON
        # lines (kept in CI's LastTest.log evidence); fails only when it
        # cannot make its windows or read the screen. Serial: it reads the
        # composed screen too.
        add_executable(gui_forms_windows_compositor_probe tests/windows_compositor_probe.cpp)
        target_compile_definitions(gui_forms_windows_compositor_probe PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_compositor_probe PRIVATE dwmapi runtimeobject)
        add_test(NAME gui_forms_windows_compositor_probe COMMAND gui_forms_windows_compositor_probe)
        set_tests_properties(gui_forms_windows_compositor_probe PROPERTIES RUN_SERIAL TRUE)
        add_executable(gui_forms_windows_content_pixels_tests tests/windows_content_pixels_tests.cpp)
        target_compile_definitions(gui_forms_windows_content_pixels_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_content_pixels_tests PRIVATE
            gui_forms_controls gui_forms_host_windows)
        add_test(NAME gui_forms_windows_content_pixels_tests COMMAND gui_forms_windows_content_pixels_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")

    endif()


    add_executable(gui_drawing_c_api_c11_tests tests/drawing_c_api_c11_tests.c)
    target_link_libraries(gui_drawing_c_api_c11_tests PRIVATE gui_drawing_c_api)
    add_test(NAME gui_drawing_c_api_c11_tests COMMAND gui_drawing_c_api_c11_tests)

    add_executable(gui_drawing_trace_equivalence_tests
        tests/drawing_trace_equivalence_tests.cpp
    )
    target_link_libraries(gui_drawing_trace_equivalence_tests PRIVATE
        gui_drawing_core
        gui_drawing_c_api
    )
    add_test(NAME gui_drawing_trace_equivalence_tests
             COMMAND gui_drawing_trace_equivalence_tests)

    # The stx package stamp's digests ignore line endings
    # (cmake/StxDigest.cmake, tests/cmake/stx_digest_test.cmake).
    add_test(NAME gui_forms_stx_digest
             COMMAND ${CMAKE_COMMAND} -DWORK_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/stx_digest_test
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/stx_digest_test.cmake)
    # Public headers name no renderer or platform type; comments may
    # (cmake/check_public_header_tokens.cmake). The fixtures prove a comment
    # passes and a token in code fails.
    add_test(NAME gui_forms_public_header_tokens
             COMMAND ${CMAKE_COMMAND} -DPUBLIC_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_public_header_tokens.cmake)
    add_test(NAME gui_forms_public_header_tokens_comments_pass
             COMMAND ${CMAKE_COMMAND}
                     -DPUBLIC_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/public_header_tokens/comments_only
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_public_header_tokens.cmake)
    add_test(NAME gui_forms_public_header_tokens_code_fails
             COMMAND ${CMAKE_COMMAND}
                     -DPUBLIC_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/public_header_tokens/in_code
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_public_header_tokens.cmake)
    set_tests_properties(gui_forms_public_header_tokens_code_fails PROPERTIES
        PASS_REGULAR_EXPRESSION "exposes renderer/platform token: CGContext")

    add_test(
        NAME gui_drawing_renderer_boundary_audit
        COMMAND ${CMAKE_COMMAND}
            -DDRAWING_CORE=$<TARGET_FILE:gui_drawing_core>
            -DDRAWING_C_API=$<TARGET_FILE:gui_drawing_c_api>
            -DDRAWING_TEST_BINARY=$<TARGET_FILE:gui_drawing_core_tests>
            -DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_drawing_boundaries.cmake
    )

    if(APPLE)
        add_test(
            NAME gui_drawing_c_api_export_policy
            COMMAND ${CMAKE_COMMAND}
                -DC_API_LIBRARY=$<TARGET_FILE:gui_drawing_c_api>
                -DEXPECTED_SYMBOL=_gd_get_api_v0
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_single_c_api_export.cmake
        )
    endif()

    add_executable(gui_forms_host_image_tests tests/host_image_tests.cpp)
    target_link_libraries(gui_forms_host_image_tests PRIVATE gui_forms_host_headless)
    add_test(NAME gui_forms_host_image_tests COMMAND gui_forms_host_image_tests)

    # The deflate contract harness's standalone inputs, in the normal suite.
    gui_forms_add_fuzz_test(deflate SOURCE fuzz/deflate_fuzz.cpp LIBRARIES stx_zlib::stx_zlib)

    add_executable(gui_forms_inflate_protocol_tests fuzz/inflate_protocol_fuzz.cpp)
    target_link_libraries(gui_forms_inflate_protocol_tests PRIVATE gui_forms_core)
    target_compile_definitions(gui_forms_inflate_protocol_tests PRIVATE GUI_FORMS_FUZZ_STANDALONE=1)
    add_test(NAME gui_forms_inflate_protocol_tests COMMAND gui_forms_inflate_protocol_tests)

    add_executable(gui_forms_png_pixels_tests tests/image/png_pixels_tests.cpp)
    target_link_libraries(gui_forms_png_pixels_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_png_pixels_tests COMMAND gui_forms_png_pixels_tests)

    # The codec-neutral resource seam: admission and decoding.
    add_executable(gui_forms_resource_pixels_tests tests/image/resource_pixels_tests.cpp)
    target_link_libraries(gui_forms_resource_pixels_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_resource_pixels_tests COMMAND gui_forms_resource_pixels_tests)
    # A timing probe for audit P12, built on demand and never a test.
    add_executable(gui_forms_png_decode_probe EXCLUDE_FROM_ALL tests/image/png_decode_probe.cpp)
    target_link_libraries(gui_forms_png_decode_probe PRIVATE gui_forms_core)

    # Deterministic malformed-input sweeps (owner revision of decision 6A).
    add_executable(gui_forms_malformed_sweep_tests tests/compression/malformed_sweep_tests.cpp)
    target_link_libraries(gui_forms_malformed_sweep_tests PRIVATE stx_zlib::stx_zlib)
    add_test(NAME gui_forms_malformed_sweep_tests COMMAND gui_forms_malformed_sweep_tests)
    add_executable(gui_forms_png_malformed_sweep_tests tests/image/png_malformed_sweep_tests.cpp)
    target_link_libraries(gui_forms_png_malformed_sweep_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_png_malformed_sweep_tests COMMAND gui_forms_png_malformed_sweep_tests)

    if(TARGET stx_text::freetype)
        # Decision 3A: FreeType refuses compressed data; plain fonts still load.
        add_executable(gui_forms_freetype_zlib_tests tests/freetype_zlib_tests.cpp)
        target_link_libraries(gui_forms_freetype_zlib_tests PRIVATE stx_text::freetype)
        target_compile_definitions(gui_forms_freetype_zlib_tests PRIVATE
            GUI_FORMS_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids.ttf")
        add_test(NAME gui_forms_freetype_zlib_tests COMMAND gui_forms_freetype_zlib_tests)
    endif()

    add_executable(gui_forms_png_color_tests tests/image/png_color_tests.cpp)
    target_link_libraries(gui_forms_png_color_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_png_color_tests COMMAND gui_forms_png_color_tests)

    # The internal interpolation seam over stx_conv (binary64, clamped).
    add_executable(gui_forms_interpolation_tests tests/interpolation/interpolation_tests.cpp)
    target_link_libraries(gui_forms_interpolation_tests PRIVATE gui_forms_core stx_conv::stx_conv)
    add_test(NAME gui_forms_interpolation_tests COMMAND gui_forms_interpolation_tests)

    # By-hand measurement of encoder size and time per level; not a test.
    add_executable(gui_forms_png_encode_benchmark tests/image/png_encode_benchmark.cpp)
    target_link_libraries(gui_forms_png_encode_benchmark PRIVATE gui_forms_core)

    add_executable(gui_forms_png_writer_tests tests/png_writer_tests.cpp)
    target_link_libraries(gui_forms_png_writer_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_png_writer_tests COMMAND gui_forms_png_writer_tests)

    add_executable(gui_forms_core_tests tests/core_tests.cpp)
    target_link_libraries(gui_forms_core_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_core_tests COMMAND gui_forms_core_tests)

    add_executable(gui_forms_page_set_tests tests/page_set_tests.cpp)
    target_link_libraries(gui_forms_page_set_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_page_set_tests COMMAND gui_forms_page_set_tests)

    add_executable(gui_forms_engine_raster_cache_tests tests/engine_raster_cache_tests.cpp)
    target_link_libraries(gui_forms_engine_raster_cache_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_engine_raster_cache_tests COMMAND gui_forms_engine_raster_cache_tests)
    add_executable(gui_forms_engine_part_table_tests tests/engine_part_table_tests.cpp)
    target_link_libraries(gui_forms_engine_part_table_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_engine_part_table_tests COMMAND gui_forms_engine_part_table_tests)
    add_executable(gui_forms_part_painting_tests tests/part_painting_tests.cpp)
    target_link_libraries(gui_forms_part_painting_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_part_painting_tests COMMAND gui_forms_part_painting_tests)
    add_executable(gui_forms_theme_contrast_tests tests/theme_contrast_tests.cpp)
    target_link_libraries(gui_forms_theme_contrast_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_contrast_tests COMMAND gui_forms_theme_contrast_tests)
    add_executable(gui_forms_dark_variant_tests tests/dark_variant_tests.cpp)
    target_link_libraries(gui_forms_dark_variant_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_dark_variant_tests COMMAND gui_forms_dark_variant_tests)
    add_executable(gui_forms_gui_forms_theme_tests tests/gui_forms_theme_tests.cpp)
    target_link_libraries(gui_forms_gui_forms_theme_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_gui_forms_theme_tests COMMAND gui_forms_gui_forms_theme_tests)
    add_executable(gui_forms_familiar_themes_tests tests/familiar_themes_tests.cpp)
    target_link_libraries(gui_forms_familiar_themes_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_familiar_themes_tests COMMAND gui_forms_familiar_themes_tests)
    add_executable(gui_forms_owner_draw_tests tests/owner_draw_tests.cpp)
    target_link_libraries(gui_forms_owner_draw_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_owner_draw_tests COMMAND gui_forms_owner_draw_tests)
    add_executable(gui_forms_theme_high_contrast_tests tests/theme_high_contrast_tests.cpp)
    target_link_libraries(gui_forms_theme_high_contrast_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_high_contrast_tests COMMAND gui_forms_theme_high_contrast_tests)
    add_executable(gui_forms_theme_cascade_tests tests/theme_cascade_tests.cpp)
    target_link_libraries(gui_forms_theme_cascade_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_cascade_tests COMMAND gui_forms_theme_cascade_tests)
    add_executable(gui_forms_theme_user_layer_tests tests/theme_user_layer_tests.cpp)
    target_link_libraries(gui_forms_theme_user_layer_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_user_layer_tests COMMAND gui_forms_theme_user_layer_tests)

    add_executable(gui_forms_theme_user_fill_selection_tests tests/theme_user_fill_selection_tests.cpp)
    target_link_libraries(gui_forms_theme_user_fill_selection_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_user_fill_selection_tests COMMAND gui_forms_theme_user_fill_selection_tests)

    add_executable(gui_forms_effect_program_tests tests/effect_program_tests.cpp)
    target_link_libraries(gui_forms_effect_program_tests PRIVATE gui_forms_controls)
    if(GUI_FORMS_EFFECTS_VERIFIED)
        target_compile_definitions(gui_forms_effect_program_tests PRIVATE GUI_FORMS_EFFECT_TEST_VERIFIED)
    endif()
    add_test(NAME gui_forms_effect_program_tests COMMAND gui_forms_effect_program_tests)
    # The effect pass itself: each case under tests/effect_pass is an object
    # library outside `all`, compiled by its test with the plugin (or, for
    # `unverified`, without it), its diagnostics matched against the
    # `// expect:` line at the head of the case. The tests run the build tool
    # in this tree, so they take the tree's lock one at a time.
    if(GUI_FORMS_EFFECTS_VERIFIED)
        set(_gf_effect_cases
            passes while_loop unbounded_for loop_variable_written recursion virtual_call function_pointer
            allocation global_write cmath_call opaque_call pointer_state oversized_state over_budget unverified
            signal_passes signal_over_budget signal_cmath_call)
        foreach(_gf_case IN LISTS _gf_effect_cases)
            set(_gf_case_target gui_forms_effect_case_${_gf_case})
            add_library(${_gf_case_target} OBJECT EXCLUDE_FROM_ALL tests/effect_pass/${_gf_case}.cpp)
            # For the usage requirements only: with Ninja an object library's
            # compiles do not wait for the libraries it links.
            target_link_libraries(${_gf_case_target} PRIVATE gui_forms_controls)
            if(_gf_case STREQUAL "unverified")
                target_compile_definitions(${_gf_case_target} PRIVATE GUI_FORMS_EFFECTS_UNVERIFIED)
            else()
                gui_forms_compile_with_localizable_plugin(${_gf_case_target})
            endif()
            add_test(NAME gui_forms_effect_pass_${_gf_case}
                     COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=${_gf_case_target}
                             -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/effect_pass/${_gf_case}.cpp
                             -DOBJECT_DIR=CMakeFiles/${_gf_case_target}.dir
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_effect_pass.cmake)
            set_tests_properties(gui_forms_effect_pass_${_gf_case} PROPERTIES RESOURCE_LOCK gui_forms_build_tree)
        endforeach()
    endif()
    add_executable(gui_forms_motion_clock_tests tests/motion_clock_tests.cpp)
    target_link_libraries(gui_forms_motion_clock_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_motion_clock_tests COMMAND gui_forms_motion_clock_tests)
    add_executable(gui_forms_engine_math_tests tests/engine_math_tests.cpp)
    target_link_libraries(gui_forms_engine_math_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_engine_math_tests COMMAND gui_forms_engine_math_tests)
    add_executable(gui_forms_engine_expression_tests tests/engine_expression_tests.cpp)
    target_link_libraries(gui_forms_engine_expression_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_engine_expression_tests COMMAND gui_forms_engine_expression_tests)
    add_executable(gui_forms_engine_input_tests tests/engine_input_tests.cpp)
    target_link_libraries(gui_forms_engine_input_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_input_tests COMMAND gui_forms_engine_input_tests)
    add_executable(gui_forms_engine_motion_tests tests/engine_motion_tests.cpp)
    target_link_libraries(gui_forms_engine_motion_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_motion_tests COMMAND gui_forms_engine_motion_tests)
    add_executable(gui_forms_engine_primitive_tests tests/engine_primitive_tests.cpp)
    target_link_libraries(gui_forms_engine_primitive_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_engine_primitive_tests COMMAND gui_forms_engine_primitive_tests)
    add_executable(gui_forms_engine_relief_tests tests/engine_relief_tests.cpp)
    target_link_libraries(gui_forms_engine_relief_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_relief_tests COMMAND gui_forms_engine_relief_tests)
    add_executable(gui_forms_engine_scale_slot_tests tests/engine_scale_slot_tests.cpp)
    target_link_libraries(gui_forms_engine_scale_slot_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_scale_slot_tests COMMAND gui_forms_engine_scale_slot_tests)
    add_executable(gui_forms_engine_motion_curve_tests tests/engine_motion_curve_tests.cpp)
    target_link_libraries(gui_forms_engine_motion_curve_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_motion_curve_tests COMMAND gui_forms_engine_motion_curve_tests)
    add_executable(gui_forms_engine_material_tests tests/engine_material_tests.cpp)
    target_link_libraries(gui_forms_engine_material_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_material_tests COMMAND gui_forms_engine_material_tests)
    add_executable(gui_forms_engine_authored_material_tests tests/engine_authored_material_tests.cpp)
    target_link_libraries(gui_forms_engine_authored_material_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_authored_material_tests COMMAND gui_forms_engine_authored_material_tests)
    add_executable(gui_forms_engine_recipe_field_tests tests/engine_recipe_field_tests.cpp)
    target_link_libraries(gui_forms_engine_recipe_field_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_recipe_field_tests COMMAND gui_forms_engine_recipe_field_tests)
    add_executable(gui_forms_engine_procedural_tests tests/engine_procedural_tests.cpp)
    target_link_libraries(gui_forms_engine_procedural_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_procedural_tests COMMAND gui_forms_engine_procedural_tests)
    # Surface finishes (skeudoindustrial surfaces, phase 1): the kernel's
    # bytes against the JavaScript reference, damage invariance, the
    # material layer and finish(); --measure and --preview <dir>.
    add_executable(gui_forms_engine_finish_tests tests/engine_finish_tests.cpp)
    target_link_libraries(gui_forms_engine_finish_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_finish_tests COMMAND gui_forms_engine_finish_tests)
    add_executable(gui_forms_engine_finish_tile_tests tests/engine_finish_tile_tests.cpp)
    target_link_libraries(gui_forms_engine_finish_tile_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_finish_tile_tests COMMAND gui_forms_engine_finish_tile_tests)
    # Lamps and lit keys as baked light planes (phase 3): the bake file,
    # the composite, the envelopes and the cache; --preview <out> <bakes>.
    add_executable(gui_forms_engine_light_planes_tests tests/engine_light_planes_tests.cpp)
    target_link_libraries(gui_forms_engine_light_planes_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_light_planes_tests COMMAND gui_forms_engine_light_planes_tests)
    add_executable(gui_forms_engine_slice_tests tests/engine_slice_tests.cpp)
    target_link_libraries(gui_forms_engine_slice_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_slice_tests COMMAND gui_forms_engine_slice_tests)

    add_executable(gui_forms_engine_retained_tests tests/engine_retained_tests.cpp)
    target_link_libraries(gui_forms_engine_retained_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_retained_tests COMMAND gui_forms_engine_retained_tests)

    add_executable(gui_forms_engine_glass_tests tests/engine_glass_tests.cpp)
    target_link_libraries(gui_forms_engine_glass_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_glass_tests COMMAND gui_forms_engine_glass_tests)
    add_executable(gui_forms_engine_backdrop_tests tests/engine_backdrop_tests.cpp)
    target_link_libraries(gui_forms_engine_backdrop_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_backdrop_tests COMMAND gui_forms_engine_backdrop_tests)
    add_executable(gui_forms_engine_reflection_tests tests/engine_reflection_tests.cpp)
    target_link_libraries(gui_forms_engine_reflection_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_reflection_tests COMMAND gui_forms_engine_reflection_tests)
    add_executable(gui_forms_engine_backplane_replay_tests tests/engine_backplane_replay_tests.cpp)
    target_link_libraries(gui_forms_engine_backplane_replay_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_backplane_replay_tests COMMAND gui_forms_engine_backplane_replay_tests)
    add_executable(gui_forms_engine_light_tests tests/engine_light_tests.cpp)
    target_link_libraries(gui_forms_engine_light_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_light_tests COMMAND gui_forms_engine_light_tests)
    add_executable(gui_forms_engine_pop_tests tests/engine_pop_tests.cpp)
    target_link_libraries(gui_forms_engine_pop_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_pop_tests COMMAND gui_forms_engine_pop_tests)

    # T2's gate evidence (E11): every registered primitive's and every feature
    # sample's reference images, the census of features, and the idle-frame
    # counters, written as a manifest that CI compares across hosts
    # (tools/engine_gate.py).
    add_executable(gui_forms_engine_gate_tests tests/engine_gate_tests.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        # The census names every value of the engine's vocabulary in a switch:
        # a value added without a name (and a feature sample) fails the build.
        set_source_files_properties(tests/engine_gate_tests.cpp PROPERTIES COMPILE_OPTIONS "-Werror=switch")
    endif()
    target_link_libraries(gui_forms_engine_gate_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_gate_tests COMMAND gui_forms_engine_gate_tests)

    add_executable(gui_forms_sha256_tests tests/sha256_tests.cpp)
    target_link_libraries(gui_forms_sha256_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_sha256_tests COMMAND gui_forms_sha256_tests)

    # The bundled font set over the platform's sealed pages (item 4, step 3); the fault mode
    # must end in the platform's protection fault (cmake/expect_fault.cmake).
    add_executable(gui_forms_bundled_font_set_tests
        tests/bundled_font_set_tests.cpp $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
    target_link_libraries(gui_forms_bundled_font_set_tests PRIVATE gui_forms_core)
    target_compile_definitions(gui_forms_bundled_font_set_tests PRIVATE
        GUI_FORMS_TEST_FONT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
    add_test(NAME gui_forms_bundled_font_set_tests COMMAND gui_forms_bundled_font_set_tests)
    add_test(NAME gui_forms_bundled_font_set_write_faults
             COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gui_forms_bundled_font_set_tests>
                     -DMODE=--write-sealed
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/expect_fault.cmake)

    add_executable(gui_forms_engine_fill_shape_tests tests/engine_fill_shape_tests.cpp)
    target_link_libraries(gui_forms_engine_fill_shape_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_fill_shape_tests COMMAND gui_forms_engine_fill_shape_tests)
    add_executable(gui_forms_engine_filter_blend_tests tests/engine_filter_blend_tests.cpp)
    target_link_libraries(gui_forms_engine_filter_blend_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_engine_filter_blend_tests COMMAND gui_forms_engine_filter_blend_tests)

    add_executable(gui_forms_damage_clip_tests tests/damage_clip_tests.cpp)
    target_link_libraries(gui_forms_damage_clip_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_damage_clip_tests COMMAND gui_forms_damage_clip_tests)
    add_executable(gui_forms_invalidation_overlay_tests tests/invalidation_overlay_tests.cpp)
    target_link_libraries(gui_forms_invalidation_overlay_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_invalidation_overlay_tests COMMAND gui_forms_invalidation_overlay_tests)

    add_executable(gui_forms_control_table_tests tests/control_table_tests.cpp)
    target_link_libraries(gui_forms_control_table_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_control_table_tests COMMAND gui_forms_control_table_tests)
    add_executable(gui_forms_tree_cost_tests tests/tree_cost_tests.cpp)
    target_link_libraries(gui_forms_tree_cost_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_tree_cost_tests COMMAND gui_forms_tree_cost_tests)
    add_executable(gui_forms_control_core_tests tests/control_core_tests.cpp)
    target_link_libraries(gui_forms_control_core_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_control_core_tests COMMAND gui_forms_control_core_tests)
    add_executable(gui_forms_window_correctness_tests tests/window_correctness_tests.cpp)
    target_link_libraries(gui_forms_window_correctness_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_window_correctness_tests COMMAND gui_forms_window_correctness_tests)
    add_executable(gui_forms_property_capability_tests tests/property_capability_tests.cpp)
    target_link_libraries(gui_forms_property_capability_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_property_capability_tests COMMAND gui_forms_property_capability_tests)
    # Written by tools/property_codemod.py tests from tools/property_families/.
    add_executable(gui_forms_property_family_generated_tests tests/property_family_generated_tests.cpp)
    target_link_libraries(gui_forms_property_family_generated_tests PRIVATE gui_forms_lab gui_forms_demo_controls)
    add_test(NAME gui_forms_property_family_generated_tests COMMAND gui_forms_property_family_generated_tests)

    add_executable(gui_forms_damage_rectangles_tests tests/damage_rectangles_tests.cpp)
    target_link_libraries(gui_forms_damage_rectangles_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_damage_rectangles_tests COMMAND gui_forms_damage_rectangles_tests)

    add_executable(gui_forms_binary_search_tests tests/binary_search_tests.cpp)
    target_link_libraries(gui_forms_binary_search_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_binary_search_tests COMMAND gui_forms_binary_search_tests)

    add_executable(gui_forms_sort_algorithm_tests tests/sort_algorithm_tests.cpp)
    target_link_libraries(gui_forms_sort_algorithm_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_sort_algorithm_tests COMMAND gui_forms_sort_algorithm_tests)

    add_executable(gui_forms_sort_lab tools/sort_lab.cpp)
    target_link_libraries(gui_forms_sort_lab PRIVATE gui_forms_core)

    add_executable(gui_forms_image_patch_lab tools/image_patch_lab.cpp)
    target_link_libraries(gui_forms_image_patch_lab PRIVATE gui_forms_core)

    add_executable(gui_forms_callback_lab tools/callback_lab.cpp)
    target_link_libraries(gui_forms_callback_lab PRIVATE gui_forms_core)
    # The dark colour rule as a filter, for tools/relocate_colours.py.
    add_executable(gui_forms_dark_colour tools/dark_colour.cpp)
    target_link_libraries(gui_forms_dark_colour PRIVATE gui_forms_core)

    if(TARGET gui_forms_skia)
        add_executable(gui_forms_concise_authoring_lab tools/concise_authoring_lab.cpp)
        target_link_libraries(gui_forms_concise_authoring_lab PRIVATE gui_forms_controls gui_forms_skia)
    endif()
    add_executable(gui_forms_owned_event_lab tools/owned_event_lab.cpp)
    target_link_libraries(gui_forms_owned_event_lab PRIVATE gui_forms_core)
    add_executable(gui_forms_property_capability_lab tools/property_capability_lab.cpp)
    target_link_libraries(gui_forms_property_capability_lab PRIVATE gui_forms_controls)

    add_executable(gui_forms_delegate_event_tests tests/delegate_event_tests.cpp)
    target_link_libraries(gui_forms_delegate_event_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_delegate_event_tests COMMAND gui_forms_delegate_event_tests)

    add_executable(gui_forms_live_surface_pacer_tests tests/live_surface_pacer_tests.cpp)
    target_link_libraries(gui_forms_live_surface_pacer_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_live_surface_pacer_tests COMMAND gui_forms_live_surface_pacer_tests)
    add_executable(gui_forms_sub_surface_tests tests/sub_surface_tests.cpp)
    target_link_libraries(gui_forms_sub_surface_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_sub_surface_tests COMMAND gui_forms_sub_surface_tests)
    set_tests_properties(gui_forms_sub_surface_tests PROPERTIES TIMEOUT 120)
    add_executable(gui_forms_data_grid_view_cells_tests tests/data_grid_view_cells_tests.cpp)
    target_link_libraries(gui_forms_data_grid_view_cells_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_data_grid_view_cells_tests COMMAND gui_forms_data_grid_view_cells_tests)
    set_tests_properties(gui_forms_data_grid_view_cells_tests PROPERTIES TIMEOUT 60)
    add_executable(gui_forms_live_surface_idle_wake_tests tests/live_surface_idle_wake_tests.cpp)
    target_link_libraries(gui_forms_live_surface_idle_wake_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_live_surface_idle_wake_tests COMMAND gui_forms_live_surface_idle_wake_tests)
    # A hang detector, not a speed budget: the test takes well under a second
    # alone and must not fail because a loaded machine schedules its threads late.
    set_tests_properties(gui_forms_live_surface_idle_wake_tests PROPERTIES TIMEOUT 120)

    add_executable(gui_forms_threading_tests tests/threading_tests.cpp)
    target_link_libraries(gui_forms_threading_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_threading_tests COMMAND gui_forms_threading_tests)
    set_tests_properties(gui_forms_threading_tests PROPERTIES TIMEOUT 15)

    add_executable(gui_forms_owned_event_tests tests/owned_event_tests.cpp)
    target_link_libraries(gui_forms_owned_event_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_owned_event_tests COMMAND gui_forms_owned_event_tests)

    # Every example in the API reference, generated from docs/library/docstrings/
    # by tools/generate_library_docs.py, compiled and run without a host.
    add_executable(gui_forms_doc_examples_tests tests/doc_examples_tests.cpp)
    target_link_libraries(gui_forms_doc_examples_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_doc_examples_tests COMMAND gui_forms_doc_examples_tests)
    # The examples marked `// compile only` need a native host: they are
    # compiled against the Application library but never linked or run.
    if(TARGET gui_forms_application)
        add_library(gui_forms_doc_examples_compile_only OBJECT
            tests/doc_examples_compile_only.cpp)
        target_link_libraries(gui_forms_doc_examples_compile_only PRIVATE gui_forms_application)
    endif()

    add_executable(gui_forms_part_composite_tests tests/part_composite_tests.cpp)
    target_link_libraries(gui_forms_part_composite_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_part_composite_tests COMMAND gui_forms_part_composite_tests)
    add_executable(gui_forms_paint_group_tests tests/paint_group_tests.cpp)
    target_link_libraries(gui_forms_paint_group_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_paint_group_tests COMMAND gui_forms_paint_group_tests)
    add_executable(gui_forms_text_composite_tests tests/text_composite_tests.cpp)
    target_link_libraries(gui_forms_text_composite_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_text_composite_tests COMMAND gui_forms_text_composite_tests)
    add_executable(gui_forms_live_surface_tests tests/live_surface_tests.cpp)
    target_link_libraries(gui_forms_live_surface_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_live_surface_tests COMMAND gui_forms_live_surface_tests)
    add_executable(gui_forms_opaque_live_surface_tests tests/opaque_live_surface_tests.cpp
        src/host/macos/application/live_surface_damage.cpp)
    target_link_libraries(gui_forms_opaque_live_surface_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_opaque_live_surface_tests COMMAND gui_forms_opaque_live_surface_tests)

    add_executable(gui_forms_text_store_tests tests/text_store_tests.cpp)
    target_link_libraries(gui_forms_text_store_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_text_store_tests COMMAND gui_forms_text_store_tests)
    # The markdown framework: parsing, source ranges, the cross-check against
    # stx_rtf2md's normative reader over its dialect corpus, and RTF round trips.
    add_executable(gui_forms_markdown_tests tests/markdown_tests.cpp)
    target_link_libraries(gui_forms_markdown_tests PRIVATE gui_forms_core stx_rtf2md::stx_rtf2md)
    # The corpus is stx's (stx_rtf2md/tests/corpus), installed with the stx package.
    add_test(NAME gui_forms_markdown_tests
        COMMAND gui_forms_markdown_tests ${GUI_FORMS_STX_BUILD_PREFIX_DIR}/share/stx_text/rtf2md_corpus)
    gui_forms_add_fuzz_test(markdown SOURCE fuzz/markdown_fuzz.cpp LIBRARIES gui_forms_core)
    gui_forms_add_fuzz_test(rtf_import SOURCE fuzz/rtf_import_fuzz.cpp
        LIBRARIES gui_forms_core stx_rtf2md::stx_rtf2md
        ARGUMENTS ${GUI_FORMS_STX_BUILD_PREFIX_DIR}/share/stx_text/rtf2md_corpus)
    add_executable(gui_forms_rtf_conversion_failure_tests tests/rtf_conversion_failure_tests.cpp)
    target_link_libraries(gui_forms_rtf_conversion_failure_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_rtf_conversion_failure_tests COMMAND gui_forms_rtf_conversion_failure_tests)

    add_executable(gui_forms_bounded_grapheme_tests tests/bounded_grapheme_tests.cpp)
    target_include_directories(gui_forms_bounded_grapheme_tests PRIVATE src/core/text/unicode)
    target_link_libraries(gui_forms_bounded_grapheme_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_bounded_grapheme_tests COMMAND gui_forms_bounded_grapheme_tests)

    add_executable(gui_forms_document_view_tests tests/document_view_tests.cpp)
    target_link_libraries(gui_forms_document_view_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_document_view_tests COMMAND gui_forms_document_view_tests)

    add_executable(gui_forms_help_system_core_tests tests/help_system_core_tests.cpp)
    target_link_libraries(gui_forms_help_system_core_tests PRIVATE gui_forms_controls gui_forms_core)
    add_test(NAME gui_forms_help_system_core_tests COMMAND gui_forms_help_system_core_tests)
    add_executable(gui_forms_locale_file_tests tests/locale_file_tests.cpp)
    target_link_libraries(gui_forms_locale_file_tests PRIVATE gui_forms_controls gui_forms_core)
    add_test(NAME gui_forms_locale_file_tests COMMAND gui_forms_locale_file_tests)
    gui_forms_add_fuzz_test(helpbook_section SOURCE fuzz/helpbook_section_fuzz.cpp LIBRARIES gui_forms_core)

    add_executable(gui_forms_xml_tests tests/xml_tests.cpp)
    target_link_libraries(gui_forms_xml_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_xml_tests COMMAND gui_forms_xml_tests)
    add_executable(gui_forms_json_reader_tests tests/json_reader_tests.cpp)
    target_link_libraries(gui_forms_json_reader_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_json_reader_tests COMMAND gui_forms_json_reader_tests)
    gui_forms_add_fuzz_test(json_reader SOURCE fuzz/json_reader_fuzz.cpp LIBRARIES gui_forms_core)

    # The theme language (stage T3): numbers, tokens, diagnostics, the syntax
    # tree, selectors, values, the compiler, and the deterministic mutation
    # run over the tokenizer, parser and value reader.
    add_executable(gui_forms_theme_number_tests tests/theme_language/theme_number_tests.cpp)
    target_link_libraries(gui_forms_theme_number_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_number_tests COMMAND gui_forms_theme_number_tests)

    add_executable(gui_forms_theme_tokenizer_tests tests/theme_language/theme_tokenizer_tests.cpp)
    target_link_libraries(gui_forms_theme_tokenizer_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_tokenizer_tests COMMAND gui_forms_theme_tokenizer_tests)

    add_executable(gui_forms_theme_syntax_tests tests/theme_language/theme_syntax_tests.cpp)
    target_link_libraries(gui_forms_theme_syntax_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_syntax_tests COMMAND gui_forms_theme_syntax_tests)
    add_executable(gui_forms_form_syntax_tests tests/form_language/form_syntax_tests.cpp)
    target_link_libraries(gui_forms_form_syntax_tests PRIVATE gui_forms_core)
    target_compile_definitions(gui_forms_form_syntax_tests PRIVATE
        GUI_FORMS_FORM_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/form_language/corpus")
    add_test(NAME gui_forms_form_syntax_tests COMMAND gui_forms_form_syntax_tests)
    add_executable(gui_forms_form_model_tests tests/form_language/form_model_tests.cpp)
    target_link_libraries(gui_forms_form_model_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_form_model_tests COMMAND gui_forms_form_model_tests)
    add_executable(gui_forms_form_loader_tests tests/form_language/form_loader_tests.cpp)
    target_link_libraries(gui_forms_form_loader_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_form_loader_tests COMMAND gui_forms_form_loader_tests)
    add_executable(gui_forms_form_editor_tests tests/form_language/form_editor_tests.cpp)
    target_link_libraries(gui_forms_form_editor_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_form_editor_tests COMMAND gui_forms_form_editor_tests)
    add_executable(gui_forms_form_generator_tests tests/form_language/form_generator_tests.cpp
        tools/form_compiler/form_catalog.cpp tools/form_compiler/form_lab_catalog.cpp)
    target_link_libraries(gui_forms_form_generator_tests PRIVATE gui_forms_lab gui_forms_demo_controls)
    target_compile_definitions(gui_forms_form_generator_tests PRIVATE
        GUI_FORMS_FORM_GENERATED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/form_language/generated")
    add_test(NAME gui_forms_form_generator_tests COMMAND gui_forms_form_generator_tests)
    if(TARGET gui_forms_form_compiler)
        # The program end to end: the golden header is current, and a
        # document it cannot spell is refused with a located E812.
        add_test(NAME gui_forms_form_compiler_tests
            COMMAND gui_forms_form_compiler
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/form_language/generated/sample_dialog.form
                --class SampleDialogForm --namespace form_generated --check
                --output ${CMAKE_CURRENT_SOURCE_DIR}/tests/form_language/generated/sample_dialog_form.hpp)
        add_test(NAME gui_forms_form_compiler_refusal_tests
            COMMAND gui_forms_form_compiler
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/form_language/generated/refused_dialog.form
                --class RefusedForm --check --output refused_dialog_form.hpp)
        set_tests_properties(gui_forms_form_compiler_refusal_tests PROPERTIES
            PASS_REGULAR_EXPRESSION "E812 form_generation_refused: refused-ok")
        if(GUI_FORMS_BUILD_GENERATED_FORM_TESTS)
            # gui_forms_add_form end to end: a header generated at build time,
            # naming an application type.
            add_executable(gui_forms_form_application_type_tests
                tests/form_language/form_application_type_tests.cpp tools/form_compiler/form_catalog.cpp
                tools/form_compiler/form_core_catalog.cpp)
            target_link_libraries(gui_forms_form_application_type_tests PRIVATE gui_forms_controls)
            target_include_directories(gui_forms_form_application_type_tests PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/form_language)
            gui_forms_add_form(gui_forms_form_application_type_tests tests/form_language/generated/note_dialog.form
                CLASS NoteDialogForm NAMESPACE form_generated
                TYPES "note_card,form_generated::NoteCard,user_control,generated/note_card.hpp")
            add_test(NAME gui_forms_form_application_type_tests COMMAND gui_forms_form_application_type_tests)
            # Stage B: the classes deriving from the root's type.
            add_executable(gui_forms_form_derived_class_tests tests/form_language/form_derived_class_tests.cpp)
            target_link_libraries(gui_forms_form_derived_class_tests PRIVATE gui_forms_controls)
            target_include_directories(gui_forms_form_derived_class_tests PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/form_language)
            gui_forms_add_form(gui_forms_form_derived_class_tests tests/form_language/generated/sample_dialog.form
                CLASS SampleDialogWindow NAMESPACE form_generated OUTPUT gui_forms_forms/sample_dialog_window.hpp
                MODEL form_generated::SampleModel MODEL_HEADER generated/sample_model.hpp)
            gui_forms_add_form(gui_forms_form_derived_class_tests tests/form_language/generated/derived_panel.form
                CLASS DerivedPanel NAMESPACE form_generated
                MODEL form_generated::SampleModel MODEL_HEADER generated/sample_model.hpp)
            gui_forms_add_form(gui_forms_form_derived_class_tests tests/form_language/generated/plain_panel.form
                CLASS PlainPanel NAMESPACE form_generated DERIVED)
            # A final root type (a layout panel), composed instead of derived.
            gui_forms_add_form(gui_forms_form_derived_class_tests tests/form_language/generated/flex_root_panel.form
                CLASS FlexRootPanel NAMESPACE form_generated
                MODEL form_generated::SampleModel MODEL_HEADER generated/sample_model.hpp)
            add_test(NAME gui_forms_form_derived_class_tests COMMAND gui_forms_form_derived_class_tests)
        endif()
    endif()

    add_executable(gui_forms_theme_selector_tests tests/theme_language/theme_selector_tests.cpp)
    target_link_libraries(gui_forms_theme_selector_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_selector_tests COMMAND gui_forms_theme_selector_tests)
    add_executable(gui_forms_theme_value_tests tests/theme_language/theme_value_tests.cpp)
    target_link_libraries(gui_forms_theme_value_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_value_tests COMMAND gui_forms_theme_value_tests)

    add_executable(gui_forms_theme_compiler_tests tests/theme_language/theme_compiler_tests.cpp)
    target_link_libraries(gui_forms_theme_compiler_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_compiler_tests COMMAND gui_forms_theme_compiler_tests)
    add_executable(gui_forms_theme_conformance_tests tests/theme_language/theme_conformance_tests.cpp)
    target_link_libraries(gui_forms_theme_conformance_tests PRIVATE gui_forms_controls)
    target_compile_definitions(gui_forms_theme_conformance_tests PRIVATE
        GUI_FORMS_THEME_CONFORMANCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/theme_language/conformance")
    add_test(NAME gui_forms_theme_conformance_tests COMMAND gui_forms_theme_conformance_tests)
    add_executable(gui_forms_theme_hostile_tests tests/theme_language/theme_hostile_tests.cpp)
    target_link_libraries(gui_forms_theme_hostile_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_hostile_tests COMMAND gui_forms_theme_hostile_tests)
    add_executable(gui_forms_theme_material_css_tests tests/theme_language/theme_material_css_tests.cpp)
    target_link_libraries(gui_forms_theme_material_css_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_material_css_tests COMMAND gui_forms_theme_material_css_tests)
    add_executable(gui_forms_theme_motion_css_tests tests/theme_language/theme_motion_css_tests.cpp)
    target_link_libraries(gui_forms_theme_motion_css_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_motion_css_tests COMMAND gui_forms_theme_motion_css_tests)
    add_executable(gui_forms_theme_animation_css_tests tests/theme_language/theme_animation_css_tests.cpp)
    target_link_libraries(gui_forms_theme_animation_css_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_animation_css_tests COMMAND gui_forms_theme_animation_css_tests)
    add_executable(gui_forms_theme_media_css_tests tests/theme_language/theme_media_css_tests.cpp)
    target_link_libraries(gui_forms_theme_media_css_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_media_css_tests COMMAND gui_forms_theme_media_css_tests)

    add_executable(gui_forms_theme_important_css_tests tests/theme_language/theme_important_css_tests.cpp)
    target_link_libraries(gui_forms_theme_important_css_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_important_css_tests COMMAND gui_forms_theme_important_css_tests)
    add_executable(gui_forms_theme_custom_property_css_tests tests/theme_language/theme_custom_property_css_tests.cpp)
    target_link_libraries(gui_forms_theme_custom_property_css_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_custom_property_css_tests COMMAND gui_forms_theme_custom_property_css_tests)

    add_executable(gui_forms_theme_inline_style_tests tests/theme_language/theme_inline_style_tests.cpp)
    target_link_libraries(gui_forms_theme_inline_style_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_inline_style_tests COMMAND gui_forms_theme_inline_style_tests)
    add_executable(gui_forms_theme_inline_custom_property_tests
                   tests/theme_language/theme_inline_custom_property_tests.cpp)
    target_link_libraries(gui_forms_theme_inline_custom_property_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_inline_custom_property_tests COMMAND gui_forms_theme_inline_custom_property_tests)
    add_executable(gui_forms_theme_colour_role_css_tests tests/theme_language/theme_colour_role_css_tests.cpp)
    target_link_libraries(gui_forms_theme_colour_role_css_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_colour_role_css_tests COMMAND gui_forms_theme_colour_role_css_tests)
    # Early theme validation (owner request 2026-11-09): the public
    # validate_theme_packages, and the build-time check run on a valid and
    # a refused package.
    add_executable(gui_forms_theme_validation_tests tests/theme_language/theme_validation_tests.cpp)
    target_link_libraries(gui_forms_theme_validation_tests PRIVATE gui_forms_controls)
    gui_forms_add_theme(gui_forms_theme_validation_tests tests/theme_language/theme_check/valid
        FUNCTION valid_theme_package NAMESPACE gui_forms_tests)
    gui_forms_add_theme(gui_forms_theme_validation_tests tests/theme_language/theme_check/refused
        FUNCTION refused_theme_package NAMESPACE gui_forms_tests)
    add_test(NAME gui_forms_theme_validation_tests COMMAND gui_forms_theme_validation_tests)
    if(TARGET gui_forms_theme_check)
        add_test(NAME gui_forms_theme_check_accepts
            COMMAND gui_forms_theme_check "${CMAKE_CURRENT_SOURCE_DIR}/tests/theme_language/theme_check/valid")
        add_test(NAME gui_forms_theme_check_refuses
            COMMAND gui_forms_theme_check "${CMAKE_CURRENT_SOURCE_DIR}/tests/theme_language/theme_check/refused")
        set_tests_properties(gui_forms_theme_check_refuses PROPERTIES WILL_FAIL TRUE)
        add_test(NAME gui_forms_theme_check_gallery
            COMMAND gui_forms_theme_check "${CMAKE_CURRENT_SOURCE_DIR}/demo/themes/gallery")
    endif()
    add_executable(gui_forms_theme_variant_tests tests/theme_variant_tests.cpp)
    target_link_libraries(gui_forms_theme_variant_tests PRIVATE gui_forms_lab gui_forms_demo_controls)
    add_test(NAME gui_forms_theme_variant_tests COMMAND gui_forms_theme_variant_tests)
    add_executable(gui_forms_theme_sheet_package_tests tests/theme_sheet_package_tests.cpp)
    target_link_libraries(gui_forms_theme_sheet_package_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_sheet_package_tests COMMAND gui_forms_theme_sheet_package_tests)
    add_executable(gui_forms_theme_font_tests tests/theme_font_tests.cpp)
    target_link_libraries(gui_forms_theme_font_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_font_tests COMMAND gui_forms_theme_font_tests)
    add_executable(gui_forms_theme_rule_faces_tests tests/theme_rule_faces_tests.cpp)
    target_link_libraries(gui_forms_theme_rule_faces_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_rule_faces_tests COMMAND gui_forms_theme_rule_faces_tests)
    add_executable(gui_forms_theme_escape_tests tests/theme_language/theme_escape_tests.cpp)
    target_link_libraries(gui_forms_theme_escape_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_escape_tests COMMAND gui_forms_theme_escape_tests)
    add_executable(gui_forms_theme_em_font_css_tests tests/theme_language/theme_em_font_css_tests.cpp)
    target_link_libraries(gui_forms_theme_em_font_css_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_em_font_css_tests COMMAND gui_forms_theme_em_font_css_tests)
    add_executable(gui_forms_theme_manifest_tests tests/theme_language/theme_manifest_tests.cpp)
    target_link_libraries(gui_forms_theme_manifest_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_manifest_tests COMMAND gui_forms_theme_manifest_tests)
    add_executable(gui_forms_theme_package_tests tests/theme_language/theme_package_tests.cpp)
    target_link_libraries(gui_forms_theme_package_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_theme_package_tests COMMAND gui_forms_theme_package_tests)
    add_executable(gui_forms_theme_package_compile_tests tests/theme_language/theme_package_compile_tests.cpp)
    target_link_libraries(gui_forms_theme_package_compile_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_package_compile_tests COMMAND gui_forms_theme_package_compile_tests)
    # The GOST paint palette package (skeudoindustrial surfaces, phase 4).
    add_executable(gui_forms_theme_gost_palette_tests tests/theme_language/theme_gost_palette_tests.cpp)
    target_link_libraries(gui_forms_theme_gost_palette_tests PRIVATE gui_forms_controls)
    target_compile_definitions(gui_forms_theme_gost_palette_tests PRIVATE
        GUI_FORMS_GOST_PALETTE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/demo/themes/gost_paints"
        GUI_FORMS_INDUSTRIAL_THEME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/demo/themes/industrial")
    add_test(NAME gui_forms_theme_gost_palette_tests COMMAND gui_forms_theme_gost_palette_tests)
    add_executable(gui_forms_theme_user_theme_tests tests/theme_language/theme_user_theme_tests.cpp)
    target_link_libraries(gui_forms_theme_user_theme_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_user_theme_tests COMMAND gui_forms_theme_user_theme_tests)

    add_executable(gui_forms_theme_user_folder_tests tests/theme_language/theme_user_folder_tests.cpp)
    target_link_libraries(gui_forms_theme_user_folder_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_user_folder_tests COMMAND gui_forms_theme_user_folder_tests)

    # The theme sheet's tokenizer and parser, then the compiler, over the
    # theme sheet corpus; the compiler's run must reach the palette bound
    # (N113) with the corpus's palette-enumeration-bound.css.
    gui_forms_add_fuzz_test(theme_sheet SOURCE fuzz/theme_sheet_fuzz.cpp LIBRARIES gui_forms_core
        ARGUMENTS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/theme_sheet)
    gui_forms_add_fuzz_test(theme_compile SOURCE fuzz/theme_compile_fuzz.cpp LIBRARIES gui_forms_controls
        ARGUMENTS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/theme_sheet)
    # The theme manifest, form documents, saved dock layouts and the
    # clipboard image decoders (audit N115, I3).
    gui_forms_add_fuzz_test(theme_manifest SOURCE fuzz/theme_manifest_fuzz.cpp LIBRARIES gui_forms_core)
    gui_forms_add_fuzz_test(form_document SOURCE fuzz/form_document_fuzz.cpp LIBRARIES gui_forms_controls)
    gui_forms_add_fuzz_test(dock_layout SOURCE fuzz/dock_layout_fuzz.cpp LIBRARIES gui_forms_core)
    gui_forms_add_fuzz_test(clipboard_image SOURCE fuzz/clipboard_image_fuzz.cpp LIBRARIES gui_forms_core)
    # The theme a process is started with (--theme, GUI_FORMS_THEME_SHEET;
    # gui_forms/application/user_themes): in process, then programs with no
    # theme code of their own launched with each request (the C ABI's in
    # cmake/CAbi.cmake). A program's expectation and policy come from its
    # environment, so its command line holds only what a user would type.
    add_executable(gui_forms_theme_request_tests tests/theme_language/theme_request_tests.cpp)
    target_link_libraries(gui_forms_theme_request_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_request_tests COMMAND gui_forms_theme_request_tests)
    set(GUI_FORMS_THEME_REQUEST_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/tests/theme_language/theme_request")
    # gui_forms_theme_request_case(<test> PROGRAM <target> EXPECT <outcome>
    #     [POLICY refused|replaces] [SHEET_ENV <path>] [APPLICATION] [ARGS <arguments>...])
    function(gui_forms_theme_request_case test)
        cmake_parse_arguments(PARSE_ARGV 1 CASE "APPLICATION" "PROGRAM;EXPECT;POLICY;SHEET_ENV" "ARGS")
        add_test(NAME ${test} COMMAND ${CASE_PROGRAM} ${CASE_ARGS})
        set(_modification "GF_EXPECT_THEME_REQUEST=set:${CASE_EXPECT}")
        if(CASE_POLICY)
            list(APPEND _modification "GF_TEST_POLICY=set:${CASE_POLICY}")
        else()
            list(APPEND _modification "GF_TEST_POLICY=unset:")
        endif()
        if(CASE_SHEET_ENV)
            list(APPEND _modification "GUI_FORMS_THEME_SHEET=set:${CASE_SHEET_ENV}")
        else()
            list(APPEND _modification "GUI_FORMS_THEME_SHEET=unset:")
        endif()
        if(CASE_APPLICATION)
            list(APPEND _modification "GF_TEST_APPLICATION=set:1")
        else()
            list(APPEND _modification "GF_TEST_APPLICATION=unset:")
        endif()
        set_tests_properties(${test} PROPERTIES ENVIRONMENT_MODIFICATION "${_modification}")
    endfunction()
    # The cases every launched program runs, its tests named <prefix>_<case>.
    function(gui_forms_theme_request_cases prefix program)
        set(_fixtures "${GUI_FORMS_THEME_REQUEST_FIXTURES}")
        gui_forms_theme_request_case(${prefix}_none PROGRAM ${program} EXPECT none)
        gui_forms_theme_request_case(${prefix}_sheet PROGRAM ${program} EXPECT applied
            ARGS --theme "${_fixtures}/good.css")
        gui_forms_theme_request_case(${prefix}_built_in PROGRAM ${program} EXPECT built_in ARGS --theme built-in)
        gui_forms_theme_request_case(${prefix}_name PROGRAM ${program} EXPECT applied ARGS --theme harbour)
        gui_forms_theme_request_case(${prefix}_unknown_name PROGRAM ${program} EXPECT unknown_name
            ARGS --theme dusk)
        gui_forms_theme_request_case(${prefix}_bad_sheet PROGRAM ${program} EXPECT refused
            ARGS --theme "${_fixtures}/bad.css")
        gui_forms_theme_request_case(${prefix}_missing_sheet PROGRAM ${program} EXPECT unreadable
            ARGS --theme "${_fixtures}/absent.css")
        gui_forms_theme_request_case(${prefix}_policy_refused PROGRAM ${program} EXPECT ignored_by_policy
            POLICY refused ARGS --theme "${_fixtures}/good.css")
        gui_forms_theme_request_case(${prefix}_policy_replaces PROGRAM ${program} EXPECT applied
            POLICY replaces ARGS --theme "${_fixtures}/good.css")
        gui_forms_theme_request_case(${prefix}_environment PROGRAM ${program} EXPECT applied
            SHEET_ENV "${_fixtures}/good.css")
        gui_forms_theme_request_case(${prefix}_command_line_wins PROGRAM ${program} EXPECT refused
            SHEET_ENV "${_fixtures}/good.css" ARGS --theme "${_fixtures}/bad.css")
        gui_forms_theme_request_case(${prefix}_malformed PROGRAM ${program} EXPECT malformed ARGS --theme)
    endfunction()
    add_executable(gui_forms_theme_request_app tests/theme_language/theme_request_app.cpp)
    target_link_libraries(gui_forms_theme_request_app PRIVATE gui_forms_controls)
    # The user theme `harbour`, in the `themes` folder beside the program.
    add_custom_command(TARGET gui_forms_theme_request_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GUI_FORMS_THEME_REQUEST_FIXTURES}/harbour.css"
            "$<TARGET_FILE_DIR:gui_forms_theme_request_app>/themes/harbour.css")
    gui_forms_theme_request_cases(gui_forms_theme_request_app gui_forms_theme_request_app)
    # A user sheet naming an effect the built-in registry does not hold is
    # refused (E417), as a program package naming one is.
    gui_forms_theme_request_case(gui_forms_theme_request_app_misspelt_effect PROGRAM gui_forms_theme_request_app
        EXPECT refused ARGS --theme "${GUI_FORMS_THEME_REQUEST_FIXTURES}/misspelt-effect.css")

    add_executable(gui_forms_dock_layout_tests tests/dock_layout_tests.cpp)
    target_link_libraries(gui_forms_dock_layout_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_dock_layout_tests COMMAND gui_forms_dock_layout_tests)

    add_executable(gui_forms_grapheme_conformance_tests
        tests/grapheme_conformance_tests.cpp
    )
    target_include_directories(gui_forms_grapheme_conformance_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(gui_forms_grapheme_conformance_tests PRIVATE
        gui_forms_core
    )
    add_test(NAME gui_forms_grapheme_conformance_tests
             COMMAND gui_forms_grapheme_conformance_tests)

    add_executable(gui_forms_text_shaping_tests tests/text_shaping_tests.cpp)
    target_link_libraries(gui_forms_text_shaping_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_text_shaping_tests COMMAND gui_forms_text_shaping_tests)

    if(TARGET gui_forms_text_engine)
        add_executable(gui_forms_glyph_run_raster_tests tests/glyph_run_raster_tests.cpp)
        target_link_libraries(gui_forms_glyph_run_raster_tests PRIVATE gui_forms_text_engine)
        target_compile_definitions(gui_forms_glyph_run_raster_tests PRIVATE
            GUI_FORMS_TEST_RAPIDS_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids.ttf"
            GUI_FORMS_TEST_CARLITO_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Regular.ttf")
        add_test(NAME gui_forms_glyph_run_raster_tests COMMAND gui_forms_glyph_run_raster_tests)
        add_executable(gui_forms_bounded_shape_tests tests/bounded_shape_tests.cpp)
        target_link_libraries(gui_forms_bounded_shape_tests PRIVATE gui_forms_text_engine)
        add_test(NAME gui_forms_bounded_shape_tests COMMAND gui_forms_bounded_shape_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        add_executable(gui_forms_font_metrics_differential_tests tests/font_metrics_differential_tests.cpp)
        target_link_libraries(gui_forms_font_metrics_differential_tests PRIVATE gui_forms_font_metrics_differential)
        add_test(NAME gui_forms_font_metrics_differential_tests COMMAND gui_forms_font_metrics_differential_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/font_metrics_differential/corpus/forms_latin_v1.txt")
        # Stage F's metric gate (step F4): bundled candidates against the
        # committed reference snapshots, on every host.
        add_executable(gui_forms_font_metrics_gate_tests tests/font_metrics_gate_tests.cpp)
        target_link_libraries(gui_forms_font_metrics_gate_tests PRIVATE
            gui_forms_font_metrics_differential gui_drawing_core)
        add_test(NAME gui_forms_font_metrics_gate_tests COMMAND gui_forms_font_metrics_gate_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/font_metrics_differential/corpus/forms_latin_v1.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/font_metrics_differential/reference_snapshots")

        add_executable(gui_forms_bounded_shape_workspace_tests tests/bounded_shape_workspace_tests.cpp)
        target_link_libraries(gui_forms_bounded_shape_workspace_tests PRIVATE gui_forms_text_engine)
        add_test(NAME gui_forms_bounded_shape_workspace_tests COMMAND gui_forms_bounded_shape_workspace_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")

        add_executable(gui_forms_prepared_window_shape_bench tests/prepared_window_shape_bench.cpp)
        target_link_libraries(gui_forms_prepared_window_shape_bench PRIVATE gui_forms_text_engine)
        add_test(NAME gui_forms_prepared_window_shape_preview_verify
            COMMAND gui_forms_prepared_window_shape_bench --fonts "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts" --scene preview-seven --verify-only)
        add_test(NAME gui_forms_prepared_window_shape_rows_verify
            COMMAND gui_forms_prepared_window_shape_bench --fonts "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts" --scene window-512 --verify-only)

        # The bundled fallback faces' compiled coverage tables against FreeType (item 4, step 5).
        add_executable(gui_forms_bundled_font_coverage_tests tests/bundled_font_coverage_tests.cpp)
        target_link_libraries(gui_forms_bundled_font_coverage_tests PRIVATE gui_forms_core stx_text::freetype)
        target_compile_definitions(gui_forms_bundled_font_coverage_tests PRIVATE
            GUI_FORMS_TEST_FONT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        add_test(NAME gui_forms_bundled_font_coverage_tests COMMAND gui_forms_bundled_font_coverage_tests)
        add_executable(gui_forms_harfbuzz_font_engine_tests
            tests/harfbuzz_font_engine_tests.cpp
        )
        target_compile_definitions(gui_forms_harfbuzz_font_engine_tests PRIVATE
            GUI_FORMS_TEST_RAPIDS_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids.ttf"
            GUI_FORMS_TEST_RAPIDS_BOLD="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids-Bold.ttf"
            GUI_FORMS_TEST_CARLITO_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Regular.ttf"
            GUI_FORMS_TEST_CARLITO_BOLD="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Bold.ttf"
            GUI_FORMS_TEST_COUSINE_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Cousine-Regular.ttf"
            GUI_FORMS_TEST_NOTO_ARABIC_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansArabic-Regular.ttf"
            GUI_FORMS_TEST_NOTO_HEBREW_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansHebrew-Regular.ttf"
            GUI_FORMS_TEST_NOTO_CJK_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansCJKjp-Regular.otf"
            GUI_FORMS_TEST_NOTO_EMOJI="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoEmoji-Regular.ttf"
            GUI_FORMS_TEST_SELAWIK_REGULAR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Selawik-Regular.ttf"
            GUI_FORMS_TEST_SELAWIK_BOLD="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Selawik-Bold.ttf"
        )
        target_link_libraries(gui_forms_harfbuzz_font_engine_tests PRIVATE
            gui_forms_text_engine)
        add_test(NAME gui_forms_harfbuzz_font_engine_tests
                 COMMAND gui_forms_harfbuzz_font_engine_tests)
    endif()

    add_executable(gui_forms_basic_controls_tests tests/basic_controls_tests.cpp)
    target_link_libraries(gui_forms_basic_controls_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_basic_controls_tests COMMAND gui_forms_basic_controls_tests)

    add_executable(gui_forms_connected_controls_tests
        tests/connected_controls_tests.cpp)
    target_link_libraries(gui_forms_connected_controls_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_connected_controls_tests
             COMMAND gui_forms_connected_controls_tests)

    add_executable(gui_forms_scrollable_control_tests
        tests/scrollable_control_tests.cpp)
    target_link_libraries(gui_forms_scrollable_control_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_scrollable_control_tests
             COMMAND gui_forms_scrollable_control_tests)

    add_executable(gui_forms_input_controls_tests tests/input_controls_tests.cpp)
    target_link_libraries(gui_forms_input_controls_tests PRIVATE
        gui_forms_controls
        gui_forms_host_headless
    )
    add_test(NAME gui_forms_input_controls_tests COMMAND gui_forms_input_controls_tests)

    add_executable(gui_forms_multiline_text_box_tests tests/multiline_text_box_tests.cpp)
    target_link_libraries(gui_forms_multiline_text_box_tests PRIVATE
        gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_multiline_text_box_tests COMMAND gui_forms_multiline_text_box_tests)

    add_executable(gui_forms_text_decorations_tests tests/text_decorations_tests.cpp)
    target_link_libraries(gui_forms_text_decorations_tests PRIVATE
        gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_text_decorations_tests COMMAND gui_forms_text_decorations_tests)

    add_executable(gui_forms_semantic_text_tests tests/semantic_text_tests.cpp)
    target_link_libraries(gui_forms_semantic_text_tests PRIVATE
        gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_semantic_text_tests COMMAND gui_forms_semantic_text_tests)

    add_executable(gui_forms_inspection_controls_tests
        tests/inspection_controls_tests.cpp)
    target_link_libraries(gui_forms_inspection_controls_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_inspection_controls_tests
             COMMAND gui_forms_inspection_controls_tests)

    add_executable(gui_forms_visual_inspection_tests
        tests/visual_inspection_tests.cpp)
    target_link_libraries(gui_forms_visual_inspection_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_visual_inspection_tests
             COMMAND gui_forms_visual_inspection_tests)

    add_executable(gui_forms_instrument_controls_tests
        tests/instrument_controls_tests.cpp)
    target_link_libraries(gui_forms_instrument_controls_tests PRIVATE
        gui_forms_lab)
    add_test(NAME gui_forms_instrument_controls_tests
             COMMAND gui_forms_instrument_controls_tests)

    add_executable(gui_forms_collection_cell_tests tests/collection_cell_tests.cpp)
    target_link_libraries(gui_forms_collection_cell_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_collection_cell_tests COMMAND gui_forms_collection_cell_tests)

    add_executable(gui_forms_checked_list_box_tests
        tests/checked_list_box_tests.cpp
    )
    target_link_libraries(gui_forms_checked_list_box_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_checked_list_box_tests
             COMMAND gui_forms_checked_list_box_tests)

    add_executable(gui_forms_collection_controls_tests
        tests/collection_controls_tests.cpp)
    target_link_libraries(gui_forms_collection_controls_tests PRIVATE
        gui_forms_lab)
    add_test(NAME gui_forms_collection_controls_tests
             COMMAND gui_forms_collection_controls_tests)

    add_executable(gui_forms_object_details_failure_tests
        tests/object_details_failure_tests.cpp)
    target_link_libraries(gui_forms_object_details_failure_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_object_details_failure_tests
             COMMAND gui_forms_object_details_failure_tests)

    add_executable(gui_forms_menu_controls_tests
        tests/menu_controls_tests.cpp)
    target_link_libraries(gui_forms_menu_controls_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_menu_controls_tests
             COMMAND gui_forms_menu_controls_tests)

    add_executable(gui_forms_overlay_controls_tests
        tests/overlay_controls_tests.cpp)
    target_link_libraries(gui_forms_overlay_controls_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_overlay_controls_tests
             COMMAND gui_forms_overlay_controls_tests)

    add_executable(gui_forms_range_controls_tests tests/range_controls_tests.cpp)
    target_link_libraries(gui_forms_range_controls_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_range_controls_tests COMMAND gui_forms_range_controls_tests)

    add_executable(gui_forms_domain_up_down_tests tests/domain_up_down_tests.cpp)
    target_link_libraries(gui_forms_domain_up_down_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_domain_up_down_tests COMMAND gui_forms_domain_up_down_tests)
    add_executable(gui_forms_editor_composite_capabilities_tests tests/editor_composite_capabilities_tests.cpp)
    target_link_libraries(gui_forms_editor_composite_capabilities_tests PRIVATE gui_forms_lab)
    add_test(NAME gui_forms_editor_composite_capabilities_tests COMMAND gui_forms_editor_composite_capabilities_tests)
    add_executable(gui_forms_item_view_capabilities_tests tests/item_view_capabilities_tests.cpp)
    target_link_libraries(gui_forms_item_view_capabilities_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_item_view_capabilities_tests COMMAND gui_forms_item_view_capabilities_tests)
    add_executable(gui_forms_picker_capabilities_tests tests/picker_capabilities_tests.cpp)
    target_link_libraries(gui_forms_picker_capabilities_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_picker_capabilities_tests COMMAND gui_forms_picker_capabilities_tests)
    add_executable(gui_forms_component_capabilities_tests tests/component_capabilities_tests.cpp)
    target_link_libraries(gui_forms_component_capabilities_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_component_capabilities_tests COMMAND gui_forms_component_capabilities_tests)
    add_executable(gui_forms_display_controls_tests tests/display_controls_tests.cpp)
    target_link_libraries(gui_forms_display_controls_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_display_controls_tests COMMAND gui_forms_display_controls_tests)
    add_executable(gui_forms_final_controls_tests tests/final_controls_tests.cpp)
    target_link_libraries(gui_forms_final_controls_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_final_controls_tests COMMAND gui_forms_final_controls_tests)
    add_executable(gui_forms_auto_complete_tests tests/auto_complete_tests.cpp)
    target_link_libraries(gui_forms_auto_complete_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_auto_complete_tests COMMAND gui_forms_auto_complete_tests)
    add_executable(gui_forms_combo_box_filter_tests tests/combo_box_filter_tests.cpp)
    target_link_libraries(gui_forms_combo_box_filter_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_combo_box_filter_tests COMMAND gui_forms_combo_box_filter_tests)
    add_executable(gui_forms_combo_box_paint_tests tests/combo_box_paint_tests.cpp)
    target_link_libraries(gui_forms_combo_box_paint_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_combo_box_paint_tests COMMAND gui_forms_combo_box_paint_tests)
    add_executable(gui_forms_token_box_tests tests/token_box_tests.cpp)
    target_link_libraries(gui_forms_token_box_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_token_box_tests COMMAND gui_forms_token_box_tests)
    add_executable(gui_forms_text_box_escape_tests tests/text_box_escape_tests.cpp)
    target_link_libraries(gui_forms_text_box_escape_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_text_box_escape_tests COMMAND gui_forms_text_box_escape_tests)
    add_executable(gui_forms_text_box_enter_tests tests/text_box_enter_tests.cpp)
    target_link_libraries(gui_forms_text_box_enter_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_text_box_enter_tests COMMAND gui_forms_text_box_enter_tests)

    add_executable(gui_forms_tool_strip_tests tests/tool_strip_tests.cpp)
    target_link_libraries(gui_forms_tool_strip_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_tool_strip_tests COMMAND gui_forms_tool_strip_tests)
    add_executable(gui_forms_strip_merge_tests tests/strip_merge_tests.cpp)
    target_link_libraries(gui_forms_strip_merge_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_strip_merge_tests COMMAND gui_forms_strip_merge_tests)
    add_executable(gui_forms_sweep_scope_tests tests/sweep_scope_tests.cpp)
    target_link_libraries(gui_forms_sweep_scope_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_sweep_scope_tests COMMAND gui_forms_sweep_scope_tests)

    # The node canvas's look, theme side (cp31): anatomy, recipes in light,
    # dark and high contrast, grid variants, the texture clamp, the paint
    # commands and the link_flow effect.
    add_executable(gui_forms_canvas_look_tests tests/canvas_look_tests.cpp)
    target_link_libraries(gui_forms_canvas_look_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_canvas_look_tests COMMAND gui_forms_canvas_look_tests)
    add_executable(gui_forms_plot_view_tests tests/plot_view_tests.cpp)
    target_link_libraries(gui_forms_plot_view_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_plot_view_tests COMMAND gui_forms_plot_view_tests)
    add_executable(gui_forms_event_payload_lifetime_tests tests/event_payload_lifetime_tests.cpp)
    target_link_libraries(gui_forms_event_payload_lifetime_tests PRIVATE gui_forms_lab)
    add_test(NAME gui_forms_event_payload_lifetime_tests COMMAND gui_forms_event_payload_lifetime_tests)
    add_executable(gui_forms_page_strip_tests tests/page_strip_tests.cpp)
    target_link_libraries(gui_forms_page_strip_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_page_strip_tests COMMAND gui_forms_page_strip_tests)
    add_executable(gui_forms_page_strip_side_tests tests/page_strip_side_tests.cpp)
    target_link_libraries(gui_forms_page_strip_side_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_page_strip_side_tests COMMAND gui_forms_page_strip_side_tests)
    add_executable(gui_forms_page_strip_drag_tests tests/page_strip_drag_tests.cpp)
    target_link_libraries(gui_forms_page_strip_drag_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_page_strip_drag_tests COMMAND gui_forms_page_strip_drag_tests)
    add_executable(gui_forms_page_view_tests tests/page_view_tests.cpp)
    target_link_libraries(gui_forms_page_view_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_page_view_tests COMMAND gui_forms_page_view_tests)
    add_executable(gui_forms_application_instance_tests tests/application_instance_tests.cpp)
    target_link_libraries(gui_forms_application_instance_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_application_instance_tests COMMAND gui_forms_application_instance_tests)
    add_executable(gui_forms_mouse_events_tests tests/mouse_events_tests.cpp)
    target_link_libraries(gui_forms_mouse_events_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_mouse_events_tests COMMAND gui_forms_mouse_events_tests)
    add_executable(gui_forms_key_events_tests tests/key_events_tests.cpp)
    target_link_libraries(gui_forms_key_events_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_key_events_tests COMMAND gui_forms_key_events_tests)
    add_executable(gui_forms_mouse_event_order_tests tests/mouse_event_order_tests.cpp)
    target_link_libraries(gui_forms_mouse_event_order_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_mouse_event_order_tests COMMAND gui_forms_mouse_event_order_tests)
    add_executable(gui_forms_control_raiser_tests tests/control_raiser_tests.cpp)
    target_link_libraries(gui_forms_control_raiser_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_control_raiser_tests COMMAND gui_forms_control_raiser_tests)
    add_executable(gui_forms_key_commands_tests tests/key_commands_tests.cpp)
    target_link_libraries(gui_forms_key_commands_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_key_commands_tests COMMAND gui_forms_key_commands_tests)
    # The X11 host's XSETTINGS parser takes plain bytes and no Xlib, so it is
    # tested on every platform.
    add_executable(gui_forms_xsettings_parse_tests
        tests/xsettings_parse_tests.cpp src/host/linux/settings/xsettings_parse.cpp)
    target_include_directories(gui_forms_xsettings_parse_tests PRIVATE include)
    # The public headers it includes refer to the checked-borrows link
    # marker in a checked build, which only the core library defines.
    target_link_libraries(gui_forms_xsettings_parse_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_xsettings_parse_tests COMMAND gui_forms_xsettings_parse_tests)
    # The X11 host's computed click counts and context keys take plain
    # values and no Xlib, so they are tested on every platform.
    add_executable(gui_forms_linux_input_rules_tests
        tests/linux_input_rules_tests.cpp src/host/linux/application/linux_input_rules.cpp)
    target_include_directories(gui_forms_linux_input_rules_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    # The public headers it includes refer to the checked-borrows link
    # marker in a checked build, which only the core library defines.
    target_link_libraries(gui_forms_linux_input_rules_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_linux_input_rules_tests COMMAND gui_forms_linux_input_rules_tests)
    add_executable(gui_forms_page_view_document_tests tests/page_view_document_tests.cpp)
    target_link_libraries(gui_forms_page_view_document_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_page_view_document_tests COMMAND gui_forms_page_view_document_tests)
    add_executable(gui_forms_navigation_pane_tests tests/navigation_pane_tests.cpp)
    target_link_libraries(gui_forms_navigation_pane_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_navigation_pane_tests COMMAND gui_forms_navigation_pane_tests)
    add_executable(gui_forms_dock_site_tests tests/dock_site_tests.cpp)
    target_link_libraries(gui_forms_dock_site_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_dock_site_tests COMMAND gui_forms_dock_site_tests)
    add_executable(gui_forms_dock_auto_hide_tests tests/dock_auto_hide_tests.cpp)
    target_link_libraries(gui_forms_dock_auto_hide_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_dock_auto_hide_tests COMMAND gui_forms_dock_auto_hide_tests)
    add_executable(gui_forms_tree_list_view_tests tests/tree_list_view_tests.cpp)
    target_link_libraries(gui_forms_tree_list_view_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_tree_list_view_tests COMMAND gui_forms_tree_list_view_tests)
    add_executable(gui_forms_number_text_tests tests/number_text_tests.cpp)
    target_link_libraries(gui_forms_number_text_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_number_text_tests COMMAND gui_forms_number_text_tests)

    add_executable(gui_forms_row_height_index_tests tests/row_height_index_tests.cpp)
    target_link_libraries(gui_forms_row_height_index_tests PRIVATE gui_forms_controls)
    target_include_directories(gui_forms_row_height_index_tests SYSTEM PRIVATE "${GUI_FORMS_STX_BTREE_INCLUDE_DIR}")
    add_test(NAME gui_forms_row_height_index_tests COMMAND gui_forms_row_height_index_tests)

    add_executable(gui_forms_virtual_rows_view_tests tests/virtual_rows_view_tests.cpp)
    target_link_libraries(gui_forms_virtual_rows_view_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_virtual_rows_view_tests COMMAND gui_forms_virtual_rows_view_tests)

    add_executable(gui_forms_list_view_tests tests/list_view_tests.cpp)
    target_link_libraries(gui_forms_list_view_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_list_view_tests COMMAND gui_forms_list_view_tests)

    add_executable(gui_forms_kept_layout_tests tests/kept_layout_tests.cpp)
    target_link_libraries(gui_forms_kept_layout_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_kept_layout_tests COMMAND gui_forms_kept_layout_tests)

    add_executable(gui_forms_data_grid_view_tests tests/data_grid_view_tests.cpp)
    target_link_libraries(gui_forms_data_grid_view_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_data_grid_view_tests COMMAND gui_forms_data_grid_view_tests)
    add_executable(gui_forms_data_grid_view_order_tests tests/data_grid_view_order_tests.cpp)
    target_link_libraries(gui_forms_data_grid_view_order_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_data_grid_view_order_tests COMMAND gui_forms_data_grid_view_order_tests)

    add_executable(gui_forms_masked_text_box_tests tests/masked_text_box_tests.cpp)
    target_link_libraries(gui_forms_masked_text_box_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_masked_text_box_tests COMMAND gui_forms_masked_text_box_tests)
    add_executable(gui_forms_rich_text_box_tests tests/rich_text_box_tests.cpp)
    target_link_libraries(gui_forms_rich_text_box_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_rich_text_box_tests COMMAND gui_forms_rich_text_box_tests)

    add_executable(gui_forms_month_calendar_tests tests/month_calendar_tests.cpp)
    target_link_libraries(gui_forms_month_calendar_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_month_calendar_tests COMMAND gui_forms_month_calendar_tests)

    add_executable(gui_forms_ribbon_gallery_tests tests/ribbon_gallery_tests.cpp)
    target_link_libraries(gui_forms_ribbon_gallery_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_gallery_tests COMMAND gui_forms_ribbon_gallery_tests)
    add_executable(gui_forms_quick_access_toolbar_tests tests/quick_access_toolbar_tests.cpp)
    target_link_libraries(gui_forms_quick_access_toolbar_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_quick_access_toolbar_tests COMMAND gui_forms_quick_access_toolbar_tests)
    add_executable(gui_forms_ribbon_tool_tip_tests tests/ribbon_tool_tip_tests.cpp)
    target_link_libraries(gui_forms_ribbon_tool_tip_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_tool_tip_tests COMMAND gui_forms_ribbon_tool_tip_tests)
    add_executable(gui_forms_ribbon_look_tests tests/ribbon_look_tests.cpp)
    target_link_libraries(gui_forms_ribbon_look_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_look_tests COMMAND gui_forms_ribbon_look_tests)
    add_executable(gui_forms_overlay_shadow_tests tests/overlay_shadow_tests.cpp)
    target_link_libraries(gui_forms_overlay_shadow_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_overlay_shadow_tests COMMAND gui_forms_overlay_shadow_tests)
    add_executable(gui_forms_ribbon_state_tests tests/ribbon_state_tests.cpp)
    target_link_libraries(gui_forms_ribbon_state_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_state_tests COMMAND gui_forms_ribbon_state_tests)
    add_executable(gui_forms_focus_region_tests tests/focus_region_tests.cpp)
    target_link_libraries(gui_forms_focus_region_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_focus_region_tests COMMAND gui_forms_focus_region_tests)
    add_executable(gui_forms_ribbon_vocabulary_tests tests/ribbon_vocabulary_tests.cpp)
    target_link_libraries(gui_forms_ribbon_vocabulary_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_vocabulary_tests COMMAND gui_forms_ribbon_vocabulary_tests)
    add_executable(gui_forms_control_snapshot_tests tests/control_snapshot_tests.cpp)
    target_include_directories(gui_forms_control_snapshot_tests PRIVATE tests)
    target_link_libraries(gui_forms_control_snapshot_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_control_snapshot_tests COMMAND gui_forms_control_snapshot_tests)
    add_executable(gui_forms_dock_drag_tests tests/dock_drag_tests.cpp)
    target_include_directories(gui_forms_dock_drag_tests PRIVATE tests)
    target_link_libraries(gui_forms_dock_drag_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_dock_drag_tests COMMAND gui_forms_dock_drag_tests)
    add_executable(gui_forms_docking_vocabulary_tests tests/docking_vocabulary_tests.cpp)
    target_link_libraries(gui_forms_docking_vocabulary_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_docking_vocabulary_tests COMMAND gui_forms_docking_vocabulary_tests)
    add_executable(gui_forms_rules_tests tests/rules_tests.cpp)
    target_link_libraries(gui_forms_rules_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_rules_tests COMMAND gui_forms_rules_tests)
    add_executable(gui_forms_command_route_tests tests/command_route_tests.cpp)
    target_link_libraries(gui_forms_command_route_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_command_route_tests COMMAND gui_forms_command_route_tests)
    add_executable(gui_forms_ribbon_tests tests/ribbon_tests.cpp)
    target_link_libraries(gui_forms_ribbon_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_tests COMMAND gui_forms_ribbon_tests)
    add_executable(gui_forms_ribbon_scaling_tests tests/ribbon_scaling_tests.cpp)
    target_link_libraries(gui_forms_ribbon_scaling_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_scaling_tests COMMAND gui_forms_ribbon_scaling_tests)
    add_executable(gui_forms_ribbon_contextual_tests tests/ribbon_contextual_tests.cpp)
    target_link_libraries(gui_forms_ribbon_contextual_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_contextual_tests COMMAND gui_forms_ribbon_contextual_tests)
    add_executable(gui_forms_ribbon_file_area_tests tests/ribbon_file_area_tests.cpp)
    target_link_libraries(gui_forms_ribbon_file_area_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_ribbon_file_area_tests COMMAND gui_forms_ribbon_file_area_tests)
    add_executable(gui_forms_tool_strip_container_tests tests/tool_strip_container_tests.cpp)
    target_link_libraries(gui_forms_tool_strip_container_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_tool_strip_container_tests COMMAND gui_forms_tool_strip_container_tests)
    add_executable(gui_forms_binding_navigator_tests tests/binding_navigator_tests.cpp)
    target_link_libraries(gui_forms_binding_navigator_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_binding_navigator_tests COMMAND gui_forms_binding_navigator_tests)

    add_executable(gui_forms_help_system_tests tests/help_system_tests.cpp)
    target_link_libraries(gui_forms_help_system_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_help_system_tests COMMAND gui_forms_help_system_tests)
    add_executable(gui_forms_font_dialog_tests tests/font_dialog_tests.cpp)
    target_link_libraries(gui_forms_font_dialog_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_font_dialog_tests COMMAND gui_forms_font_dialog_tests)

    add_executable(gui_forms_background_worker_tests tests/background_worker_tests.cpp)
    target_link_libraries(gui_forms_background_worker_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_background_worker_tests COMMAND gui_forms_background_worker_tests)

    add_executable(gui_forms_task_dialog_tests tests/task_dialog_tests.cpp)
    target_link_libraries(gui_forms_task_dialog_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_task_dialog_tests COMMAND gui_forms_task_dialog_tests)
    add_executable(gui_forms_thread_exception_dialog_tests tests/thread_exception_dialog_tests.cpp)
    target_link_libraries(gui_forms_thread_exception_dialog_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_thread_exception_dialog_tests COMMAND gui_forms_thread_exception_dialog_tests)
    set_tests_properties(gui_forms_background_worker_tests PROPERTIES TIMEOUT 60)

    add_executable(gui_forms_command_chain_tests tests/command_chain_tests.cpp)
    target_link_libraries(gui_forms_command_chain_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_command_chain_tests COMMAND gui_forms_command_chain_tests)
    add_executable(gui_forms_notify_icon_tests tests/notify_icon_tests.cpp)
    target_link_libraries(gui_forms_notify_icon_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_notify_icon_tests COMMAND gui_forms_notify_icon_tests)
    add_executable(gui_forms_open_file_dialog_tests tests/open_file_dialog_tests.cpp)
    target_link_libraries(gui_forms_open_file_dialog_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_open_file_dialog_tests COMMAND gui_forms_open_file_dialog_tests)
    add_executable(gui_forms_save_file_dialog_tests tests/save_file_dialog_tests.cpp)
    target_link_libraries(gui_forms_save_file_dialog_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_save_file_dialog_tests COMMAND gui_forms_save_file_dialog_tests)
    add_executable(gui_forms_color_dialog_tests tests/color_dialog_tests.cpp)
    target_link_libraries(gui_forms_color_dialog_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_color_dialog_tests COMMAND gui_forms_color_dialog_tests)
    add_executable(gui_forms_folder_browser_dialog_tests tests/folder_browser_dialog_tests.cpp)
    target_link_libraries(gui_forms_folder_browser_dialog_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_folder_browser_dialog_tests COMMAND gui_forms_folder_browser_dialog_tests)
    add_executable(gui_forms_message_box_tests tests/message_box_tests.cpp)
    target_link_libraries(gui_forms_message_box_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_message_box_tests COMMAND gui_forms_message_box_tests)

    add_executable(gui_forms_anatomy_tests tests/anatomy_tests.cpp)
    target_link_libraries(gui_forms_anatomy_tests PRIVATE gui_forms_lab)
    add_test(NAME gui_forms_anatomy_tests COMMAND gui_forms_anatomy_tests)

    add_executable(gui_forms_material_tests tests/material_tests.cpp)
    target_link_libraries(gui_forms_material_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_material_tests COMMAND gui_forms_material_tests)

    add_executable(gui_forms_theme_tests tests/theme_tests.cpp)
    target_link_libraries(gui_forms_theme_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_theme_tests COMMAND gui_forms_theme_tests)

    add_executable(gui_forms_composition_controls_tests
        tests/composition_controls_tests.cpp
    )
    target_link_libraries(gui_forms_composition_controls_tests
        PRIVATE gui_forms_lab
    )
    add_test(NAME gui_forms_composition_controls_tests
        COMMAND gui_forms_composition_controls_tests)

    add_executable(gui_forms_split_container_tests
        tests/split_container_tests.cpp
    )
    target_link_libraries(gui_forms_split_container_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_split_container_tests
             COMMAND gui_forms_split_container_tests)
    add_executable(gui_forms_splitter_tests tests/splitter_tests.cpp)
    target_link_libraries(gui_forms_splitter_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_splitter_tests COMMAND gui_forms_splitter_tests)
    add_executable(gui_forms_node_graph_tests tests/node_graph_tests.cpp)
    target_link_libraries(gui_forms_node_graph_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_node_graph_tests COMMAND gui_forms_node_graph_tests)
    add_executable(gui_forms_node_graph_history_tests tests/node_graph_history_tests.cpp)
    target_link_libraries(gui_forms_node_graph_history_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_node_graph_history_tests COMMAND gui_forms_node_graph_history_tests)
    add_executable(gui_forms_link_router_tests tests/link_router_tests.cpp)
    target_link_libraries(gui_forms_link_router_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_link_router_tests COMMAND gui_forms_link_router_tests)
    add_executable(gui_forms_drag_engine_tests tests/drag_engine_tests.cpp)
    target_link_libraries(gui_forms_drag_engine_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_drag_engine_tests COMMAND gui_forms_drag_engine_tests)
    add_executable(gui_forms_drag_tracker_tests tests/drag_tracker_tests.cpp)
    target_link_libraries(gui_forms_drag_tracker_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_drag_tracker_tests COMMAND gui_forms_drag_tracker_tests)
    add_executable(gui_forms_scrub_gesture_tests tests/scrub_gesture_tests.cpp)
    target_link_libraries(gui_forms_scrub_gesture_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_scrub_gesture_tests COMMAND gui_forms_scrub_gesture_tests)
    add_executable(gui_forms_value_text_tests tests/value_text_tests.cpp)
    target_link_libraries(gui_forms_value_text_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_value_text_tests COMMAND gui_forms_value_text_tests)
    add_executable(gui_forms_value_bar_tests tests/value_bar_tests.cpp)
    target_link_libraries(gui_forms_value_bar_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_value_bar_tests COMMAND gui_forms_value_bar_tests)

    add_executable(gui_forms_layout_contract_tests
        tests/layout_contract_tests.cpp
    )
    target_link_libraries(gui_forms_layout_contract_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_layout_contract_tests
             COMMAND gui_forms_layout_contract_tests)

    # Stage L, phase L2: the layout style reader, against its conformance
    # table and the layout corpus's style attributes.
    add_executable(gui_forms_layout_style_tests
        tests/layout_style_tests.cpp
    )
    target_link_libraries(gui_forms_layout_style_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_layout_style_tests
             COMMAND gui_forms_layout_style_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/layout_corpus)

    # Stage L, phase L5: the block flow solver headless, and BlockPanel
    # with the 10,000-child counts.
    add_executable(gui_forms_block_flow_solver_tests
        tests/block_flow_solver_tests.cpp
    )
    target_link_libraries(gui_forms_block_flow_solver_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_block_flow_solver_tests
             COMMAND gui_forms_block_flow_solver_tests)

    add_executable(gui_forms_block_panel_tests
        tests/block_panel_tests.cpp
    )
    target_link_libraries(gui_forms_block_panel_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_block_panel_tests
             COMMAND gui_forms_block_panel_tests)

    # Stage L, phase L8a: TextBlock, the CSS text leaf, and the baselines
    # the containers align it by.
    add_executable(gui_forms_text_block_tests
        tests/text_block_tests.cpp
    )
    target_link_libraries(gui_forms_text_block_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_text_block_tests
             COMMAND gui_forms_text_block_tests)
    if(TARGET gui_forms_text_engine)
        add_executable(gui_forms_text_block_engine_tests
            tests/text_block_engine_tests.cpp
        )
        target_link_libraries(gui_forms_text_block_engine_tests PRIVATE gui_forms_controls gui_forms_text_engine)
        add_test(NAME gui_forms_text_block_engine_tests
                 COMMAND gui_forms_text_block_engine_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
    endif()

    # Paragraph layout into caller storage (chat_stress gap A1): wrapping
    # against TextBlock's lines and the shared greedy wrapper, the storage
    # results, counted allocations, long words and emoji clusters; and the
    # same against the bundled engine.
    add_executable(gui_forms_paragraph_layout_tests
        tests/paragraph_layout_tests.cpp
    )
    target_link_libraries(gui_forms_paragraph_layout_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_paragraph_layout_tests
             COMMAND gui_forms_paragraph_layout_tests)
    if(TARGET gui_forms_text_engine)
        add_executable(gui_forms_paragraph_layout_engine_tests
            tests/paragraph_layout_engine_tests.cpp
        )
        target_link_libraries(gui_forms_paragraph_layout_engine_tests PRIVATE gui_forms_controls gui_forms_text_engine)
        add_test(NAME gui_forms_paragraph_layout_engine_tests
                 COMMAND gui_forms_paragraph_layout_engine_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
    endif()

    # Stage L, phase L5: absolute and relative positioning in the three CSS
    # containers, layout diagnostics and the containing-block dependency.
    add_executable(gui_forms_positioning_tests
        tests/positioning_tests.cpp
    )
    target_link_libraries(gui_forms_positioning_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_positioning_tests
             COMMAND gui_forms_positioning_tests)

    # Stage L, phase L4: the grid solver headless, and GridPanel with
    # the 10,000-child counts.
    add_executable(gui_forms_grid_solver_tests
        tests/grid_solver_tests.cpp
    )
    target_link_libraries(gui_forms_grid_solver_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_grid_solver_tests
             COMMAND gui_forms_grid_solver_tests)

    add_executable(gui_forms_grid_panel_tests
        tests/grid_panel_tests.cpp
    )
    target_link_libraries(gui_forms_grid_panel_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_grid_panel_tests
             COMMAND gui_forms_grid_panel_tests)

    # Stage L, phase L3: the flex solver headless, and FlexPanel with
    # the 10,000-child counts.
    add_executable(gui_forms_flex_solver_tests
        tests/flex_solver_tests.cpp
    )
    target_link_libraries(gui_forms_flex_solver_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_flex_solver_tests
             COMMAND gui_forms_flex_solver_tests)

    add_executable(gui_forms_flex_panel_tests
        tests/flex_panel_tests.cpp
    )
    target_link_libraries(gui_forms_flex_panel_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_flex_panel_tests
             COMMAND gui_forms_flex_panel_tests)

    add_executable(gui_forms_layout_panel_tests
        tests/layout_panel_tests.cpp
    )
    target_link_libraries(gui_forms_layout_panel_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_layout_panel_tests
             COMMAND gui_forms_layout_panel_tests)

    add_executable(gui_forms_window_resize_measure_tests
        tests/window_resize_measure_tests.cpp
    )
    target_link_libraries(gui_forms_window_resize_measure_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_window_resize_measure_tests
             COMMAND gui_forms_window_resize_measure_tests)

    add_executable(gui_forms_window_root_slot_tests
        tests/window_root_slot_tests.cpp
    )
    target_link_libraries(gui_forms_window_root_slot_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_window_root_slot_tests
             COMMAND gui_forms_window_root_slot_tests)

    add_executable(gui_forms_window_band_tests
        tests/window_band_tests.cpp
    )
    target_link_libraries(gui_forms_window_band_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_window_band_tests
             COMMAND gui_forms_window_band_tests)

    add_executable(gui_forms_window_chrome_tests
        tests/window_chrome_tests.cpp
    )
    target_link_libraries(gui_forms_window_chrome_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_window_chrome_tests
             COMMAND gui_forms_window_chrome_tests)

    add_executable(gui_forms_focus_restore_tests
        tests/focus_restore_tests.cpp
    )
    target_link_libraries(gui_forms_focus_restore_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_focus_restore_tests
             COMMAND gui_forms_focus_restore_tests)

    add_executable(gui_forms_layout_direction_tests
        tests/layout_direction_tests.cpp
    )
    target_link_libraries(gui_forms_layout_direction_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_layout_direction_tests
             COMMAND gui_forms_layout_direction_tests)

    add_executable(gui_forms_right_to_left_tests
        tests/right_to_left_tests.cpp
    )
    target_link_libraries(gui_forms_right_to_left_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_right_to_left_tests
             COMMAND gui_forms_right_to_left_tests)

    # The layout fixture corpus against checked-in Chromium references
    # (notes:planning/stages/L-layout/PLAN.md section 6). The corpus path on the
    # command line also selects this test when the corpus changes.
    add_executable(gui_forms_layout_corpus_tests
        tests/layout_corpus_tests.cpp
    )
    target_link_libraries(gui_forms_layout_corpus_tests PRIVATE gui_forms_controls)
    # Text fixtures (phase L8b) are laid out through the bundled text engine;
    # a build without it reports them as not run, which fails the test.
    set(GUI_FORMS_LAYOUT_CORPUS_FONTS)
    if(TARGET gui_forms_text_engine)
        target_link_libraries(gui_forms_layout_corpus_tests PRIVATE gui_forms_text_engine)
        target_compile_definitions(gui_forms_layout_corpus_tests PRIVATE GUI_FORMS_LAYOUT_CORPUS_TEXT_ENGINE=1)
        set(GUI_FORMS_LAYOUT_CORPUS_FONTS --fonts ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)
    endif()
    add_test(NAME gui_forms_layout_corpus_tests
             COMMAND gui_forms_layout_corpus_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/layout_corpus
                     ${GUI_FORMS_LAYOUT_CORPUS_FONTS})

    add_executable(gui_forms_responsive_track_panel_tests
        tests/responsive_track_panel_tests.cpp
    )
    target_link_libraries(gui_forms_responsive_track_panel_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_responsive_track_panel_tests
             COMMAND gui_forms_responsive_track_panel_tests)

    add_executable(gui_forms_command_overflow_panel_tests
        tests/command_overflow_panel_tests.cpp
    )
    target_link_libraries(gui_forms_command_overflow_panel_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_command_overflow_panel_tests
             COMMAND gui_forms_command_overflow_panel_tests)

    add_executable(gui_forms_tab_control_tests
        tests/tab_control_tests.cpp
    )
    target_link_libraries(gui_forms_tab_control_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_tab_control_tests
             COMMAND gui_forms_tab_control_tests)
    add_executable(gui_forms_tab_control_strip_tests tests/tab_control_strip_tests.cpp)
    target_link_libraries(gui_forms_tab_control_strip_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_tab_control_strip_tests COMMAND gui_forms_tab_control_strip_tests)

    add_executable(gui_forms_focus_scope_tests
        tests/focus_scope_tests.cpp
    )
    target_link_libraries(gui_forms_focus_scope_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_focus_scope_tests
             COMMAND gui_forms_focus_scope_tests)

    add_executable(gui_forms_control_depth_tests tests/control_depth_tests.cpp)
    target_link_libraries(gui_forms_control_depth_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_control_depth_tests COMMAND gui_forms_control_depth_tests)

    add_executable(gui_forms_animation_tests
        tests/animation_tests.cpp
    )
    target_link_libraries(gui_forms_animation_tests PRIVATE gui_forms_demo_controls)
    add_test(NAME gui_forms_animation_tests
             COMMAND gui_forms_animation_tests)

    add_executable(gui_forms_semantic_tests
        tests/semantic_tests.cpp
    )
    target_link_libraries(gui_forms_semantic_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_semantic_tests COMMAND gui_forms_semantic_tests)
    # The semantic halves of the UI Automation provider and text tests'
    # checks, on the same fixtures, headless (no host, no foreground).
    add_executable(gui_forms_semantic_fixture_tests tests/semantic_fixture_tests.cpp)
    target_link_libraries(gui_forms_semantic_fixture_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_semantic_fixture_tests COMMAND gui_forms_semantic_fixture_tests)

    # The localizable-literal plugin, dogfooded: a unit with the plugin, one
    # without it, a static and a shared library with it. Without the plugin
    # the test checks the table reader only.
    add_library(gui_forms_localizable_plugin_static STATIC tests/localizable_plugin_static.cpp)
    target_link_libraries(gui_forms_localizable_plugin_static PUBLIC gui_forms_controls)
    gui_forms_compile_with_localizable_plugin(gui_forms_localizable_plugin_static)
    add_library(gui_forms_localizable_plugin_shared SHARED tests/localizable_plugin_shared.cpp)
    target_include_directories(gui_forms_localizable_plugin_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    gui_forms_compile_with_localizable_plugin(gui_forms_localizable_plugin_shared)
    add_executable(gui_forms_localizable_plugin_tests
        tests/localizable_plugin_tests.cpp
        tests/localizable_plugin_plain.cpp)
    target_link_libraries(gui_forms_localizable_plugin_tests PRIVATE
        gui_forms_localizable_plugin_static gui_forms_localizable_plugin_shared gui_forms_controls)
    target_compile_definitions(gui_forms_localizable_plugin_tests PRIVATE
        GUI_FORMS_LOCALIZABLE_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(GUI_FORMS_LOCALIZABLE_PLUGIN_PATH)
        target_compile_definitions(gui_forms_localizable_plugin_tests PRIVATE GUI_FORMS_LOCALIZABLE_TEST_PLUGIN)
        gui_forms_compile_with_localizable_plugin(gui_forms_localizable_plugin_tests
            SOURCES tests/localizable_plugin_tests.cpp)
    endif()
    add_test(NAME gui_forms_localizable_plugin_tests COMMAND gui_forms_localizable_plugin_tests)
    # The dump tool reads the same tables from the linked program's file.
    get_filename_component(_gf_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(GUI_FORMS_LLVM_OBJCOPY NAMES llvm-objcopy HINTS "${_gf_compiler_dir}")
    if(GUI_FORMS_LOCALIZABLE_PLUGIN_PATH AND GUI_FORMS_LLVM_OBJCOPY AND Python3_EXECUTABLE)
        add_test(NAME gui_forms_localizable_dump_tests
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/dump_localizable_literals.py
                         --objcopy ${GUI_FORMS_LLVM_OBJCOPY} --require Ready --require "Static library text"
                         $<TARGET_FILE:gui_forms_localizable_plugin_tests>)
    endif()

    add_executable(gui_forms_lifecycle_controls_tests
        tests/lifecycle_controls_tests.cpp
    )
    target_link_libraries(gui_forms_lifecycle_controls_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_lifecycle_controls_tests
             COMMAND gui_forms_lifecycle_controls_tests)

    add_executable(gui_forms_retained_lifetime_tests
        tests/retained_lifetime_tests.cpp
    )
    target_link_libraries(gui_forms_retained_lifetime_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_retained_lifetime_tests
             COMMAND gui_forms_retained_lifetime_tests)

    add_executable(gui_forms_invalidation_damage_tests
        tests/invalidation_damage_tests.cpp
    )
    target_link_libraries(gui_forms_invalidation_damage_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_invalidation_damage_tests
             COMMAND gui_forms_invalidation_damage_tests)

    add_executable(gui_forms_display_chunk_tests
        tests/display_chunk_tests.cpp
    )
    target_link_libraries(gui_forms_display_chunk_tests PRIVATE gui_forms_controls gui_forms_core)
    add_test(NAME gui_forms_display_chunk_tests COMMAND gui_forms_display_chunk_tests)

    add_executable(gui_forms_paint_marks_tests
        tests/paint_marks_tests.cpp
    )
    target_link_libraries(gui_forms_paint_marks_tests PRIVATE gui_forms_controls gui_forms_core)
    add_test(NAME gui_forms_paint_marks_tests COMMAND gui_forms_paint_marks_tests)

    # The plots package's primitives, headless: numeric checks, golden tick
    # tables and display traces, and the envelope's pixel-identity proof.
    add_executable(gui_forms_plot_primitives_tests tests/plot_primitives_tests.cpp)
    target_link_libraries(gui_forms_plot_primitives_tests PRIVATE gui_forms_plot_primitives)
    if(GUI_FORMS_EFFECTS_VERIFIED)
        target_compile_definitions(gui_forms_plot_primitives_tests PRIVATE GUI_FORMS_EFFECT_TEST_VERIFIED)
    endif()
    add_test(NAME gui_forms_plot_primitives_tests COMMAND gui_forms_plot_primitives_tests)
    add_executable(gui_forms_plot_decimation_raster_tests tests/plot_decimation_raster_tests.cpp)
    target_link_libraries(gui_forms_plot_decimation_raster_tests PRIVATE gui_forms_plot_primitives)
    add_test(NAME gui_forms_plot_decimation_raster_tests COMMAND gui_forms_plot_decimation_raster_tests)

    add_executable(gui_forms_splash_baker_tests tests/splash_baker_tests.cpp)
    target_link_libraries(gui_forms_splash_baker_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_splash_baker_tests COMMAND gui_forms_splash_baker_tests)
    set_tests_properties(gui_forms_splash_baker_tests PROPERTIES TIMEOUT 30)
    if(TARGET gui_forms_skia)
        add_executable(gui_forms_skia_splash_bake_tests tests/skia_splash_bake_tests.cpp)
        target_link_libraries(gui_forms_skia_splash_bake_tests PRIVATE gui_forms_skia gui_forms_core)
        add_test(NAME gui_forms_skia_splash_bake_tests COMMAND gui_forms_skia_splash_bake_tests)
        set_tests_properties(gui_forms_skia_splash_bake_tests PROPERTIES TIMEOUT 30)
    endif()

    add_executable(gui_forms_frame_scheduler_tests
        tests/frame_scheduler_tests.cpp
    )
    target_link_libraries(gui_forms_frame_scheduler_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_frame_scheduler_tests COMMAND gui_forms_frame_scheduler_tests)

    # Housekeeping expectations: a missed wake and a slow paint tick each give
    # one telemetry record; a normal run gives none (owner, 2026-11-11).
    add_executable(gui_forms_housekeeping_expectation_tests tests/housekeeping_expectation_tests.cpp)
    target_link_libraries(gui_forms_housekeeping_expectation_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_housekeeping_expectation_tests COMMAND gui_forms_housekeeping_expectation_tests)

    # The fail-fast boundary: each mode must end the process with its report
    # (site and exception text) and never print "survived".
    add_executable(gui_forms_fail_fast_tests tests/fail_fast_tests.cpp)
    target_link_libraries(gui_forms_fail_fast_tests PRIVATE gui_forms_host_headless)
    foreach(_gui_fail_fast_case IN ITEMS
            "direct|test.direct|injected direct failure"
            "ui_timer|scheduler.ui_timer|injected timer failure"
            "motion_listener|motion_clock.listener|injected listener failure"
            "host_dispatch|host_session.dispatch|injected event failure")
        string(REPLACE "|" ";" _gui_fail_fast_fields "${_gui_fail_fast_case}")
        list(GET _gui_fail_fast_fields 0 _gui_fail_fast_mode)
        list(GET _gui_fail_fast_fields 1 _gui_fail_fast_site)
        list(GET _gui_fail_fast_fields 2 _gui_fail_fast_text)
        # fail_fast() aborts: SIGABRT on Linux and macOS, which CTest counts as a failure
        # whatever the output says, so the mode runs through cmake/expect_fail_fast.cmake.
        add_test(NAME gui_forms_fail_fast_${_gui_fail_fast_mode}
                 COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gui_forms_fail_fast_tests>
                         -DMODE=${_gui_fail_fast_mode}
                         "-DEXPECTED=GUI.Forms: fatal exception at ${_gui_fail_fast_site}: ${_gui_fail_fast_text}"
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/expect_fail_fast.cmake)
    endforeach()
    # The report prints the failure snapshot's shadow frames: the event frame
    # the target threw in, closed by unwinding before the boundary caught it.
    add_test(NAME gui_forms_fail_fast_shadow_frames
             COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gui_forms_fail_fast_tests> -DMODE=shadow_frames
                     "-DEXPECTED=GUI.Forms: shadow frames \\(1 open, 1 recorded\\):[^#]*#0 kind 3 fail-fast.frame click 0"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/expect_fail_fast.cmake)

    # Borrowed views (borrow sanitizer): `plain` uses fresh views and passes
    # in both builds; in the checked build each death mode must end the
    # process with its report and never print "survived". The modes cover
    # each fault on the child list, a collection read, a back link and a
    # call-scope payload (tests/borrowed_view_tests.cpp), then stage 3.
    add_executable(gui_forms_borrowed_view_tests tests/borrowed_view_tests.cpp)
    target_link_libraries(gui_forms_borrowed_view_tests PRIVATE gui_forms_controls gui_forms_core Threads::Threads)
    add_test(NAME gui_forms_borrowed_view_plain COMMAND gui_forms_borrowed_view_tests plain)
    set_tests_properties(gui_forms_borrowed_view_plain PROPERTIES PASS_REGULAR_EXPRESSION "borrowed views: ok")
    if(GUI_FORMS_CHECKED_BORROWS)
        set(_gui_borrow_stale "a borrowed view was used after a structural change of its owner")
        set(_gui_borrow_dead "a borrowed view was used after its owner was destroyed")
        set(_gui_borrow_thread "a borrowed view was used on another thread than the one it was lent on")
        set(_gui_borrow_range "a borrowed view was indexed past its end")
        set(_gui_borrow_null "a null borrowed pointer was dereferenced")
        foreach(_gui_borrow_case IN ITEMS
                "children|stale" "owner_death|dead" "children_thread|thread" "children_range|range"
                "collection|stale" "collection_death|dead" "collection_thread|thread" "collection_range|range"
                "back_link|stale" "back_link_death|dead" "back_link_thread|thread" "back_link_null|null"
                "payload_death|dead" "payload_thread|thread" "payload_range|range"
                "list_items|stale" "text_store|stale" "layout_workspace|stale" "write_lease|stale"
                "raster_death|dead" "tool_strip_items|stale"
                "inline_style|stale" "paint_text|dead")
            string(REPLACE "|" ";" _gui_borrow_fields "${_gui_borrow_case}")
            list(GET _gui_borrow_fields 0 _gui_borrow_mode)
            list(GET _gui_borrow_fields 1 _gui_borrow_fault)
            # Each mode ends in fail_borrow's abort (cmake/expect_fail_fast.cmake).
            add_test(NAME gui_forms_borrowed_view_${_gui_borrow_mode}
                     COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gui_forms_borrowed_view_tests>
                             -DMODE=${_gui_borrow_mode}
                             "-DEXPECTED=GUI.Forms borrow sanitizer: ${_gui_borrow_${_gui_borrow_fault}}"
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/expect_fail_fast.cmake)
        endforeach()
    endif()

    add_executable(gui_forms_program_options_tests tests/program_options_tests.cpp)
    target_link_libraries(gui_forms_program_options_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_program_options_tests COMMAND gui_forms_program_options_tests)

    add_executable(gui_forms_shadow_frames_tests tests/shadow_frames_tests.cpp)
    target_link_libraries(gui_forms_shadow_frames_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_shadow_frames_tests COMMAND gui_forms_shadow_frames_tests)

    add_executable(gui_forms_timer_tests tests/timer_tests.cpp)
    target_link_libraries(gui_forms_timer_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_timer_tests COMMAND gui_forms_timer_tests)

    add_executable(gui_forms_surface_capability_tests tests/surface_capability_tests.cpp)
    target_link_libraries(gui_forms_surface_capability_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_surface_capability_tests COMMAND gui_forms_surface_capability_tests)

    add_executable(gui_forms_dispatcher_tests tests/dispatcher_tests.cpp)
    target_link_libraries(gui_forms_dispatcher_tests PRIVATE
        gui_forms_host_headless
        gui_forms_controls
    )
    add_test(NAME gui_forms_dispatcher_tests COMMAND gui_forms_dispatcher_tests)
    set_tests_properties(gui_forms_dispatcher_tests PROPERTIES TIMEOUT 60)

    add_executable(gui_forms_tooltip_tests tests/tooltip_tests.cpp)
    target_link_libraries(gui_forms_tooltip_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_tooltip_tests COMMAND gui_forms_tooltip_tests)

    add_executable(gui_forms_cut_off_text_tests tests/cut_off_text_tests.cpp)
    target_link_libraries(gui_forms_cut_off_text_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_cut_off_text_tests COMMAND gui_forms_cut_off_text_tests)

    add_executable(gui_forms_binding_tests tests/binding_tests.cpp)
    target_link_libraries(gui_forms_binding_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_binding_tests COMMAND gui_forms_binding_tests)
    add_executable(gui_forms_binding_provider_tests tests/binding_provider_tests.cpp)
    target_link_libraries(gui_forms_binding_provider_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_binding_provider_tests COMMAND gui_forms_binding_provider_tests)
    add_executable(gui_forms_number_format_tests tests/number_format_tests.cpp)
    target_link_libraries(gui_forms_number_format_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_number_format_tests COMMAND gui_forms_number_format_tests)

    add_executable(gui_forms_validation_tests tests/validation_tests.cpp)
    target_link_libraries(gui_forms_validation_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_validation_tests COMMAND gui_forms_validation_tests)

    add_executable(gui_forms_guidance_provider_tests
        tests/guidance_provider_tests.cpp)
    target_link_libraries(gui_forms_guidance_provider_tests PRIVATE
        gui_forms_controls)
    add_test(NAME gui_forms_guidance_provider_tests
             COMMAND gui_forms_guidance_provider_tests)

    add_executable(gui_forms_date_time_picker_tests
        tests/date_time_picker_tests.cpp
    )
    target_link_libraries(gui_forms_date_time_picker_tests PRIVATE gui_forms_controls)
    add_test(NAME gui_forms_date_time_picker_tests
             COMMAND gui_forms_date_time_picker_tests)

    add_executable(gui_forms_png_registry_tests tests/png_registry_tests.cpp)
    target_link_libraries(gui_forms_png_registry_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_png_registry_tests COMMAND gui_forms_png_registry_tests)

    add_executable(gui_forms_headless_trace_tests
        tests/headless_trace_tests.cpp
        tests/support/headless_trace.cpp
    )
    target_include_directories(gui_forms_headless_trace_tests PRIVATE tests)
    target_link_libraries(gui_forms_headless_trace_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_headless_trace_tests COMMAND gui_forms_headless_trace_tests)

    add_executable(gui_forms_host_protocol_tests tests/host_protocol_tests.cpp)
    target_link_libraries(gui_forms_host_protocol_tests PRIVATE
        gui_forms_host_headless
    )
    add_test(NAME gui_forms_host_protocol_tests COMMAND gui_forms_host_protocol_tests)
    add_executable(gui_forms_window_teardown_tests tests/window_teardown_tests.cpp)
    target_link_libraries(gui_forms_window_teardown_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_window_teardown_tests COMMAND gui_forms_window_teardown_tests)
    add_executable(gui_forms_runtime_windows_tests tests/runtime_windows_tests.cpp)
    target_link_libraries(gui_forms_runtime_windows_tests PRIVATE gui_forms_host_headless)
    add_test(NAME gui_forms_runtime_windows_tests COMMAND gui_forms_runtime_windows_tests)
    add_executable(gui_forms_compositor_backdrop_tests tests/compositor_backdrop_tests.cpp)
    target_link_libraries(gui_forms_compositor_backdrop_tests PRIVATE gui_forms_host_headless)
    add_test(NAME gui_forms_compositor_backdrop_tests COMMAND gui_forms_compositor_backdrop_tests)
    add_executable(gui_forms_floating_windows_tests tests/floating_windows_tests.cpp)
    target_link_libraries(gui_forms_floating_windows_tests PRIVATE gui_forms_host_headless)
    add_test(NAME gui_forms_floating_windows_tests COMMAND gui_forms_floating_windows_tests)
    add_executable(gui_forms_native_popup_tests tests/native_popup_tests.cpp)
    target_link_libraries(gui_forms_native_popup_tests PRIVATE gui_forms_host_headless gui_forms_controls)
    add_test(NAME gui_forms_native_popup_tests COMMAND gui_forms_native_popup_tests)
    add_executable(gui_forms_splash_window_tests tests/splash_window_tests.cpp)
    target_link_libraries(gui_forms_splash_window_tests PRIVATE gui_forms_host_headless gui_forms_controls)
    add_test(NAME gui_forms_splash_window_tests COMMAND gui_forms_splash_window_tests)
    add_executable(gui_forms_menu_popup_tests tests/menu_popup_tests.cpp)
    target_link_libraries(gui_forms_menu_popup_tests PRIVATE gui_forms_host_headless gui_forms_controls)
    add_test(NAME gui_forms_menu_popup_tests COMMAND gui_forms_menu_popup_tests)
    add_executable(gui_forms_shared_resources_tests tests/shared_resources_tests.cpp)
    target_link_libraries(gui_forms_shared_resources_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_shared_resources_tests COMMAND gui_forms_shared_resources_tests)
    add_executable(gui_forms_image_atlas_tests tests/image_atlas_tests.cpp)
    target_link_libraries(gui_forms_image_atlas_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_image_atlas_tests COMMAND gui_forms_image_atlas_tests)
    add_executable(gui_forms_allocation_statistics_tests tests/allocation_statistics_tests.cpp)
    target_link_libraries(gui_forms_allocation_statistics_tests PRIVATE gui_forms_core)
    add_test(NAME gui_forms_allocation_statistics_tests COMMAND gui_forms_allocation_statistics_tests)
    add_executable(gui_forms_dock_floating_tests tests/dock_floating_tests.cpp)
    target_link_libraries(gui_forms_dock_floating_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_dock_floating_tests COMMAND gui_forms_dock_floating_tests)
    add_executable(gui_forms_workspace_transaction_tests tests/workspace_transaction_tests.cpp)
    target_link_libraries(gui_forms_workspace_transaction_tests PRIVATE gui_forms_controls gui_forms_host_headless)
    add_test(NAME gui_forms_workspace_transaction_tests COMMAND gui_forms_workspace_transaction_tests)
    add_test(
        NAME gui_forms_host_boundary_audit
        COMMAND ${CMAKE_COMMAND}
            -DTARGET_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
            -DNM_TOOL=${CMAKE_NM}
            -DOBJDUMP_TOOL=${CMAKE_OBJDUMP}
            -DCORE_LIBRARY=$<TARGET_FILE:gui_forms_core>
            -DHEADLESS_LIBRARY=$<TARGET_FILE:gui_forms_host_headless>
            -DHEADLESS_TEST_BINARY=$<TARGET_FILE:gui_forms_host_protocol_tests>
            -DPORTABLE_SOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/src
            -DPUBLIC_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_host_boundaries.cmake
    )

    if(UNIX AND NOT APPLE)
        add_test(
            NAME gui_drawing_c_api_export_policy
            COMMAND ${CMAKE_COMMAND}
                -DC_API_LIBRARY=$<TARGET_FILE:gui_drawing_c_api>
                -DEXPECTED_SYMBOL=gd_get_api_v0
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_linux_c_api_exports.cmake
        )
    endif()

    # The semantic half of the Windows MSAA and UI Automation Gallery
    # clients' checks (registry coverage, identities, and one default
    # action per control type and role by each client's rules), headless.
    if(TARGET gui_forms_gallery_model)
        add_executable(gui_forms_semantic_gallery_tests tests/semantic_gallery_tests.cpp)
        target_link_libraries(gui_forms_semantic_gallery_tests PRIVATE gui_forms_gallery_model)
        add_test(NAME gui_forms_semantic_gallery_tests COMMAND gui_forms_semantic_gallery_tests)
    endif()

    if(TARGET gui_forms_gallery_model)
    add_executable(gui_forms_gallery_interaction_tests
        tests/gallery_interaction_tests.cpp
        )
        target_link_libraries(gui_forms_gallery_interaction_tests PRIVATE
            gui_forms_gallery_model
        )
    add_test(NAME gui_forms_gallery_interaction_tests
             COMMAND gui_forms_gallery_interaction_tests)

        add_executable(gui_forms_gallery_pages_tests
            tests/gallery_pages/main.cpp
            tests/gallery_pages/catalog_tests.cpp
            tests/gallery_pages/shell_tests.cpp
            tests/gallery_pages/scroll_extent_tests.cpp
            tests/gallery_pages/display_trace_tests.cpp
            tests/gallery_pages/ranges-and-progress_tests.cpp
            tests/gallery_pages/containers-and-focus_tests.cpp
            tests/gallery_pages/animation-laboratory_tests.cpp
            tests/gallery_pages/text-and-input_tests.cpp
            tests/gallery_pages/collections-and-overlays_tests.cpp
            tests/gallery_pages/values-and-spinners_tests.cpp
            tests/gallery_pages/images-and-drawing_tests.cpp
            tests/gallery_pages/tabs-and-pages_tests.cpp
            tests/gallery_pages/checked-collections_tests.cpp
            tests/gallery_pages/timing-and-tooltips_tests.cpp
            tests/gallery_pages/dates-and-calendar_tests.cpp
            tests/gallery_pages/dialogs-and-host-services_tests.cpp
            tests/gallery_pages/states-and-diagnostics_tests.cpp
            tests/gallery_pages/dock-and-anchor_tests.cpp
            tests/gallery_pages/docking-workspace_tests.cpp
            tests/gallery_pages/retained-layout-panels_tests.cpp
            tests/gallery_pages/css-layout-panels_tests.cpp
            tests/gallery_pages/control-spectrum_tests.cpp
            tests/gallery_pages/commands-and-menus_tests.cpp
            tests/gallery_pages/tree-and-object-views_tests.cpp
            tests/gallery_pages/choices-and-sections_tests.cpp
            tests/gallery_pages/value-editors_tests.cpp
            tests/gallery_pages/responsive-layouts_tests.cpp
            tests/gallery_pages/anchored-overlays_tests.cpp
            tests/gallery_pages/live-surfaces_tests.cpp
            tests/gallery_pages/typography-and-inspection_tests.cpp
            tests/gallery_pages/semantic-feedback_tests.cpp
            tests/gallery_pages/notices-and-badges_tests.cpp
            tests/gallery_pages/input-and-rules_tests.cpp
            tests/gallery_pages/theme-settings_tests.cpp
            tests/gallery_pages/spinners-and-rings_tests.cpp
            tests/gallery_pages/instruments_tests.cpp
            tests/gallery_pages/guided-tasks_tests.cpp
            tests/gallery_pages/help-book_tests.cpp
            tests/gallery_pages/industrial-surfaces_tests.cpp
            tests/gallery_pages/anatomy_coverage_tests.cpp
            tests/gallery_pages/schema_export_tests.cpp
        )
        # The theme switch test compiles the Gallery's embedded theme package
        # (gui_forms_add_theme writes its header there).
        target_include_directories(gui_forms_gallery_pages_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/gui_forms_themes
        )
        target_link_libraries(gui_forms_gallery_pages_tests PRIVATE
            gui_forms_gallery_model
            gui_forms_host_headless
        )
        # The M0 measurement on the real Gallery (tools/gallery_dirty_lab.cpp);
        # a measurement program, not a test.
        add_executable(gui_forms_gallery_dirty_lab tools/gallery_dirty_lab.cpp)
        target_include_directories(gui_forms_gallery_dirty_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(gui_forms_gallery_dirty_lab PRIVATE gui_forms_gallery_model gui_forms_host_headless)
        add_test(NAME gui_forms_gallery_pages_tests
                 COMMAND gui_forms_gallery_pages_tests)
        # The test compares its control-schema export with the committed
        # manifest (stage T1).
        set_tests_properties(gui_forms_gallery_pages_tests PROPERTIES ENVIRONMENT
            "GUI_FORMS_CONTROL_SCHEMA=${CMAKE_CURRENT_SOURCE_DIR}/manifests/control-schema.json")
        # Every Gallery page's display commands against the table recorded
        # before the theme engine (T2 E2). A process of its own, so that no
        # other test makes a control first (see display_trace_tests.cpp).
        add_test(NAME gui_forms_gallery_display_trace_tests
                 COMMAND gui_forms_gallery_pages_tests --display-trace)
        # The policy script takes one comma-separated argument; a CMake list
        # would be split into separate COMMAND arguments.
        set(gui_forms_gallery_policy_files
            ${CMAKE_CURRENT_SOURCE_DIR}/src/controls/gallery_pages.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/controls/gallery_pages.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/controls/gallery/pages/page_context.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/controls/gallery/pages/pages.hpp
        )
        foreach(gui_forms_gallery_page_source IN LISTS gui_forms_gallery_page_sources)
            list(APPEND gui_forms_gallery_policy_files
                ${CMAKE_CURRENT_SOURCE_DIR}/${gui_forms_gallery_page_source})
        endforeach()
        string(REPLACE ";" "," gui_forms_gallery_policy_file_list
            "${gui_forms_gallery_policy_files}")
        add_test(
            NAME gui_forms_gallery_pages_public_control_policy
            COMMAND ${CMAKE_COMMAND}
                -DGALLERY_PAGE_FILES=${gui_forms_gallery_policy_file_list}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_pages_public_controls.cmake
        )
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_EXECUTABLE)
            add_test(
                NAME gui_forms_gallery_catalog_current
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/control_registry.py emit-cpp --check
            )
        endif()

    if(TARGET gui_forms_visual_inspector_lab_model)
        add_executable(gui_forms_visual_inspector_lab_tests
            tests/visual_inspector_lab_tests.cpp)
        target_include_directories(gui_forms_visual_inspector_lab_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo)
        target_link_libraries(gui_forms_visual_inspector_lab_tests PRIVATE
            gui_forms_visual_inspector_lab_model)
        add_test(NAME gui_forms_visual_inspector_lab_tests
                 COMMAND gui_forms_visual_inspector_lab_tests)
    endif()

    if(TARGET gui_forms_layered_material_lab_model)
        add_executable(gui_forms_layered_material_lab_tests
            tests/layered_material_lab_tests.cpp)
        target_include_directories(gui_forms_layered_material_lab_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo)
        target_link_libraries(gui_forms_layered_material_lab_tests PRIVATE
            gui_forms_layered_material_lab_model)
        add_test(NAME gui_forms_layered_material_lab_tests
                 COMMAND gui_forms_layered_material_lab_tests)

        if(TARGET gui_forms_skia)
            add_executable(gui_forms_layered_material_raster_tests
                tests/layered_material_raster_tests.cpp)
            target_include_directories(gui_forms_layered_material_raster_tests PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/demo
                ${CMAKE_CURRENT_SOURCE_DIR}/src/render/skia/raster)
            target_link_libraries(gui_forms_layered_material_raster_tests PRIVATE
                gui_forms_layered_material_lab_model
                gui_forms_skia)
            add_test(NAME gui_forms_layered_material_raster_tests
                     COMMAND gui_forms_layered_material_raster_tests)
        endif()
    endif()

    if(TARGET gui_forms_compositor_lab_model AND TARGET gui_forms_skia)
        # The lab in each compositor state, drawn on the Skia raster every
        # host presents with: grants, fallbacks and the frame's alpha.
        add_executable(gui_forms_compositor_lab_tests
            tests/compositor_lab_tests.cpp)
        target_include_directories(gui_forms_compositor_lab_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo
            ${CMAKE_CURRENT_SOURCE_DIR}/src/render/skia/raster)
        target_link_libraries(gui_forms_compositor_lab_tests PRIVATE
            gui_forms_compositor_lab_model
            gui_forms_skia)
        add_test(NAME gui_forms_compositor_lab_tests
                 COMMAND gui_forms_compositor_lab_tests)
    endif()

    if(TARGET gui_forms_custom_chrome_lab_model)
        add_executable(gui_forms_custom_chrome_lab_tests
            tests/custom_chrome_lab_tests.cpp)
        target_include_directories(gui_forms_custom_chrome_lab_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo
            ${CMAKE_CURRENT_SOURCE_DIR}/src/host/headless)
        target_link_libraries(gui_forms_custom_chrome_lab_tests PRIVATE
            gui_forms_custom_chrome_lab_model
            gui_forms_host_headless)
        add_test(NAME gui_forms_custom_chrome_lab_tests
                 COMMAND gui_forms_custom_chrome_lab_tests)
    endif()

    if(TARGET gui_forms_typography_scale_lab_model)
        add_executable(gui_forms_typography_scale_lab_tests
            tests/typography_scale_lab_tests.cpp)
        target_include_directories(gui_forms_typography_scale_lab_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo)
        target_link_libraries(gui_forms_typography_scale_lab_tests PRIVATE
            gui_forms_typography_scale_lab_model)
        add_test(NAME gui_forms_typography_scale_lab_tests
                 COMMAND gui_forms_typography_scale_lab_tests)
    endif()

    if(TARGET gui_forms_responsive_tracks_lab_model)
        add_executable(gui_forms_responsive_tracks_lab_tests
            tests/responsive_tracks_lab_tests.cpp)
        target_include_directories(gui_forms_responsive_tracks_lab_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo)
        target_link_libraries(gui_forms_responsive_tracks_lab_tests PRIVATE
            gui_forms_responsive_tracks_lab_model)
        add_test(NAME gui_forms_responsive_tracks_lab_tests
                 COMMAND gui_forms_responsive_tracks_lab_tests)
    endif()

    if(TARGET gui_forms_connected_controls_lab_model)
        add_executable(gui_forms_connected_controls_lab_tests
            tests/connected_controls_lab_tests.cpp)
        target_include_directories(gui_forms_connected_controls_lab_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo)
        target_link_libraries(gui_forms_connected_controls_lab_tests PRIVATE
            gui_forms_connected_controls_lab_model)
        add_test(NAME gui_forms_connected_controls_lab_tests
                 COMMAND gui_forms_connected_controls_lab_tests)
    endif()

    if(TARGET gui_forms_seam_proximity_lab_model)
        add_executable(gui_forms_seam_proximity_lab_tests
            tests/seam_proximity_lab_tests.cpp)
        target_include_directories(gui_forms_seam_proximity_lab_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo)
        target_link_libraries(gui_forms_seam_proximity_lab_tests PRIVATE
            gui_forms_seam_proximity_lab_model)
        add_test(NAME gui_forms_seam_proximity_lab_tests
                 COMMAND gui_forms_seam_proximity_lab_tests)
    endif()
    endif()

    if(TARGET gui_forms_gallery)
        add_test(
            NAME gui_forms_gallery_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_gallery>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()

    if(TARGET gui_forms_visual_inspector_lab)
        add_test(
            NAME gui_forms_visual_inspector_lab_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_visual_inspector_lab>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()

    if(TARGET gui_forms_layered_material_lab)
        add_test(
            NAME gui_forms_layered_material_lab_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_layered_material_lab>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()

    if(TARGET gui_forms_custom_chrome_lab)
        add_test(
            NAME gui_forms_custom_chrome_lab_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_custom_chrome_lab>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()

    if(TARGET gui_forms_typography_scale_lab)
        add_test(
            NAME gui_forms_typography_scale_lab_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_typography_scale_lab>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()

    if(TARGET gui_forms_responsive_tracks_lab)
        add_test(
            NAME gui_forms_responsive_tracks_lab_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_responsive_tracks_lab>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()

    if(TARGET gui_forms_connected_controls_lab)
        add_test(
            NAME gui_forms_connected_controls_lab_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_connected_controls_lab>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()

    if(TARGET gui_forms_seam_proximity_lab)
        add_test(
            NAME gui_forms_seam_proximity_lab_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_seam_proximity_lab>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()

    if(TARGET gui_forms_host_macos)
        add_executable(gui_forms_macos_clipboard_image_tests tests/macos_clipboard_image_tests.mm)
    target_compile_options(gui_forms_macos_clipboard_image_tests PRIVATE -fobjc-arc)
    target_link_libraries(gui_forms_macos_clipboard_image_tests PRIVATE gui_forms_host_macos)
    add_test(NAME gui_forms_macos_clipboard_image_tests COMMAND gui_forms_macos_clipboard_image_tests)

    # The application menu names the running program, not the Gallery.
    add_executable(gui_forms_macos_application_menu_tests tests/macos_application_menu_tests.mm)
    target_compile_options(gui_forms_macos_application_menu_tests PRIVATE -fobjc-arc)
    target_link_libraries(gui_forms_macos_application_menu_tests PRIVATE gui_forms_host_macos)
    target_include_directories(gui_forms_macos_application_menu_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/host/macos/application)
    add_test(NAME gui_forms_macos_application_menu_tests COMMAND gui_forms_macos_application_menu_tests)

        # Stage T5, C3's measurement: what the window server composes from a
        # non-opaque window and a behind-window effect view, AppKit only.
        # An application bundle launched through LaunchServices, so that its
        # windows are active as a real application's are; the script prints
        # its JSON lines (kept in CI's LastTest.log evidence) and fails when
        # a material case was read in an inactive window. Serial: it reads
        # the screen.
        add_executable(gui_forms_macos_compositor_probe MACOSX_BUNDLE tests/macos_compositor_probe.mm)
        target_compile_options(gui_forms_macos_compositor_probe PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_compositor_probe PRIVATE
            "-framework AppKit" "-framework ApplicationServices")
        set_target_properties(gui_forms_macos_compositor_probe PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "T5 Compositor Probe"
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.t5-compositor-probe"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.3"
            MACOSX_BUNDLE_BUNDLE_VERSION "1")
        add_test(NAME gui_forms_macos_compositor_probe
            COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/run_macos_probe_bundle.sh
                $<TARGET_BUNDLE_DIR:gui_forms_macos_compositor_probe>
                ${CMAKE_CURRENT_BINARY_DIR}/macos_compositor_probe.jsonl)
        set_tests_properties(gui_forms_macos_compositor_probe PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

        add_executable(gui_forms_macos_exposure_tests tests/macos_exposure_tests.mm)
        target_compile_options(gui_forms_macos_exposure_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_exposure_tests PRIVATE gui_forms_host_macos)
        add_test(NAME gui_forms_macos_exposure_tests COMMAND gui_forms_macos_exposure_tests)

        # Audit P13: a PDF recording of the view outlives the raster's frame.
        add_executable(gui_forms_macos_recorded_draw_tests tests/macos_recorded_draw_tests.mm)
        target_compile_options(gui_forms_macos_recorded_draw_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_recorded_draw_tests PRIVATE gui_forms_host_macos)
        add_test(NAME gui_forms_macos_recorded_draw_tests COMMAND gui_forms_macos_recorded_draw_tests)
        set_tests_properties(gui_forms_macos_recorded_draw_tests PROPERTIES TIMEOUT 60)

        # Stage DP gate on macOS (notes:results/2026-10-25-dp-gate-macos).
        add_executable(gui_forms_macos_damage_rectangles_tests tests/macos_damage_rectangles_tests.mm)
        target_compile_options(gui_forms_macos_damage_rectangles_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_damage_rectangles_tests PRIVATE gui_forms_host_macos)
        add_test(NAME gui_forms_macos_damage_rectangles_tests COMMAND gui_forms_macos_damage_rectangles_tests)
        set_tests_properties(gui_forms_macos_damage_rectangles_tests PROPERTIES TIMEOUT 60)

        add_executable(gui_forms_macos_display_link_tests tests/macos_display_link_tests.mm)
        target_compile_options(gui_forms_macos_display_link_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_display_link_tests PRIVATE gui_forms_host_macos gui_forms_controls)
        add_test(NAME gui_forms_macos_display_link_tests COMMAND gui_forms_macos_display_link_tests)
        add_test(NAME gui_forms_macos_display_link_overlay_tests COMMAND gui_forms_macos_display_link_tests --overlay)
        set_tests_properties(gui_forms_macos_display_link_tests gui_forms_macos_display_link_overlay_tests PROPERTIES TIMEOUT 45)

        add_executable(gui_forms_macos_text_input_caret_tests tests/macos_text_input_caret_tests.mm)
        target_compile_options(gui_forms_macos_text_input_caret_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_text_input_caret_tests PRIVATE gui_forms_host_macos gui_forms_controls)
        add_test(NAME gui_forms_macos_text_input_caret_tests COMMAND gui_forms_macos_text_input_caret_tests)
        set_tests_properties(gui_forms_macos_text_input_caret_tests PROPERTIES TIMEOUT 15)

        add_executable(gui_forms_macos_text_composition_tests tests/macos_text_composition_tests.mm)
        target_compile_options(gui_forms_macos_text_composition_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_text_composition_tests PRIVATE gui_forms_host_macos gui_forms_controls)
        add_test(NAME gui_forms_macos_text_composition_tests COMMAND gui_forms_macos_text_composition_tests)
        set_tests_properties(gui_forms_macos_text_composition_tests PROPERTIES TIMEOUT 15)

        add_executable(gui_forms_macos_idle_visibility_tests tests/macos_idle_visibility_tests.mm)
        target_compile_options(gui_forms_macos_idle_visibility_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_idle_visibility_tests PRIVATE
            gui_forms_host_macos gui_forms_controls)
        add_test(NAME gui_forms_macos_idle_visibility_tests COMMAND gui_forms_macos_idle_visibility_tests)
        set_tests_properties(gui_forms_macos_idle_visibility_tests PROPERTIES TIMEOUT 15)

    add_executable(gui_forms_macos_host_close_tests tests/macos_host_close_tests.mm)
        target_compile_options(gui_forms_macos_host_close_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_host_close_tests PRIVATE
            gui_forms_host_macos
        )
        add_test(NAME gui_forms_macos_host_close_tests
                 COMMAND gui_forms_macos_host_close_tests)

        add_executable(gui_forms_macos_message_dialog_key_tests MACOSX_BUNDLE
            tests/macos_message_dialog_key_tests.mm)
        target_compile_options(gui_forms_macos_message_dialog_key_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_message_dialog_key_tests PRIVATE
            gui_forms_host_macos)
        # Launched through LaunchServices so the alert can become the key
        # window; see cmake/run_macos_activated_test.cmake.
        add_test(NAME gui_forms_macos_message_dialog_key_tests
                 COMMAND ${CMAKE_COMMAND}
                     -DBUNDLE=$<TARGET_BUNDLE_DIR:gui_forms_macos_message_dialog_key_tests>
                     -DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_macos_activated_test.cmake)
        set_tests_properties(gui_forms_macos_message_dialog_key_tests PROPERTIES TIMEOUT 60)

        # Stage A: the NSAccessibility tree of every stock control, read in
        # process (no Accessibility permission needed).
        add_executable(gui_forms_macos_accessibility_tests MACOSX_BUNDLE
            tests/macos_accessibility_tests.mm
            tests/support/accessibility_actions.cpp
            tests/support/macos_accessibility_contract.cpp
            tests/support/stock_control_fixture.cpp)
        target_compile_options(gui_forms_macos_accessibility_tests PRIVATE -fobjc-arc)
        target_include_directories(gui_forms_macos_accessibility_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(gui_forms_macos_accessibility_tests PRIVATE
            gui_forms_host_macos gui_forms_lab gui_forms_demo_controls)
        add_test(NAME gui_forms_macos_accessibility_tests
                 COMMAND gui_forms_macos_accessibility_tests)
        set_tests_properties(gui_forms_macos_accessibility_tests PROPERTIES TIMEOUT 60)

        # Stage A: the out-of-process AX client and the app it inspects. The
        # client needs the Accessibility permission; without it it reports
        # unsupported and exits 77, which ctest shows as skipped.
        add_executable(gui_forms_macos_accessibility_fixture_app MACOSX_BUNDLE
            tests/macos_accessibility_fixture_app.mm
            tests/support/stock_control_fixture.cpp)
        target_compile_options(gui_forms_macos_accessibility_fixture_app PRIVATE -fobjc-arc)
        target_include_directories(gui_forms_macos_accessibility_fixture_app PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(gui_forms_macos_accessibility_fixture_app PRIVATE
            gui_forms_host_macos gui_forms_lab gui_forms_demo_controls)
        set_target_properties(gui_forms_macos_accessibility_fixture_app PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.ax-fixture")
        find_library(GUI_FORMS_APPLICATION_SERVICES ApplicationServices REQUIRED)
        add_executable(gui_forms_macos_accessibility_client_tests
            tests/macos_accessibility_client_tests.mm
            tests/support/accessibility_actions.cpp
            tests/support/macos_accessibility_contract.cpp)
        target_compile_options(gui_forms_macos_accessibility_client_tests PRIVATE -fobjc-arc)
        target_include_directories(gui_forms_macos_accessibility_client_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(gui_forms_macos_accessibility_client_tests PRIVATE
            gui_forms_controls ${GUI_FORMS_APPKIT} ${GUI_FORMS_APPLICATION_SERVICES})
        add_dependencies(gui_forms_macos_accessibility_client_tests gui_forms_macos_accessibility_fixture_app)
        add_test(NAME gui_forms_macos_accessibility_client_tests
                 COMMAND gui_forms_macos_accessibility_client_tests
                     --app $<TARGET_BUNDLE_DIR:gui_forms_macos_accessibility_fixture_app>
                     --dump ${CMAKE_CURRENT_BINARY_DIR}/accessibility_fixture_semantics.json)
        set_tests_properties(gui_forms_macos_accessibility_client_tests PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 300)
        if(TARGET gui_forms_gallery_model)
            add_executable(gui_forms_macos_accessibility_gallery_app MACOSX_BUNDLE
                tests/macos_accessibility_fixture_app.mm
                tests/support/stock_control_fixture.cpp
                ${GUI_FORMS_GALLERY_FONTS} ${GUI_FORMS_GALLERY_FONT_NOTICES})
            target_compile_options(gui_forms_macos_accessibility_gallery_app PRIVATE -fobjc-arc)
            target_compile_definitions(gui_forms_macos_accessibility_gallery_app PRIVATE GUI_FORMS_AX_GALLERY=1)
            target_include_directories(gui_forms_macos_accessibility_gallery_app PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/tests)
            target_link_libraries(gui_forms_macos_accessibility_gallery_app PRIVATE
                gui_forms_gallery_model gui_forms_host_macos)
            set_target_properties(gui_forms_macos_accessibility_gallery_app PROPERTIES
                MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.ax-gallery")
            add_dependencies(gui_forms_macos_accessibility_client_tests gui_forms_macos_accessibility_gallery_app)
            add_test(NAME gui_forms_macos_accessibility_client_gallery_tests
                     COMMAND gui_forms_macos_accessibility_client_tests --gallery
                         --app $<TARGET_BUNDLE_DIR:gui_forms_macos_accessibility_gallery_app>
                         --dump ${CMAKE_CURRENT_BINARY_DIR}/accessibility_gallery_semantics.json)
            # Gallery-wide: walks every page; `ctest -LE gallery_accessibility` skips it.
            set_property(TEST gui_forms_macos_accessibility_client_gallery_tests APPEND PROPERTY LABELS gallery_accessibility)
            set_tests_properties(gui_forms_macos_accessibility_client_gallery_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 600)
        endif()

        add_executable(gui_forms_macos_multi_window_tests
            tests/macos_multi_window_tests.mm)
        target_compile_options(gui_forms_macos_multi_window_tests PRIVATE -fobjc-arc)
        target_link_libraries(gui_forms_macos_multi_window_tests PRIVATE
            gui_forms_host_macos
            gui_forms_controls)
        add_test(NAME gui_forms_macos_multi_window_tests
                 COMMAND gui_forms_macos_multi_window_tests)
        # The native host resolves its font pack through NSBundle, not an
        # environment override. Exercise the same resource route as applications.
        set_source_files_properties(
            ${GUI_FORMS_GALLERY_FONTS} ${GUI_FORMS_GALLERY_FONT_NOTICES}
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
        foreach(native_fixture IN ITEMS
                gui_forms_macos_clipboard_image_tests
                gui_forms_macos_exposure_tests
                gui_forms_macos_damage_rectangles_tests
                gui_forms_macos_idle_visibility_tests
                gui_forms_macos_host_close_tests
                gui_forms_macos_multi_window_tests
                gui_forms_macos_accessibility_tests
                gui_forms_macos_accessibility_fixture_app)
            set_target_properties(${native_fixture} PROPERTIES MACOSX_BUNDLE TRUE)
            target_sources(${native_fixture} PRIVATE
                ${GUI_FORMS_GALLERY_FONTS} ${GUI_FORMS_GALLERY_FONT_NOTICES})
        endforeach()
        if(TARGET gui_forms_prepared_text_impl)
            add_executable(gui_forms_macos_prepared_text_host_tests MACOSX_BUNDLE
                tests/macos_prepared_text_host_tests.mm
                ${GUI_FORMS_GALLERY_FONTS} ${GUI_FORMS_GALLERY_FONT_NOTICES})
            target_compile_options(gui_forms_macos_prepared_text_host_tests PRIVATE -fobjc-arc)
            target_link_libraries(gui_forms_macos_prepared_text_host_tests PRIVATE
                gui_forms_host_macos gui_forms_prepared_text_impl)
            add_test(NAME gui_forms_macos_prepared_text_host_tests
                COMMAND gui_forms_macos_prepared_text_host_tests
                    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
            set_tests_properties(gui_forms_macos_prepared_text_host_tests PROPERTIES TIMEOUT 90)
        endif()
    endif()

    if(TARGET gui_forms_skia)
        add_executable(gui_forms_live_surface_benchmark EXCLUDE_FROM_ALL benchmarks/live_surface_benchmark.cpp)
        target_link_libraries(gui_forms_live_surface_benchmark PRIVATE gui_forms_skia)
        # The canvas pan workload (felt pattern, gradient, sheet shadow and live
        # surface, panned in 8-unit steps at scales 1 and 2); run by hand.
        add_executable(gui_forms_canvas_pan_benchmark EXCLUDE_FROM_ALL benchmarks/canvas_pan_benchmark.cpp)
        target_link_libraries(gui_forms_canvas_pan_benchmark PRIVATE gui_forms_skia)
        # Threaded sub-surfaces measurements M1, M2, M4-M9 for the grid's cell
        # region (stage S7); run by hand, not by ctest.
        add_executable(gui_forms_sub_surface_cells_benchmark EXCLUDE_FROM_ALL benchmarks/sub_surface_cells_benchmark.cpp)
        target_link_libraries(gui_forms_sub_surface_cells_benchmark PRIVATE gui_forms_controls gui_forms_skia)
        add_executable(gui_forms_opaque_live_surface_raster_tests tests/opaque_live_surface_raster_tests.cpp)
        target_link_libraries(gui_forms_opaque_live_surface_raster_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_opaque_live_surface_raster_tests COMMAND gui_forms_opaque_live_surface_raster_tests)
        add_executable(gui_forms_canvas_occlusion_raster_tests tests/canvas_occlusion_raster_tests.cpp)
        target_link_libraries(gui_forms_canvas_occlusion_raster_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_canvas_occlusion_raster_tests COMMAND gui_forms_canvas_occlusion_raster_tests)
        add_executable(gui_forms_part_composite_identity_tests tests/part_composite_identity_tests.cpp)
        target_link_libraries(gui_forms_part_composite_identity_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_part_composite_identity_tests COMMAND gui_forms_part_composite_identity_tests)
        add_executable(gui_forms_ribbon_scale_raster_tests tests/ribbon_scale_raster_tests.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
        target_compile_definitions(gui_forms_ribbon_scale_raster_tests PRIVATE
            GUI_FORMS_TEST_FONT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        target_include_directories(gui_forms_ribbon_scale_raster_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(gui_forms_ribbon_scale_raster_tests PRIVATE gui_forms_gallery_model gui_forms_skia)
        add_test(NAME gui_forms_ribbon_scale_raster_tests COMMAND gui_forms_ribbon_scale_raster_tests)
        add_executable(gui_forms_surface_material_kernel_tests tests/surface_material_kernel_tests.cpp)
        target_link_libraries(gui_forms_surface_material_kernel_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_surface_material_kernel_tests COMMAND gui_forms_surface_material_kernel_tests)
        if(TARGET gui_forms_text_engine)
            add_executable(gui_forms_text_identity_raster_tests tests/text_identity_raster_tests.cpp)
            target_link_libraries(gui_forms_text_identity_raster_tests PRIVATE gui_forms_controls gui_forms_skia)
            target_compile_definitions(gui_forms_text_identity_raster_tests PRIVATE
                GUI_FORMS_TEST_CONTROL_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Regular.ttf")
            add_test(NAME gui_forms_text_identity_raster_tests COMMAND gui_forms_text_identity_raster_tests)
            add_executable(gui_forms_backplane_replay_identity_tests tests/backplane_replay_identity_tests.cpp)
            target_link_libraries(gui_forms_backplane_replay_identity_tests PRIVATE gui_forms_controls gui_forms_skia)
            target_compile_definitions(gui_forms_backplane_replay_identity_tests PRIVATE
                GUI_FORMS_TEST_CONTENT_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Regular.ttf")
            add_test(NAME gui_forms_backplane_replay_identity_tests COMMAND gui_forms_backplane_replay_identity_tests)
        endif()
        add_executable(gui_forms_retained_draw_cache_raster_tests tests/retained_draw_cache_raster_tests.cpp)
        target_link_libraries(gui_forms_retained_draw_cache_raster_tests PRIVATE gui_forms_controls gui_forms_skia)
        target_compile_definitions(gui_forms_retained_draw_cache_raster_tests PRIVATE
            GUI_FORMS_TEST_CONTROL_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Carlito-Regular.ttf"
            GUI_FORMS_TEST_FALLBACK_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoEmoji-Regular.ttf")
        add_test(NAME gui_forms_retained_draw_cache_raster_tests COMMAND gui_forms_retained_draw_cache_raster_tests)
        if(TARGET gui_forms_text_engine)
            add_executable(gui_forms_paragraph_glyph_tests tests/paragraph_glyph_tests.cpp)
            target_link_libraries(gui_forms_paragraph_glyph_tests PRIVATE
                gui_forms_controls gui_forms_skia gui_forms_text_engine)
            add_test(NAME gui_forms_paragraph_glyph_tests
                     COMMAND gui_forms_paragraph_glyph_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        endif()
        add_executable(gui_forms_skia_smoke src/render/skia/skia_raster_smoke.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
        target_compile_definitions(gui_forms_skia_smoke PRIVATE
            GUI_FORMS_TEST_CONTROL_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids.ttf"
            GUI_FORMS_TEST_CJK_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansCJKjp-Regular.otf"
            GUI_FORMS_TEST_EMOJI_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoEmoji-Regular.ttf"
        )
        target_link_libraries(gui_forms_skia_smoke PRIVATE gui_forms_skia)
        add_test(NAME gui_forms_skia_smoke COMMAND gui_forms_skia_smoke)
        # Process memory of the bundled fonts per configuration (item 4, step 6); a report, not a test.
        add_executable(gui_forms_bundled_font_memory_report src/render/skia/bundled_font_memory_report.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
        target_link_libraries(gui_forms_bundled_font_memory_report PRIVATE gui_forms_skia)

        # The specimen sheet (stage T4, phase 2): every stock control of the
        # Gallery in its states, light, dark and high contrast, scales 1 and 2,
        # as contact sheets with a hash manifest and pixel contrast. The test
        # captures the first page twice and requires identical pixels; a full
        # run is `gui_forms_specimen_sheet --out <directory> --repeat`.
        if(TARGET gui_forms_gallery_model)
            add_executable(gui_forms_specimen_sheet tests/specimen/specimen_sheet.cpp
                $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
            target_include_directories(gui_forms_specimen_sheet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
            target_compile_definitions(gui_forms_specimen_sheet PRIVATE
                GUI_FORMS_SPECIMEN_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
            target_link_libraries(gui_forms_specimen_sheet PRIVATE gui_forms_gallery_model gui_forms_skia)
            add_test(NAME gui_forms_specimen_sheet_tests COMMAND gui_forms_specimen_sheet --smoke)
        endif()

        # Windowless capture (owner request 2026-11-11, the theme palette):
        # OffscreenRaster with the bundled fonts named, Window::capture_control,
        # specimen states, and the stock specimen catalogue's completeness
        # against stock_control_anatomies().
        add_executable(gui_forms_offscreen_capture_tests tests/offscreen_capture_tests.cpp
            src/runtime/offscreen/offscreen_raster_create.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
        target_compile_definitions(gui_forms_offscreen_capture_tests PRIVATE
            GUI_FORMS_TEST_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        target_link_libraries(gui_forms_offscreen_capture_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_offscreen_capture_tests COMMAND gui_forms_offscreen_capture_tests)

        # Skeudoindustrial surfaces follow-ups 4 and 5: the indicator lamp
        # and the danger key, their timing, and that each lights only its
        # own lens or face (offscreen raster). GUI_FORMS_PREVIEW_DIR, when
        # set, receives preview shots.
        foreach(lamp_test indicator_lamp danger_key)
            add_executable(gui_forms_${lamp_test}_tests tests/${lamp_test}_tests.cpp
                src/runtime/offscreen/offscreen_raster_create.cpp
                $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
            target_compile_definitions(gui_forms_${lamp_test}_tests PRIVATE
                GUI_FORMS_TEST_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
            target_link_libraries(gui_forms_${lamp_test}_tests PRIVATE gui_forms_controls gui_forms_skia)
            add_test(NAME gui_forms_${lamp_test}_tests COMMAND gui_forms_${lamp_test}_tests)
        endforeach()
        # The node canvas's look in pixels (cp31), and its preview set:
        # gui_forms_canvas_look_raster_tests --preview <directory>.
        add_executable(gui_forms_canvas_look_raster_tests tests/canvas_look_raster_tests.cpp
            src/runtime/offscreen/offscreen_raster_create.cpp
            $<TARGET_OBJECTS:gui_forms_sealed_pages_objects>)
        target_compile_definitions(gui_forms_canvas_look_raster_tests PRIVATE
            GUI_FORMS_TEST_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
        target_link_libraries(gui_forms_canvas_look_raster_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_canvas_look_raster_tests COMMAND gui_forms_canvas_look_raster_tests)

        add_executable(gui_forms_canvas_raster_tests tests/canvas_raster_tests.cpp)
        target_link_libraries(gui_forms_canvas_raster_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_canvas_raster_tests COMMAND gui_forms_canvas_raster_tests)
        # draw_image below 1x resamples through stx_conv (audit P11).
        add_executable(gui_forms_skia_image_downscale_tests tests/skia_image_downscale_tests.cpp)
        target_link_libraries(gui_forms_skia_image_downscale_tests PRIVATE gui_forms_core gui_forms_skia)
        add_test(NAME gui_forms_skia_image_downscale_tests COMMAND gui_forms_skia_image_downscale_tests)

        add_executable(gui_forms_content_scroll_raster_tests tests/content_scroll_raster_tests.cpp)
        target_link_libraries(gui_forms_content_scroll_raster_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_content_scroll_raster_tests COMMAND gui_forms_content_scroll_raster_tests)

        add_executable(gui_forms_skia_edge_snapping_tests tests/skia_edge_snapping_tests.cpp)
        target_link_libraries(gui_forms_skia_edge_snapping_tests PRIVATE gui_forms_skia)
        add_test(NAME gui_forms_skia_edge_snapping_tests COMMAND gui_forms_skia_edge_snapping_tests)

        add_executable(gui_forms_paint_marks_raster_tests tests/paint_marks_raster_tests.cpp)
        target_link_libraries(gui_forms_paint_marks_raster_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_paint_marks_raster_tests COMMAND gui_forms_paint_marks_raster_tests)
        add_executable(gui_forms_plot_decimation_skia_tests tests/plot_decimation_skia_tests.cpp)
        target_link_libraries(gui_forms_plot_decimation_skia_tests PRIVATE
            gui_forms_plot_primitives gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_plot_decimation_skia_tests COMMAND gui_forms_plot_decimation_skia_tests)

        add_executable(gui_forms_skia_damage_edge_tests tests/skia_damage_edge_tests.cpp)
        target_link_libraries(gui_forms_skia_damage_edge_tests PRIVATE gui_forms_skia)
        add_test(NAME gui_forms_skia_damage_edge_tests COMMAND gui_forms_skia_damage_edge_tests)
        # A resize through several sizes presents what a full repaint draws.
        add_executable(gui_forms_resize_damage_equivalence_tests tests/resize_damage_equivalence_tests.cpp)
        target_link_libraries(gui_forms_resize_damage_equivalence_tests PRIVATE gui_forms_controls gui_forms_skia)
        add_test(NAME gui_forms_resize_damage_equivalence_tests COMMAND gui_forms_resize_damage_equivalence_tests)


        add_test(
            NAME gui_forms_skia_archive_policy
            COMMAND ${CMAKE_COMMAND}
                -DSKIA_ARCHIVE=$<TARGET_FILE:stx_skia::stx_skia>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_skia_archive.cmake
        )
    endif()

    if(TARGET gui_drawing_skia)
        add_executable(gui_drawing_skia_tests tests/drawing_skia_tests.cpp)
        target_compile_definitions(gui_drawing_skia_tests PRIVATE
            GUI_DRAWING_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids.ttf"
            GUI_DRAWING_TEST_BOLD_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids-Bold.ttf"
            GUI_DRAWING_TEST_ITALIC_FONT="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PortsmouthRapids-Italic.ttf"
        )
        target_link_libraries(gui_drawing_skia_tests PRIVATE gui_drawing_skia gui_forms_core)
        target_include_directories(gui_drawing_skia_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME gui_drawing_skia_tests COMMAND gui_drawing_skia_tests)

        add_executable(gui_drawing_raster_c_api_tests
            tests/drawing_raster_c_api_tests.cpp
        )
        target_link_libraries(gui_drawing_raster_c_api_tests PRIVATE
            gui_drawing_c_api
            gui_drawing_raster_c_api
        )
        add_test(NAME gui_drawing_raster_c_api_tests
                 COMMAND gui_drawing_raster_c_api_tests)
    endif()

    if(TARGET gui_forms_renderer_benchmark)
        add_test(
            NAME gui_forms_renderer_boundary_audit
            COMMAND ${CMAKE_COMMAND}
                -DCORE_LIBRARY=$<TARGET_FILE:gui_forms_core>
                -DSKIA_ADAPTER=$<TARGET_FILE:gui_forms_skia>
                -DBENCHMARK_BINARY=$<TARGET_FILE:gui_forms_renderer_benchmark>
                -DPUBLIC_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
                -DSKIA_ARCHIVE=$<TARGET_FILE:stx_skia::stx_skia>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_renderer_boundaries.cmake
        )
    endif()
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
    if(GUI_FORMS_BUILD_TESTS AND TARGET stx_vorbis::stx_vorbis_stb)
        # stx_vorbis's decoder over a mutated stream (audit I3).
        gui_forms_add_fuzz_test(vorbis SOURCE fuzz/vorbis_fuzz.cpp LIBRARIES stx_vorbis::stx_vorbis_stb
            ARGUMENTS ${CMAKE_CURRENT_SOURCE_DIR}/examples/reference/tone.ogg)
    endif()
endif()

# An installed shared library finds the shared libraries it needs beside
# itself: libraries are installed into one directory, and a consumer's own
# search path covers only the libraries it links directly (a library's
# private dependencies are found through that library's own RUNPATH, or its
# LC_RPATH on macOS). Windows finds DLLs beside the program instead.
if(APPLE)
    set(GUI_FORMS_INSTALLED_LIBRARY_RPATH "@loader_path")
elseif(UNIX)
    set(GUI_FORMS_INSTALLED_LIBRARY_RPATH "$ORIGIN")
else()
    set(GUI_FORMS_INSTALLED_LIBRARY_RPATH "")
endif()

# An installed program finds the shared libraries it needs in the library
# directory beside its own (bin/../lib): the form compiler links the
# controls, core and libgui_forms_threading.
if(TARGET gui_forms_form_compiler AND NOT GUI_FORMS_INSTALLED_LIBRARY_RPATH STREQUAL "")
    file(RELATIVE_PATH GUI_FORMS_BIN_TO_LIB "/${CMAKE_INSTALL_BINDIR}" "/${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(gui_forms_form_compiler PROPERTIES
        INSTALL_RPATH "${GUI_FORMS_INSTALLED_LIBRARY_RPATH}/${GUI_FORMS_BIN_TO_LIB}")
endif()
if(TARGET gui_forms_form_compiler_lab AND NOT GUI_FORMS_INSTALLED_LIBRARY_RPATH STREQUAL "")
    file(RELATIVE_PATH GUI_FORMS_BIN_TO_LIB "/${CMAKE_INSTALL_BINDIR}" "/${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(gui_forms_form_compiler_lab PROPERTIES
        INSTALL_RPATH "${GUI_FORMS_INSTALLED_LIBRARY_RPATH}/${GUI_FORMS_BIN_TO_LIB}")
endif()

if(TARGET gui_forms_application)
    target_link_libraries(gui_forms_application PUBLIC GUIForms::Threading)
    # libgui_forms_threading, beside it, when a library loads it privately
    # (the C ABI library).
    set_target_properties(gui_forms_application PROPERTIES INSTALL_RPATH "${GUI_FORMS_INSTALLED_LIBRARY_RPATH}")
    set_property(TARGET gui_forms_application PROPERTY INTERFACE_GUI_FORMS_CORE_LINKAGE application)
    set_property(TARGET gui_forms_application APPEND PROPERTY COMPATIBLE_INTERFACE_STRING GUI_FORMS_CORE_LINKAGE)
endif()

include(cmake/ApplicationText.cmake)
include(cmake/TextStackExport.cmake)
include(cmake/CAbi.cmake)

install(FILES cmake/llvm22.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
install(FILES src/core/threading/atomic_pool/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME threadpool_atomic_fast-LICENSE.txt)
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
install(FILES "${GUI_FORMS_STX_AUXLIB_SOURCE_DIR}/btree/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_btree-LICENSE.txt)
install(FILES "${GUI_FORMS_STX_AUXLIB_SOURCE_DIR}/btree/NOTICE" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_btree-NOTICE.txt)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms)
# Every third-party component the SDK and the programs ship, with its licence
# and the FreeType credit (THIRD_PARTY_NOTICES.md).
install(FILES THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms)
# The stx package keeps the stx source's licence files at their source paths.
set(gui_forms_stx_notices "${GUI_FORMS_STX_BUILD_PREFIX_DIR}/share/stx_text/notices")
install(FILES "${gui_forms_stx_notices}/third_party/unicode/LICENSE.txt" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME Unicode-LICENSE.txt)
if(GUI_FORMS_ENABLE_HARFBUZZ_TEXT)
    install(FILES "${gui_forms_stx_notices}/third_party/harfbuzz/COPYING" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME HarfBuzz-COPYING.txt)
    install(FILES "${gui_forms_stx_notices}/third_party/freetype/docs/FTL.TXT" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME FreeType-FTL.txt)
endif()
if(GUI_FORMS_ENABLE_SKIA)
    install(FILES "${gui_forms_stx_notices}/stx_skia/LICENSE-Skia" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME Skia-LICENSE.txt)
    install(FILES "${gui_forms_stx_notices}/stx_skia/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME stx_skia-LICENSE.txt)
endif()

install(FILES "${gui_forms_stx_notices}/third_party/sheenbidi/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME SheenBidi-LICENSE.txt)
install(FILES "${gui_forms_stx_notices}/third_party/md4c/LICENSE.md" DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/GUIForms RENAME MD4C-LICENSE.txt)

# Windows: the installed DLLs embed the LLVM runtimes, winpthreads and the
# MinGW-w64 CRT (add_link_options(-static) above). Their licences come from
# the toolchain prefix, the directory above the compiler's bin (the CLANG64
# tree); the LLVM exception does not require them for embedded runtime code,
# and they are installed anyway.
if(WIN32 AND NOT MSVC)
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

# The accessibility service adapter is independently testable without a renderer.
# Stage A on Linux: a real AT-SPI client (tests/linux_accessibility_client.py)
# drives the fixture over D-Bus inside its own session bus with the AT-SPI bus
# launcher and registry. It runs under the system Python, which carries
# PyGObject and the Atspi typelib (not a toolcache Python earlier on PATH).
# On CI a missing prerequisite fails the test; elsewhere the client reports
# the platform as unsupported with ctest's skip code, 77.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND GUI_FORMS_BUILD_TESTS)
    find_program(GUI_FORMS_SYSTEM_PYTHON3 NAMES python3 PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
    if(NOT GUI_FORMS_SYSTEM_PYTHON3)
        find_program(GUI_FORMS_SYSTEM_PYTHON3 NAMES python3)
    endif()
    set(gui_forms_linux_accessibility_test_ready OFF)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GUI_FORMS_TEST_ATK QUIET IMPORTED_TARGET atk atk-bridge-2.0 gio-2.0)
        find_package(X11 QUIET)
        if(TARGET PkgConfig::GUI_FORMS_TEST_ATK AND TARGET X11::X11)
            add_executable(gui_forms_linux_accessibility_fixture
                tests/linux_accessibility_fixture.cpp
                src/host/linux/accessibility/linux_accessibility.cpp)
            target_link_libraries(gui_forms_linux_accessibility_fixture PRIVATE
                gui_forms_controls gui_forms_core PkgConfig::GUI_FORMS_TEST_ATK X11::X11)
            set(gui_forms_linux_accessibility_test_ready ON)
        endif()
    endif()
    if(NOT GUI_FORMS_SYSTEM_PYTHON3)
        if("$ENV{CI}" STREQUAL "true")
            message(FATAL_ERROR "The Linux accessibility test needs python3 on CI")
        endif()
        message(STATUS "Linux accessibility test: unsupported (no python3)")
    elseif(gui_forms_linux_accessibility_test_ready)
        add_test(NAME gui_forms_linux_accessibility_tests
                 COMMAND ${GUI_FORMS_SYSTEM_PYTHON3} -B
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/linux_accessibility_client.py
                         $<TARGET_FILE:gui_forms_linux_accessibility_fixture>)
    else()
        add_test(NAME gui_forms_linux_accessibility_tests
                 COMMAND ${GUI_FORMS_SYSTEM_PYTHON3} -B
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/linux_accessibility_client.py
                         --unsupported "ATK, atk-bridge-2.0 or X11 development files were not found")
    endif()
    if(TEST gui_forms_linux_accessibility_tests)
        set_tests_properties(gui_forms_linux_accessibility_tests PROPERTIES
            SKIP_RETURN_CODE 77)
    endif()
    # A-L2: the whole Gallery through AT-SPI (every stock control's node and
    # default action). The Gallery model exists only with
    # GUI_FORMS_BUILD_GALLERY, which the programs workflow sets; without it
    # there is no Gallery to publish and the test is not configured. It walks
    # every page over D-Bus, so it gets a longer timeout than the suite's.
    if(gui_forms_linux_accessibility_test_ready AND GUI_FORMS_SYSTEM_PYTHON3 AND TARGET gui_forms_gallery_model)
        add_executable(gui_forms_linux_accessibility_gallery_fixture
            tests/linux_accessibility_gallery_fixture.cpp
            src/host/linux/accessibility/linux_accessibility.cpp)
        target_include_directories(gui_forms_linux_accessibility_gallery_fixture PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(gui_forms_linux_accessibility_gallery_fixture PRIVATE
            gui_forms_gallery_model gui_forms_host_headless PkgConfig::GUI_FORMS_TEST_ATK X11::X11)
        add_test(NAME gui_forms_linux_accessibility_gallery_tests
                 COMMAND ${GUI_FORMS_SYSTEM_PYTHON3} -B
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/linux_accessibility_gallery_client.py
                         $<TARGET_FILE:gui_forms_linux_accessibility_gallery_fixture>
                         ${CMAKE_CURRENT_SOURCE_DIR}/manifests/controls.json)
        # Gallery-wide: walks every page; `ctest -LE gallery_accessibility` skips it.
        set_property(TEST gui_forms_linux_accessibility_gallery_tests APPEND PROPERTY LABELS gallery_accessibility)
        set_tests_properties(gui_forms_linux_accessibility_gallery_tests PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 900)
    endif()
endif()

if(WIN32 AND GUI_FORMS_ENABLE_WINDOWS_HOST AND GUI_FORMS_BUILD_TESTS)
    add_executable(gui_forms_windows_input_settings_tests tests/windows_input_settings_tests.cpp)
    target_link_libraries(gui_forms_windows_input_settings_tests PRIVATE GUIForms::Application)
    add_test(NAME gui_forms_windows_input_settings_tests COMMAND gui_forms_windows_input_settings_tests)
    set_tests_properties(gui_forms_windows_input_settings_tests PROPERTIES TIMEOUT 60)
    # N61: the host places the system input method's windows at the caret and
    # does not advertise text_composition.
    add_executable(gui_forms_windows_input_method_tests tests/windows_input_method_tests.cpp)
    target_compile_definitions(gui_forms_windows_input_method_tests PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS})
    target_link_libraries(gui_forms_windows_input_method_tests PRIVATE
        gui_forms_controls gui_forms_host_windows imm32)
    add_test(NAME gui_forms_windows_input_method_tests COMMAND gui_forms_windows_input_method_tests)
    set_tests_properties(gui_forms_windows_input_method_tests PROPERTIES TIMEOUT 60)
    add_executable(gui_forms_windows_accessibility_tests tests/windows_accessibility_tests.cpp)
    target_link_libraries(gui_forms_windows_accessibility_tests PRIVATE GUIForms::Application oleacc uiautomationcore oleaut32 uuid)
    add_test(NAME gui_forms_windows_accessibility_tests COMMAND gui_forms_windows_accessibility_tests)
    # Stage A, A-W2: the UI Automation provider read through IUIAutomation
    # from a client thread.
    add_executable(gui_forms_windows_accessibility_uia_provider_tests
        tests/windows_accessibility_uia_provider_tests.cpp)
    target_compile_definitions(gui_forms_windows_accessibility_uia_provider_tests PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS})
    target_link_libraries(gui_forms_windows_accessibility_uia_provider_tests PRIVATE
        gui_forms_controls gui_forms_host_windows uiautomationcore ole32 oleaut32 uuid)
    add_test(NAME gui_forms_windows_accessibility_uia_provider_tests
        COMMAND gui_forms_windows_accessibility_uia_provider_tests)
    set_tests_properties(gui_forms_windows_accessibility_uia_provider_tests PROPERTIES TIMEOUT 60)
    # Stage A, A-W3: UI Automation's TextPattern read through IUIAutomation
    # from a client thread.
    add_executable(gui_forms_windows_accessibility_uia_text_tests
        tests/windows_accessibility_uia_text_tests.cpp)
    target_compile_definitions(gui_forms_windows_accessibility_uia_text_tests PRIVATE
        ${GUI_FORMS_WINDOWS_DEFINITIONS})
    target_link_libraries(gui_forms_windows_accessibility_uia_text_tests PRIVATE
        gui_forms_controls gui_forms_host_windows uiautomationcore ole32 oleaut32 uuid)
    add_test(NAME gui_forms_windows_accessibility_uia_text_tests
        COMMAND gui_forms_windows_accessibility_uia_text_tests)
    set_tests_properties(gui_forms_windows_accessibility_uia_text_tests PROPERTIES TIMEOUT 60)
    # Stage A, A-W1: an MSAA client enumerates every Gallery page in a real
    # Win32 window and performs each control's default action.
    if(TARGET gui_forms_gallery_model)
        add_executable(gui_forms_windows_accessibility_msaa_gallery_tests tests/windows_accessibility_msaa_gallery_tests.cpp)
        target_compile_definitions(gui_forms_windows_accessibility_msaa_gallery_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_accessibility_msaa_gallery_tests PRIVATE
            gui_forms_gallery_model gui_forms_host_windows oleacc uiautomationcore oleaut32 uuid)
        add_test(NAME gui_forms_windows_accessibility_msaa_gallery_tests COMMAND gui_forms_windows_accessibility_msaa_gallery_tests)
        # Every page, then one fresh Gallery window per page of actions:
        # about 10 s in a release tree, about 340 s in the sanitizer tree
        # (measured 2026-10-20 on the owner's machine with other builds
        # running), whose ctest budget is 900 s.
        # Gallery-wide: walks every page; `ctest -LE gallery_accessibility` skips it.
        set_property(TEST gui_forms_windows_accessibility_msaa_gallery_tests APPEND PROPERTY LABELS gallery_accessibility)
        set_tests_properties(gui_forms_windows_accessibility_msaa_gallery_tests PROPERTIES TIMEOUT 900)
        # Stage A, A-W3: an IUIAutomation client on a thread of its own
        # enumerates every Gallery page and performs each control's default
        # action through its pattern.
        add_executable(gui_forms_windows_accessibility_uia_gallery_tests tests/windows_accessibility_uia_gallery_tests.cpp)
        target_compile_definitions(gui_forms_windows_accessibility_uia_gallery_tests PRIVATE
            ${GUI_FORMS_WINDOWS_DEFINITIONS})
        target_link_libraries(gui_forms_windows_accessibility_uia_gallery_tests PRIVATE
            gui_forms_gallery_model gui_forms_host_windows uiautomationcore ole32 oleaut32 uuid)
        add_test(NAME gui_forms_windows_accessibility_uia_gallery_tests COMMAND gui_forms_windows_accessibility_uia_gallery_tests)
        # About 49 s in a release tree. In the sanitizer tree it needs about
        # 950 s of CPU time (every provider call takes a fresh snapshot of the
        # page): 741 s and 975 s wall on a lightly loaded machine, and more
        # than 1800 s through ctest on 2026-10-20 while other workspaces'
        # builds left it about 55% of a core. The limit is twice the
        # heaviest load measured.
        # Gallery-wide: walks every page; `ctest -LE gallery_accessibility` skips it.
        set_property(TEST gui_forms_windows_accessibility_uia_gallery_tests APPEND PROPERTY LABELS gallery_accessibility)
        set_tests_properties(gui_forms_windows_accessibility_uia_gallery_tests PROPERTIES TIMEOUT 3600)
    endif()
endif()

# Programs that bundle fonts share one output directory, so one fonts/
# beside them (the Windows and X11 hosts look there): the Gallery and its
# labs and the Correspondence demonstration (GUI_FORMS_GALLERY_FONTS), and the
# File Manager demoboard (GUI_FORMS_DEMOBOARD_FONTS), largely the same faces.
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
        list(APPEND gui_forms_font_sources ${GUI_FORMS_GALLERY_FONTS} ${GUI_FORMS_GALLERY_FONT_NOTICES})
    endif()
endforeach()
foreach(demoboard_program IN ITEMS gui_forms_file_manager_demoboard_windows gui_forms_file_manager_demoboard_linux)
    if(TARGET ${demoboard_program})
        list(APPEND gui_forms_font_programs ${demoboard_program})
        list(APPEND gui_forms_font_sources ${GUI_FORMS_DEMOBOARD_FONTS} ${GUI_FORMS_DEMOBOARD_FONT_NOTICES})
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
# core (material images), the controls (the raster canvas), the Windows drawing C API, and
# the Windows host with the tests that compile windows_host.cpp, the last seven by including it textually.
foreach(gui_forms_swizzle_target IN ITEMS gui_forms_core gui_forms_controls
        gui_drawing_c_api gui_forms_host_windows
        gui_forms_windows_dib_lifecycle_tests
        gui_forms_windows_prepared_text_tests gui_forms_windows_backplane_replay_tests
        gui_forms_windows_canvas_raster_tests gui_forms_windows_content_pixels_tests
        gui_forms_windows_control_snapshot_tests gui_forms_windows_damage_rectangles_tests
        gui_forms_windows_scroll_copy_tests gui_forms_windows_live_idle_tests
        gui_forms_windows_text_tests)
    if(TARGET ${gui_forms_swizzle_target})
        target_link_libraries(${gui_forms_swizzle_target} PRIVATE stx_skia::swizzle)
    endif()
endforeach()

# Every test carries one area label (cmake/TestAreas.cmake, tools/test_areas.json).
if(GUI_FORMS_BUILD_TESTS)
    include(cmake/TestAreas.cmake)
    gui_forms_label_test_areas()
endif()

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
        add_executable(focus_exec_lab benchmarks/focus_labs/exec_lab.cpp)
        target_link_libraries(focus_exec_lab PRIVATE gui_drawing_skia gui_forms_core)
        target_include_directories(focus_exec_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    endif()
endif()

# No floating-point contraction in first-party code (owner, 2026-11-01): Clang's default
# fuses a * b + c within an expression where the target has a fused multiply-add, which
# arm64 always has and the x86-64 baseline never does, so the same source gave different
# bits on the two. Every target this directory defines gets -ffp-contract=off; the
# stx libraries (stx_zlib, stx_png, stx_conv, stx_rtf2md, stx_vorbis) set it on their own
# targets in their own build. A kernel that wants fusion spells it std::fma, so the choice is
# visible and gives the same bits everywhere. Third-party code (FreeType, HarfBuzz, MD4C,
# SheenBidi, libunibreak) keeps its own flags in the stx package (cmake/stx_package/);
# miniaudio is compiled inside src/audio/audio.cpp and so takes the flag with it. stx_skia sets -ffp-contract=off on its
# library itself; its raster pipeline's explicit fused stages (the FMA entries in the FMA
# log) are a separate matter.
# One shared test runtime for the test programs that use only the toolkit's
# own libraries, before the options below read their links
# (cmake/TestRuntime.cmake).
if(GUI_FORMS_BUILD_TESTS)
    include(cmake/TestRuntime.cmake)
    gui_forms_link_tests_to_runtime()
endif()
include(cmake/Hardening.cmake)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    get_property(gui_forms_first_party_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(gui_forms_first_party_target IN LISTS gui_forms_first_party_targets)
        get_target_property(gui_forms_target_type ${gui_forms_first_party_target} TYPE)
        if(gui_forms_target_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            target_compile_options(${gui_forms_first_party_target} PRIVATE
                $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-ffp-contract=off>)
            # Hardening and the warning level (cmake/Hardening.cmake, D11).
            gui_forms_harden_target(${gui_forms_first_party_target} ${gui_forms_target_type})
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
endif()

# One precompiled header per group of test programs, after every option is set
# (cmake/TestPrecompiledHeader.cmake).
if(GUI_FORMS_BUILD_TESTS)
    include(cmake/TestPrecompiledHeader.cmake)
    gui_forms_share_test_precompiled_header()
endif()

# The foreground and input-desktop tests (gui_forms_foreground_test and
# gui_forms_input_desktop_test, above), on the hosts that build them.
foreach(gui_forms_foreground_name IN ITEMS
        gui_forms_application_native_tests
        gui_forms_gallery_interaction_tests
        gui_forms_windows_accessibility_tests
        gui_forms_windows_dib_lifecycle_tests
        gui_forms_windows_input_method_tests
        gui_forms_windows_prepared_text_tests)
    if(TEST ${gui_forms_foreground_name})
        gui_forms_foreground_test(${gui_forms_foreground_name})
    endif()
endforeach()
# The Popup checks of the menu probe (the Gallery under Application::run),
# Python scripts run through the foreground launcher in front of the
# interpreter (TEST_LAUNCHER only reaches a test whose command is a target).
# They are registered only where a launcher is set, so only on a machine
# whose tests run on private desktops: tools/popup_margin_probe.py (Popup
# and tooltip window size and hit margin) and a short seeded smoke run of
# tools/contention_fuzz.py, labelled timing and non_gating (a long run is
# local and on demand; see the script).
if(TARGET gui_forms_menu_probe_windows)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    set(gui_forms_popup_launcher "${GUI_FORMS_FOREGROUND_LAUNCHER}")
    if(NOT gui_forms_popup_launcher)
        set(gui_forms_popup_launcher "$ENV{GUI_FORMS_FOREGROUND_LAUNCHER}")
    endif()
    if(Python3_Interpreter_FOUND AND gui_forms_popup_launcher)
        add_test(NAME gui_forms_popup_margin_probe
            COMMAND ${gui_forms_popup_launcher} ${Python3_EXECUTABLE} -B
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/popup_margin_probe.py
                $<TARGET_FILE:gui_forms_menu_probe_windows> 1.5)
        set_property(TEST gui_forms_popup_margin_probe APPEND PROPERTY LABELS foreground)
        add_test(NAME gui_forms_contention_fuzz_smoke
            COMMAND ${gui_forms_popup_launcher} ${Python3_EXECUTABLE} -B
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/contention_fuzz.py
                $<TARGET_FILE:gui_forms_menu_probe_windows> --seed 20261110 --steps 40 --settle-every 10
                --no-shrink --out ${CMAKE_CURRENT_BINARY_DIR}/contention-fuzz)
        set_property(TEST gui_forms_contention_fuzz_smoke APPEND PROPERTY LABELS timing non_gating foreground)
    endif()
endif()
foreach(gui_forms_input_desktop_name IN ITEMS
        gui_forms_windows_accessibility_msaa_gallery_tests
        gui_forms_windows_accessibility_uia_gallery_tests
        gui_forms_windows_accessibility_uia_provider_tests
        gui_forms_windows_accessibility_uia_text_tests
        gui_forms_windows_compositor_probe)
    if(TEST ${gui_forms_input_desktop_name})
        gui_forms_input_desktop_test(${gui_forms_input_desktop_name})
    endif()
endforeach()
