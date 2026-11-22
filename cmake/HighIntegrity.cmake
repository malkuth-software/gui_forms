# The high-integrity configuration (GUI_FORMS_HIGH_INTEGRITY, owner,
# 2026-11-16). Its rule is "not compiled", never "disabled": every function
# that takes bytes from outside the program, or reaches outside it, is left
# out of the libraries and out of the C ABI's tables, so a reviewer checks
# the build (the symbols its objects define and import), not the program's
# behaviour at run time.
#
# Included from CMakeLists.txt before the options it settles. With the
# option on:
#   - GUI_FORMS_HIGH_INTEGRITY=1 is a compile definition of every target of
#     this tree (Core, Controls, Drawing, every host, the C ABI) and a PUBLIC
#     definition of Core, so a consumer compiles the same declarations
#     (GUIFormsConfig.cmake carries it to an installed consumer);
#   - each setting in _gf_high_integrity_settings below takes its required
#     value; a cache entry that already holds another value (given with -D,
#     or left by an earlier configure) stops the configure, naming the
#     setting, the value it needs and why;
#   - the stx package is built with STXlib's own allocator (stx_minmallocpp)
#     and without math dispatch (one code path); cmake/Stx.cmake records the
#     choice in the package's stamp, so a package built otherwise is refused;
#   - the build is Release with the C++ runtime linked statically, so every
#     program is one module pruned by the linker (CMakeLists.txt: every
#     function and datum in its own section, -dead_strip on Mach-O,
#     --gc-sections elsewhere, ThinLTO where the toolchain links with lld).
#     A high-integrity program holds exactly what it reaches (owner,
#     2026-11-16), and its symbols are the proof
#     (gui_forms_high_integrity_surface checks a linked example program).
#
# What is compiled out is only what a program would reach without asking for
# it: environment and user-locale reads at start-up, host defaults (the
# hosts' virtual services, drops from other programs, frame capture), motion,
# the theme request read from the arguments and environment, and the C ABI
# table entries (null slots), which a table keeps reachable whatever the
# program calls. An entry a program must call itself (application fonts,
# hosted content, user themes, inline styles) stays compiled and is pruned
# from every program that does not call it.
#
# GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT is the one allowance so far:
# with it, HostServices keeps read_clipboard_text and write_clipboard_text
# (and the C ABI's two host slots). It means nothing without the option.
#
# GUI_FORMS_BUILD_C_ABI (default ON) leaves the C ABI library out of any
# build when OFF. Whether a high-integrity build may carry the C ABI is the
# owner's open question, so this file neither forces nor refuses it; a
# high-integrity C ABI has null slots where the entries are compiled out.
#
# The Gallery and the demonstrations call entries this configuration
# removes, so they are off here. The test suite does too: gui_forms_tests,
# given a test cache of this configuration (GUI_FORMS_TEST_CACHE), builds the
# high-integrity tests alone (its cmake/HighIntegrityTests.cmake). The form compiler and the theme check stay: they are
# build-time programs that build steps run (gui_forms_add_form,
# gui_forms_add_theme_check), executables of their own that no program links.
# The Gallery's lab form compiler is not built.

option(GUI_FORMS_HIGH_INTEGRITY
       "Leave out of the build everything that takes outside bytes or reaches outside the program (cmake/HighIntegrity.cmake)"
       OFF)
option(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT
       "With GUI_FORMS_HIGH_INTEGRITY: keep the host's clipboard text services" OFF)
option(GUI_FORMS_BUILD_C_ABI "Build the C ABI library (cmake/CAbi.cmake)" ON)

