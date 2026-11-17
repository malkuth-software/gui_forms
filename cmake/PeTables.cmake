# The export and import tables of a PE image (a .dll or .exe), read from an
# `objdump -p` listing, for the cmake -P checks (CheckCAbiExports and
# CheckAllocatorBoundary). Included, never run.
#
# Two listings are read, since CMAKE_OBJDUMP is GNU objdump in a MinGW
# binutils toolchain and llvm-objdump in the pinned CLANG64 one (whose
# objdump.exe is llvm-objdump too):
#
#   exports, GNU:   [Ordinal/Name Pointer] Table
#                   	[   0] gf_get_api
#   exports, LLVM:  Export Table:
#                    Ordinal      RVA  Name
#                          1   0x2530  gf_get_api
#   imports, GNU:   	DLL Name: KERNEL32.dll
#                   	vma:  Hint/Ord Member-Name Bound-To
#                   	1a2b2	  123  CloseHandle
#   imports, LLVM:      DLL Name: KERNEL32.dll
#                       Hint/Ord  Name
#                              0  CloseHandle
#
# A table ends at a line that is neither blank nor indented, or at the start
# of the other table. An import by ordinal alone has no name and is skipped.

# Sets `result` to the names in the export table of `listing`, in table
# order; empty when the image exports nothing (or the listing is not PE).
function(gui_forms_pe_exports listing result)
    string(REPLACE "\n" ";" lines "${listing}")
    set(names "")
    set(inside FALSE)
    foreach(line IN LISTS lines)
        string(REPLACE "\r" "" line "${line}")
        if(line MATCHES "^Export Table:" OR line MATCHES "\\[Ordinal/Name Pointer\\] Table")
            set(inside TRUE)
            continue()
        endif()
        if(line MATCHES "^The Import Tables" OR line MATCHES "^[ \t]*DLL Name:")
            set(inside FALSE)
            continue()
        endif()
        if(NOT inside)
            continue()
        endif()
        if(NOT line STREQUAL "" AND NOT line MATCHES "^[ \t]")
            set(inside FALSE)
            continue()
        endif()
        if(line MATCHES "^[ \t]*\\[ *[0-9]+\\] +([^ \t]+)")
            list(APPEND names "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^[ \t]*[0-9]+[ \t]+0x[0-9A-Fa-f]+[ \t]+([^ \t]+)")
            list(APPEND names "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${result} "${names}" PARENT_SCOPE)
endfunction()

# Sets `result` to the imports of `listing`, one `<dll>|<name>` entry each
# (the DLL as the image names it), in table order.
function(gui_forms_pe_imports listing result)
    string(REPLACE "\n" ";" lines "${listing}")
    set(entries "")
    set(dll "")
    foreach(line IN LISTS lines)
        string(REPLACE "\r" "" line "${line}")
        if(line MATCHES "^[ \t]*DLL Name:[ \t]*([^ \t]+)")
            set(dll "${CMAKE_MATCH_1}")
            continue()
        endif()
        if(dll STREQUAL "")
            continue()
        endif()
        if(line MATCHES "^Export Table:" OR line MATCHES "\\[Ordinal/Name Pointer\\] Table"
           OR line MATCHES "^There is an export table")
            set(dll "")
            continue()
        endif()
        if(NOT line STREQUAL "" AND NOT line MATCHES "^[ \t]")
            set(dll "")
            continue()
        endif()
        # GNU: "<vma>\t<hint> <name>"; LLVM: "<hint>  <name>".
        if(line MATCHES "^[ \t]*[0-9A-Fa-f]+\t+[ \t]*[0-9]+[ \t]+([^ \t]+)")
            list(APPEND entries "${dll}|${CMAKE_MATCH_1}")
        elseif(line MATCHES "^[ \t]+[0-9]+[ \t][ \t]+([^ \t]+)$")
            list(APPEND entries "${dll}|${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${result} "${entries}" PARENT_SCOPE)
endfunction()
