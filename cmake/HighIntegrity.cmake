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
#     choice in the package's stamp, so a package built otherwise is refused.
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
# The test suite, the Gallery, the demonstrations and the form compiler call
# entries this configuration removes, so they are off here; the high-
# integrity tests (GUI_FORMS_BUILD_HIGH_INTEGRITY_TESTS) build instead
# (tools/ci_build.py --high-integrity).

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
    set(GUI_FORMS_BUILD_HIGH_INTEGRITY_TESTS OFF)
    return()
endif()

# Whether this tree is sanitized: STXlib's allocator is then the host's,
# which the sanitizers intercept (STXlib's own default in a sanitized tree).
set(_gf_high_integrity_sanitized OFF)
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
    set(_gf_high_integrity_sanitized ON)
endif()

# Each record: the cache entry, the value it must hold, its type, and why.
# A boolean compares by truth (ON, 1, TRUE and YES agree); a string by text.
set(_gf_high_integrity_settings
    "GUI_FORMS_HARDENING|ON|BOOL|the hardening flags are part of the configuration (cmake/Hardening.cmake)"
    "GUI_FORMS_WINDOWS_AUTOMATION_ENVIRONMENT|OFF|BOOL|the automation channel takes another process's requests"
    "GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY|OFF|BOOL|faces come from the embedded font pack only"
    "GUI_FORMS_BUILD_AUDIO|OFF|BOOL|the audio service decodes sound files from outside the program"
    "GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT|OFF|BOOL|the loop transport is a development channel"
    "GUI_FORMS_BUILD_PREPARED_TEXT|OFF|BOOL|the prepared-text service is a development service"
    "GUI_FORMS_BUILD_TEXT_MASKS|OFF|BOOL|the text-mask service is a development service"
    "GUI_FORMS_ENABLE_METAL_PRESENT|OFF|BOOL|the presentation shim's direct paths are not compiled"
    "GUI_FORMS_ENABLE_X11_DRI3_PRESENT|OFF|BOOL|the presentation shim's direct paths are not compiled"
    "GUI_FORMS_ENABLE_WAYLAND_PRESENT|OFF|BOOL|the presentation shim's direct paths are not compiled"
    "GUI_FORMS_ENABLE_WAYLAND_DMABUF_PRESENT|OFF|BOOL|the presentation shim's direct paths are not compiled"
    "GUI_FORMS_BUILD_TESTS|OFF|BOOL|the suite calls entries this configuration removes; GUI_FORMS_BUILD_HIGH_INTEGRITY_TESTS builds its tests"
    "GUI_FORMS_BUILD_GALLERY|OFF|BOOL|the Gallery calls entries this configuration removes"
    "GUI_FORMS_BUILD_GALLERY_MODEL|OFF|BOOL|the Gallery model calls entries this configuration removes"
    "GUI_FORMS_INSTALL_GALLERY_MODEL|OFF|BOOL|the Gallery model calls entries this configuration removes"
    "GUI_FORMS_BUILD_FILE_MANAGER_DEMOBOARD|OFF|BOOL|the demoboard calls entries this configuration removes"
    "GUI_FORMS_BUILD_FORM_COMPILER|OFF|BOOL|the form compiler is a build tool; use an ordinary build's"
    "GUI_FORMS_BUILD_FUZZERS|OFF|BOOL|the fuzzers feed outside bytes to the parsers"
    "GUI_FORMS_BUILD_BENCHMARKS|OFF|BOOL|the measurement harnesses are development programs"
    "STXLIB_MATH_DISPATCH|OFF|BOOL|STXlib's math takes one code path, chosen at build time")
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

option(GUI_FORMS_BUILD_HIGH_INTEGRITY_TESTS "Build the high-integrity configuration's tests" ON)
add_compile_definitions(GUI_FORMS_HIGH_INTEGRITY=1)
if(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    add_compile_definitions(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT=1)
endif()
if(GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    message(STATUS "GUI.Forms high integrity: on, clipboard text allowed")
else()
    message(STATUS "GUI.Forms high integrity: on")
endif()