if(NOT GUI_FORMS_HIGH_INTEGRITY)
    if(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
        message(FATAL_ERROR "GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT is an allowance of the "
                            "high-integrity configuration; it needs GUI_FORMS_HIGH_INTEGRITY=ON")
    endif()
    return()
endif()

# Whether this tree is sanitized: STXlib's allocator is then the host's,
# which the sanitizers intercept (STXlib's own default in a sanitized tree).
set(_gf_high_integrity_sanitized OFF)
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
    set(_gf_high_integrity_sanitized ON)
endif()

# Each record: the cache entry, the value it must hold, its type, and why
# (no semicolon in a reason: CMake would split the record there).
# A boolean compares by truth (ON, 1, TRUE and YES agree); a string by text.
set(_gf_high_integrity_settings
    "GUI_FORMS_HARDENING|ON|BOOL|the hardening flags are part of the configuration (cmake/Hardening.cmake)"
    "GUI_FORMS_WINDOWS_AUTOMATION_ENVIRONMENT|OFF|BOOL|the automation channel takes another process's requests"
    "GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY|OFF|BOOL|faces come from the embedded font pack only"
    "GUI_FORMS_BUILD_AUDIO|OFF|BOOL|the audio service decodes sound files from outside the program"
    "GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT|OFF|BOOL|the loop transport is a development channel"
    "GUI_FORMS_BUILD_PREPARED_TEXT|OFF|BOOL|the prepared-text service is a development service"
    "GUI_FORMS_BUILD_TEXT_MASKS|OFF|BOOL|the text-mask service is a development service"
    "GUI_FORMS_ENABLE_WAYLAND_PRESENT|OFF|BOOL|the presentation shim's direct paths are not compiled"
    "GUI_FORMS_BUILD_GALLERY|OFF|BOOL|the Gallery calls entries this configuration removes"
    "GUI_FORMS_BUILD_GALLERY_MODEL|OFF|BOOL|the Gallery model calls entries this configuration removes"
    "GUI_FORMS_INSTALL_GALLERY_MODEL|OFF|BOOL|the Gallery model calls entries this configuration removes"
    "GUI_FORMS_BUILD_FILE_MANAGER_DEMOBOARD|OFF|BOOL|the demoboard calls entries this configuration removes"
    "STXLIB_MATH_DISPATCH|OFF|BOOL|STXlib's math takes one code path, chosen at build time"
    "GUI_FORMS_LTO|ON|BOOL|link-time optimisation prunes across modules where the toolchain allows")
if(NOT _gf_high_integrity_sanitized)
    list(APPEND _gf_high_integrity_settings
        "STXLIB_ALLOCATOR|stx_minmallocpp|STRING|STXlib's memory comes from its own allocator")
endif()

set(_gf_high_integrity_conflicts "")
foreach(_gf_high_integrity_record IN LISTS _gf_high_integrity_settings)
    string(REPLACE "|" ";" _gf_high_integrity_fields "${_gf_high_integrity_record}")
    list(GET _gf_high_integrity_fields 0 _gf_high_integrity_name)
    list(GET _gf_high_integrity_fields 1 _gf_high_integrity_value)
    list(GET _gf_high_integrity_fields 2 _gf_high_integrity_type)
    list(GET _gf_high_integrity_fields 3 _gf_high_integrity_reason)
    if(DEFINED CACHE{${_gf_high_integrity_name}})
        set(_gf_high_integrity_given "$CACHE{${_gf_high_integrity_name}}")
        set(_gf_high_integrity_agrees OFF)
        if(_gf_high_integrity_type STREQUAL "BOOL")
            if(_gf_high_integrity_value AND _gf_high_integrity_given)
                set(_gf_high_integrity_agrees ON)
            elseif(NOT _gf_high_integrity_value AND NOT _gf_high_integrity_given)
                set(_gf_high_integrity_agrees ON)
            endif()
        elseif(_gf_high_integrity_given STREQUAL _gf_high_integrity_value)
            set(_gf_high_integrity_agrees ON)
        endif()
        if(NOT _gf_high_integrity_agrees)
            string(APPEND _gf_high_integrity_conflicts
                   "\n  ${_gf_high_integrity_name}=${_gf_high_integrity_given}: needs "
                   "${_gf_high_integrity_value} (${_gf_high_integrity_reason})")
        endif()
    else()
        set(${_gf_high_integrity_name} "${_gf_high_integrity_value}" CACHE ${_gf_high_integrity_type}
            "Settled by GUI_FORMS_HIGH_INTEGRITY: ${_gf_high_integrity_reason}")
    endif()
endforeach()
# A pruned, one-module program needs a Release build (the pruning flags are
# Release's) and a static C++ runtime (cmake/llvm22.cmake decides it).
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    string(APPEND _gf_high_integrity_conflicts
           "\n  CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}: needs Release (the linker pruning is Release's)")
endif()
if(DEFINED GUI_FORMS_STATIC_CXX_RUNTIME AND NOT GUI_FORMS_STATIC_CXX_RUNTIME)
    string(APPEND _gf_high_integrity_conflicts
           "\n  GUI_FORMS_STATIC_CXX_RUNTIME=OFF: needs the C++ runtime linked statically (cmake/llvm22.cmake)")
endif()
if(NOT _gf_high_integrity_conflicts STREQUAL "")
    message(FATAL_ERROR "GUI_FORMS_HIGH_INTEGRITY=ON contradicts these settings:"
                        "${_gf_high_integrity_conflicts}\n"
                        "Remove them from the command line (or the cache), or configure without "
                        "GUI_FORMS_HIGH_INTEGRITY.")
endif()

# What cmake/Stx.cmake passes to the stx package build, and records in its
# stamp.
set(GUI_FORMS_HIGH_INTEGRITY_STX_ARGUMENTS -DSTXLIB_MATH_DISPATCH=OFF)
if(NOT _gf_high_integrity_sanitized)
    list(APPEND GUI_FORMS_HIGH_INTEGRITY_STX_ARGUMENTS -DSTXLIB_ALLOCATOR=stx_minmallocpp)
else()
    message(STATUS "GUI.Forms high integrity: sanitized tree, so STXlib keeps the host allocator "
                   "the sanitizers intercept")
endif()

add_compile_definitions(GUI_FORMS_HIGH_INTEGRITY=1)
if(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    add_compile_definitions(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT=1)
endif()
if(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    message(STATUS "GUI.Forms high integrity: on, clipboard text allowed")
else()
    message(STATUS "GUI.Forms high integrity: on")
endif()
