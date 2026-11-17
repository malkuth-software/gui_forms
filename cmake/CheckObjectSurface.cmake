# Proves that none of a set of binaries defines or imports a forbidden symbol.
# It reads only the binaries' symbol tables, so it checks what was built, not
# what the build was asked for. It names no product: GUI.Forms runs it over
# the high-integrity configuration's archives and modules
# (cmake/HighIntegrityTests.cmake), and it is meant to move to template_harness's
# project-neutral object-surface check when that lands.
#
#   cmake -DTOOL_NM=<nm> -DBINARIES=<a|b|...> -DFORBIDDEN=<file>
#         -P cmake/CheckObjectSurface.cmake
#
# BINARIES are archives, objects, shared libraries or programs, separated by
# '|' (a CMake list's ';' does not survive a -D argument on every
# generator). TOOL_NM is an nm that demangles (-C) and reads every binary
# given: LLVM's llvm-nm reads Mach-O, ELF, COFF and LLVM bitcode (ThinLTO
# archives).
#
# FORBIDDEN holds one CMake regular expression per line; '#' starts a comment
# line. Each is matched, unanchored, against every line of the demangled
# `nm -C` listing: defined symbols (local ones of an archive's members
# included) and undefined ones, which in an archive or object are what it
# imports. Write a pattern so that it matches a symbol, not an archive
# member's file name: a function's demangled name carries its '(' (written
# `\(`). A pattern may end in $, the end of the line (so `_?environ$` is
# the symbol environ and not environment_flag); it may not use ^ (the
# listing is matched as one text). Avoid square brackets (CMake lists do not
# split across an unbalanced one).
#
# cmake/CheckSystemImports.cmake includes this file when it is given
# FORBIDDEN, so one test checks a binary's library imports and its symbols.

cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS TOOL_NM BINARIES FORBIDDEN)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "CheckObjectSurface: set ${required}")
    endif()
endforeach()
if(NOT EXISTS "${FORBIDDEN}")
    message(FATAL_ERROR "CheckObjectSurface: FORBIDDEN (${FORBIDDEN}) does not exist")
endif()

file(STRINGS "${FORBIDDEN}" forbidden_lines)
set(patterns "")
foreach(line IN LISTS forbidden_lines)
    string(STRIP "${line}" line)
    if(NOT line STREQUAL "" AND NOT line MATCHES "^#")
        list(APPEND patterns "${line}")
    endif()
endforeach()
if(NOT patterns)
    message(FATAL_ERROR "CheckObjectSurface: ${FORBIDDEN} names no pattern")
endif()

string(REPLACE "|" ";" binaries "${BINARIES}")
set(failures "")
set(checked 0)
foreach(binary IN LISTS binaries)
    if(NOT EXISTS "${binary}")
        message(FATAL_ERROR "CheckObjectSurface: ${binary} does not exist")
    endif()
    execute_process(COMMAND "${TOOL_NM}" -C "${binary}"
        OUTPUT_VARIABLE listing ERROR_VARIABLE listing_error RESULT_VARIABLE listed)
    if(NOT listed EQUAL 0)
        message(FATAL_ERROR "CheckObjectSurface: nm failed on ${binary}: ${listing_error}")
    endif()
    if(listing STREQUAL "")
        message(FATAL_ERROR "CheckObjectSurface: nm listed no symbol of ${binary}")
    endif()
    # The listing as one text, without CMake list separators, so a symbol
    # with ';' or brackets in it does not split, and ending in a newline, so
    # a final $ can be matched as one.
    string(REPLACE ";" "," listing "${listing}\n")
    foreach(pattern IN LISTS patterns)
        if(pattern MATCHES "^(.*)\\$$")
            string(REGEX MATCHALL "[^\n]*${CMAKE_MATCH_1}\n" hits "${listing}")
        else()
            string(REGEX MATCHALL "[^\n]*${pattern}[^\n]*" hits "${listing}")
        endif()
        foreach(hit IN LISTS hits)
            string(STRIP "${hit}" hit)
            list(APPEND failures "${binary}: ${hit}  (pattern ${pattern})")
        endforeach()
    endforeach()
    math(EXPR checked "${checked} + 1")
endforeach()
if(failures)
    list(REMOVE_DUPLICATES failures)
    string(REPLACE ";" "\n  " failure_text "${failures}")
    message(FATAL_ERROR "CheckObjectSurface: forbidden symbols:\n  ${failure_text}")
endif()
list(LENGTH patterns pattern_count)
message(STATUS "CheckObjectSurface: ${checked} binaries hold none of the ${pattern_count} forbidden patterns")
