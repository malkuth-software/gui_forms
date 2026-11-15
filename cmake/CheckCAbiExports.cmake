# The C ABI library exports its entry points and nothing else (one module,
# owner decision 2026-11-15): every exported symbol is a gf_ entry point or
# GUI.Drawing's gd_ or gdr_ one. libc++, STXlib, the engine and the text
# stack are linked in and stay local (cmake/CAbi.cmake).
#
#   cmake -DTOOL_NM=<nm> [-DTOOL_OBJDUMP=<objdump>] -DLIBRARY=<library>
#         -P cmake/CheckCAbiExports.cmake
#
# ELF: the dynamic symbol table (`nm -D --defined-only`). Mach-O: the
# external defined symbols (`nm -g --defined-only`; a symbol the exported
# list leaves out is private extern, which -g omits). PE (a .dll): the export
# table from `objdump -p`. Fails, naming each symbol outside the prefixes,
# or when the library exports nothing.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED LIBRARY OR NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "CheckCAbiExports: LIBRARY (${LIBRARY}) does not exist")
endif()

set(exported "")
if(LIBRARY MATCHES "\\.[dD][lL][lL]$")
    if(NOT TOOL_OBJDUMP)
        message(FATAL_ERROR "CheckCAbiExports: a DLL needs TOOL_OBJDUMP")
    endif()
    execute_process(COMMAND "${TOOL_OBJDUMP}" -p "${LIBRARY}"
        OUTPUT_VARIABLE listing ERROR_VARIABLE listing_error RESULT_VARIABLE listed)
    if(NOT listed EQUAL 0)
        message(FATAL_ERROR "CheckCAbiExports: objdump failed on ${LIBRARY}: ${listing_error}")
    endif()
    string(REPLACE "\n" ";" lines "${listing}")
    foreach(line IN LISTS lines)
        # "[   0] gf_get_api" in the Ordinal/Name Pointer table.
        if(line MATCHES "^[ \t]*\\[ *[0-9]+\\] +([^ \t]+)")
            list(APPEND exported "${CMAKE_MATCH_1}")
        endif()
    endforeach()
else()
    if(NOT TOOL_NM)
        message(FATAL_ERROR "CheckCAbiExports: set TOOL_NM")
    endif()
    if(APPLE OR LIBRARY MATCHES "\\.dylib$")
        set(nm_arguments -g --defined-only)
    else()
        set(nm_arguments -D --defined-only)
    endif()
    execute_process(COMMAND "${TOOL_NM}" ${nm_arguments} "${LIBRARY}"
        OUTPUT_VARIABLE listing ERROR_VARIABLE listing_error RESULT_VARIABLE listed)
    if(NOT listed EQUAL 0)
        message(FATAL_ERROR "CheckCAbiExports: nm failed on ${LIBRARY}: ${listing_error}")
    endif()
    string(REPLACE "\n" ";" lines "${listing}")
    foreach(line IN LISTS lines)
        # "<address> <type> <name>"; the name is the last field. An absolute
        # symbol naming a version node (ELF "A GUI_FORMS_ABI_1") is the
        # version script's, not a routine.
        if(line MATCHES "^[0-9A-Fa-f]* *([A-Za-z]) +([^ \t]+)$")
            if(NOT CMAKE_MATCH_1 STREQUAL "A")
                list(APPEND exported "${CMAKE_MATCH_2}")
            endif()
        endif()
    endforeach()
endif()

if(NOT exported)
    message(FATAL_ERROR "CheckCAbiExports: ${LIBRARY} exports nothing")
endif()
set(outside "")
foreach(symbol IN LISTS exported)
    # Mach-O's leading underscore is optional; ELF may version a name (@...).
    if(NOT symbol MATCHES "^_?(gf|gd|gdr)_[A-Za-z0-9_]+(@.*)?$")
        list(APPEND outside "${symbol}")
    endif()
endforeach()
if(outside)
    list(LENGTH outside outside_count)
    list(SUBLIST outside 0 40 outside_shown)
    string(REPLACE ";" "\n  " outside_text "${outside_shown}")
    message(FATAL_ERROR "CheckCAbiExports: ${LIBRARY} exports ${outside_count} symbols outside gf_, gd_ "
                        "and gdr_ (the first 40):\n  ${outside_text}")
endif()
list(LENGTH exported exported_count)
message(STATUS "CheckCAbiExports: ${LIBRARY} exports ${exported_count} entry points, all gf_, gd_ or gdr_")
