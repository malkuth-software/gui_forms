# A shipped module (a program, or the C ABI library) imports no library but
# the system's (one module, owner decision 2026-11-15): no first-party
# library (libgui_forms_*, libgui_drawing_*, libstxlib_*), no C++ runtime
# (libc++, libc++abi, libunwind, libstdc++, libgcc_s on Windows,
# libwinpthread) and no copy of the text stack.
#
#   cmake [-DTOOL_OBJDUMP=<objdump>] [-DALLOW_SHARED_CXX_RUNTIME=ON]
#         -DBINARIES=<a|b|...> -P cmake/CheckSystemImports.cmake
#
# ALLOW_SHARED_CXX_RUNTIME admits libc++, libc++abi and libunwind, for a
# macOS runtime tree that has no archives yet (cmake/llvm22.cmake,
# GUI_FORMS_STATIC_CXX_RUNTIME OFF); every other refusal stands.
#
# ELF: the NEEDED entries (`objdump -p`). Mach-O: `otool -L`. PE: the DLL
# Name entries (`objdump -p`). This is a refusal list of the libraries the
# build could wrongly leave shared; tools/package_programs.py checks shipped
# packages against the platforms' system libraries as well. The binaries are
# separated by '|', since a CMake list's ';' does not survive a -D argument
# on every generator.
#
# With FORBIDDEN (and TOOL_NM), the binaries' symbols are checked as well:
# none may define or import a symbol matching a pattern of that file
# (cmake/CheckObjectSurface.cmake, which this file then includes). Archives
# and objects (.a, .lib, .o, .obj) import no library and are given for that
# check only. The high-integrity configuration runs it
# (cmake/HighIntegrityTests.cmake) until GUI.Forms' harness pin carries
# template_harness's check_object_surface.py.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED BINARIES OR BINARIES STREQUAL "")
    message(FATAL_ERROR "CheckSystemImports: set BINARIES")
endif()
string(REPLACE "|" ";" binaries "${BINARIES}")
set(refused_pattern
    "(gui_forms_|gui_drawing_|stxlib_|libc\\+\\+|libunwind|libstdc\\+\\+|libgcc_s_|libwinpthread|freetype|harfbuzz|sheenbidi|unibreak)")

set(failures "")
foreach(binary IN LISTS binaries)
    if(NOT EXISTS "${binary}")
        message(FATAL_ERROR "CheckSystemImports: ${binary} does not exist")
    endif()
    set(imports "")
    if(binary MATCHES "\\.(a|lib|o|obj)$")
        # An archive or object names no library it imports; its symbols are FORBIDDEN's.
        continue()
    endif()
    if(APPLE AND NOT binary MATCHES "\\.[dD][lL][lL]$|\\.exe$")
        execute_process(COMMAND otool -L "${binary}"
            OUTPUT_VARIABLE listing ERROR_VARIABLE listing_error RESULT_VARIABLE listed)
        if(NOT listed EQUAL 0)
            message(FATAL_ERROR "CheckSystemImports: otool failed on ${binary}: ${listing_error}")
        endif()
        # A dylib's `otool -L` listing starts with its own install name
        # (LC_ID_DYLIB, @rpath/libgui_forms_abi.1.dylib), which is not an
        # import; `otool -D` names it, and it is left out.
        execute_process(COMMAND otool -D "${binary}"
            OUTPUT_VARIABLE identity_listing ERROR_QUIET RESULT_VARIABLE identity_listed)
        set(identity "")
        if(identity_listed EQUAL 0)
            string(REPLACE "\n" ";" identity_lines "${identity_listing}")
            list(LENGTH identity_lines identity_count)
            if(identity_count GREATER 1)
                list(GET identity_lines 1 identity)
                string(STRIP "${identity}" identity)
            endif()
        endif()
        string(REPLACE "\n" ";" lines "${listing}")
        foreach(line IN LISTS lines)
            # "\t/usr/lib/libSystem.B.dylib (compatibility version ...)".
            if(line MATCHES "^[ \t]+([^ \t]+) \\(" AND NOT CMAKE_MATCH_1 STREQUAL identity)
                list(APPEND imports "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    else()
        if(NOT TOOL_OBJDUMP)
            message(FATAL_ERROR "CheckSystemImports: set TOOL_OBJDUMP")
        endif()
        execute_process(COMMAND "${TOOL_OBJDUMP}" -p "${binary}"
            OUTPUT_VARIABLE listing ERROR_VARIABLE listing_error RESULT_VARIABLE listed)
        if(NOT listed EQUAL 0)
            message(FATAL_ERROR "CheckSystemImports: objdump failed on ${binary}: ${listing_error}")
        endif()
        string(REPLACE "\n" ";" lines "${listing}")
        foreach(line IN LISTS lines)
            if(line MATCHES "^[ \t]*NEEDED[ \t]+([^ \t]+)" OR line MATCHES "DLL Name:[ \t]*([^ \t]+)")
                list(APPEND imports "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    foreach(import IN LISTS imports)
        get_filename_component(import_name "${import}" NAME)
        string(TOLOWER "${import_name}" import_lower)
        if(ALLOW_SHARED_CXX_RUNTIME AND import_lower MATCHES "^lib(c\\+\\+|c\\+\\+abi|unwind)\\.")
            continue()
        endif()
        if(import_lower MATCHES "${refused_pattern}")
            list(APPEND failures "${binary}: ${import}")
        endif()
    endforeach()
endforeach()
if(failures)
    string(REPLACE ";" "\n  " failure_text "${failures}")
    message(FATAL_ERROR "CheckSystemImports: imports that are not the system's:\n  ${failure_text}")
endif()
message(STATUS "CheckSystemImports: only system libraries imported")
if(DEFINED FORBIDDEN AND NOT FORBIDDEN STREQUAL "")
    include("${CMAKE_CURRENT_LIST_DIR}/CheckObjectSurface.cmake")
endif()
