# The C ABI library exports exactly its entry points (one module, owner
# decision 2026-11-15): the gf_ entry points of the allowlist and GUI.Drawing's
# gd_get_api_v0, whose table carries the raster's raster_initialize (there
# is no gdr_ export any more). libc++, STXlib, the engine and the text stack
# are linked in and stay local (cmake/CAbi.cmake).
#
#   cmake -DTOOL_NM=<nm> [-DTOOL_OBJDUMP=<objdump>] -DLIBRARY=<library>
#         -DEXPECTED=<gf_abi_exports.txt> [-DEXTRA_DEF=<shim .def>]
#         -P cmake/CheckCAbiExports.cmake
#
# EXPECTED lists the exact names, one per line (cmake/CAbi.cmake generates
# it from the allowlist). On Windows EXTRA_DEF names the Win32 compatibility
# shim's .def, whose exported names (the part before any '=') are accepted
# too. ELF: the dynamic symbol table (`nm -D --defined-only`). Mach-O: the
# external defined symbols (`nm -g --defined-only`; a symbol the exported
# list leaves out is private extern, which -g omits). PE (a .dll): the
# export table from `objdump -p`. Fails, naming each symbol not expected and
# each expected one missing, or when the library exports nothing.

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
if(NOT DEFINED EXPECTED OR NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR "CheckCAbiExports: EXPECTED (${EXPECTED}) does not exist")
endif()
file(STRINGS "${EXPECTED}" expected_names)
set(accepted ${expected_names})
if(DEFINED EXTRA_DEF AND NOT EXTRA_DEF STREQUAL "")
    file(STRINGS "${EXTRA_DEF}" def_lines)
    foreach(line IN LISTS def_lines)
        # "    IsWindow=gf_compat_IsWindow": the exported name is before '='.
        if(line MATCHES "^[ \t]+([A-Za-z_][A-Za-z0-9_]*)")
            list(APPEND accepted "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endif()

set(outside "")
set(found "")
foreach(symbol IN LISTS exported)
    # Mach-O's leading underscore is optional; ELF may version a name (@...).
    string(REGEX REPLACE "@.*$" "" name "${symbol}")
    if(APPLE OR LIBRARY MATCHES "\\.dylib$")
        string(REGEX REPLACE "^_" "" name "${name}")
    endif()
    list(FIND accepted "${name}" position)
    if(position EQUAL -1)
        list(APPEND outside "${symbol}")
    else()
        list(APPEND found "${name}")
    endif()
endforeach()
set(missing "")
foreach(name IN LISTS expected_names)
    list(FIND found "${name}" position)
    if(position EQUAL -1)
        list(APPEND missing "${name}")
    endif()
endforeach()
if(outside)
    list(LENGTH outside outside_count)
    list(SUBLIST outside 0 40 outside_shown)
    string(REPLACE ";" "\n  " outside_text "${outside_shown}")
    message(FATAL_ERROR "CheckCAbiExports: ${LIBRARY} exports ${outside_count} symbols outside "
                        "${EXPECTED} (the first 40):\n  ${outside_text}")
endif()
if(missing)
    string(REPLACE ";" "\n  " missing_text "${missing}")
    message(FATAL_ERROR "CheckCAbiExports: ${LIBRARY} does not export:\n  ${missing_text}")
endif()
list(LENGTH exported exported_count)
message(STATUS "CheckCAbiExports: ${LIBRARY} exports exactly its ${exported_count} entry points")
