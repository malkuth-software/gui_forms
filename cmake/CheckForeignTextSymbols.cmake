# Symbol isolation of the text stack (owner decisions 2026-11-13, "What this
# asks of GUI.Forms": a consumer may link its own HarfBuzz and FreeType beside
# GUI.Forms). The one shared GUI.Forms library, the C ABI library, holds the
# pinned FreeType, HarfBuzz, SheenBidi, libunibreak and MD4C privately; its
# generated export list leaves every one of those symbols local. This script fails when a shared library defines and exports
# any of them, so a consumer's own copy can never bind to ours or ours to its.
#
#   cmake -DTOOL_NM=<nm> [-DTOOL_OBJDUMP=<objdump>] -DLIBRARIES=<a|b|...>
#         -P cmake/CheckForeignTextSymbols.cmake
#
# ELF and Mach-O: `nm --extern-only --defined-only` (llvm-nm, GNU nm and
# Xcode's nm read the long options). PE (a .dll): the export table from
# `objdump -p`. The libraries are separated by '|', since a CMake list's ';'
# does not survive a -D argument on every generator.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED LIBRARIES OR LIBRARIES STREQUAL "")
    message(FATAL_ERROR "CheckForeignTextSymbols: set LIBRARIES")
endif()
string(REPLACE "|" ";" foreign_libraries "${LIBRARIES}")

# The pinned text stack's public and internal prefixes, with Mach-O's
# leading underscore optional.
set(foreign_pattern "^_?(hb_|FT_|FTC_|ft_|SB[A-Z][A-Za-z]|lb_|unibreak_|init_linebreak|set_linebreaks|is_line_breakable|md_)")

set(foreign_failures "")
foreach(library IN LISTS foreign_libraries)
    if(NOT EXISTS "${library}")
        message(FATAL_ERROR "CheckForeignTextSymbols: ${library} does not exist")
    endif()
    set(exported "")
    if(library MATCHES "\\.[dD][lL][lL]$")
        if(NOT TOOL_OBJDUMP)
            message(FATAL_ERROR "CheckForeignTextSymbols: a DLL needs TOOL_OBJDUMP")
        endif()
        execute_process(COMMAND "${TOOL_OBJDUMP}" -p "${library}"
            OUTPUT_VARIABLE listing ERROR_VARIABLE listing_error RESULT_VARIABLE listed)
        if(NOT listed EQUAL 0)
            message(FATAL_ERROR "CheckForeignTextSymbols: objdump failed on ${library}: ${listing_error}")
        endif()
        string(REPLACE "\n" ";" lines "${listing}")
        foreach(line IN LISTS lines)
            if(line MATCHES "^[ \t]*\\[ *[0-9]+\\] +([^ \t]+)")
                list(APPEND exported "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    else()
        if(NOT TOOL_NM)
            message(FATAL_ERROR "CheckForeignTextSymbols: set TOOL_NM")
        endif()
        execute_process(COMMAND "${TOOL_NM}" --extern-only --defined-only "${library}"
            OUTPUT_VARIABLE listing ERROR_VARIABLE listing_error RESULT_VARIABLE listed)
        if(NOT listed EQUAL 0)
            message(FATAL_ERROR "CheckForeignTextSymbols: nm failed on ${library}: ${listing_error}")
        endif()
        string(REPLACE "\n" ";" lines "${listing}")
        foreach(line IN LISTS lines)
            # "<address> <type> <name>"; the name is the last field.
            if(line MATCHES "[ \t]([^ \t]+)$")
                list(APPEND exported "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    list(LENGTH exported exported_count)
    # A listing with nothing in it means the tool read nothing, not that the
    # library is clean: every one of these libraries exports GUI.Forms.
    if(exported_count EQUAL 0)
        message(FATAL_ERROR "CheckForeignTextSymbols: no exported symbols read from ${library}")
    endif()
    set(found 0)
    foreach(symbol IN LISTS exported)
        if(symbol MATCHES "${foreign_pattern}")
            string(APPEND foreign_failures "  ${library}: ${symbol}\n")
            math(EXPR found "${found} + 1")
        endif()
    endforeach()
    message(STATUS "${library}: ${exported_count} exported symbols, ${found} from the text stack")
endforeach()

if(NOT foreign_failures STREQUAL "")
    message(FATAL_ERROR "Text-stack symbols are exported:\n${foreign_failures}")
endif()
message(STATUS "No shared library exports a text-stack symbol")
