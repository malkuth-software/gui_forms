# No module frees another's memory, and none is redirected onto another's
# allocator (the memory ownership rule: we own our memory, free only what we
# allocated, and never force a foreign module onto our allocator). On
# Windows each module links its own C++ runtime statically (libc++, one
# module per program, owner decision 2026-11-15) and the C heap is the
# system's UCRT, shared by every module of the process. So a module's
# allocation entry points are its own (operator new and delete, from its
# static libc++) or the system's (malloc and free, from the CRT), and never
# another module's:
#
# - no module exports an allocation entry point: not the C heap's names,
#   not the global operator new or delete. A MinGW DLL linked without a .def
#   exports every global symbol (auto-export), and a consumer linking it
#   would then bind its operator new to the DLL's copy while its inline and
#   template code freed with its own;
# - a module imports the C heap's names only from the system CRT, and never
#   imports operator new or delete (they come from its static libc++);
# - with PROVIDER, a module that links that first-party DLL (the C ABI
#   library) imports from it only the names in PROVIDER_NAMES and the
#   PROVIDER_DEF's export names (the allowlist and the Win32 shim): nothing
#   of the engine, STXlib or the text stack, whose memory would then cross
#   the boundary.
#
# STXlib's allocator (stx_minmallocpp, in stxlib_state) is linked once into
# each module; that the C ABI library exports none of STXlib is the exact
# check of cmake/CheckCAbiExports.cmake.
#
#   cmake -DTOOL_OBJDUMP=<objdump> -DBINARIES=<a|b|...>
#         [-DPROVIDER=<dll file name> -DPROVIDER_NAMES=<names file> [-DPROVIDER_DEF=<.def>]]
#         -P cmake/CheckAllocatorBoundary.cmake
#
# PE images only (a .dll or .exe), read from `objdump -p` (GNU or LLVM,
# cmake/PeTables.cmake). The binaries are separated by '|', since a CMake
# list's ';' does not survive a -D argument on every generator. Fails naming
# every offending export and import, or when a binary cannot be read.

cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/PeTables.cmake")

if(NOT DEFINED BINARIES OR BINARIES STREQUAL "")
    message(FATAL_ERROR "CheckAllocatorBoundary: set BINARIES")
endif()
if(NOT TOOL_OBJDUMP)
    message(FATAL_ERROR "CheckAllocatorBoundary: set TOOL_OBJDUMP")
endif()
string(REPLACE "|" ";" boundary_binaries "${BINARIES}")

# The C heap's entry points (the CRT's names, with MinGW's leading-underscore
# spellings), and the global operator new and delete in the Itanium mangling
# MinGW uses (_Znwy, _ZdlPv...) and the Microsoft one (??2@, ??3@, ??_U@,
# ??_V@) a clang-cl module would use.
set(heap_pattern "^_?(malloc|calloc|realloc|free|_?aligned_(malloc|free|realloc|offset_malloc|offset_realloc)|_expand|_msize|_recalloc|strdup|_strdup|wcsdup|_wcsdup)$")
set(operator_pattern "^(_Zn[wa][jmy].*|_Zd[la]Pv.*|\\?\\?[23]@.*|\\?\\?_[UV]@.*)$")
# The system CRT DLLs whose heap every module of the process shares.
set(system_crt_pattern "^(api-ms-win-crt-[a-z0-9-]+|ucrtbase|ucrtbased|msvcrt)\\.dll$")

set(provider_accepted "")
if(DEFINED PROVIDER AND NOT PROVIDER STREQUAL "")
    if(NOT DEFINED PROVIDER_NAMES OR NOT EXISTS "${PROVIDER_NAMES}")
        message(FATAL_ERROR "CheckAllocatorBoundary: PROVIDER_NAMES (${PROVIDER_NAMES}) does not exist")
    endif()
    file(STRINGS "${PROVIDER_NAMES}" provider_accepted)
    if(DEFINED PROVIDER_DEF AND NOT PROVIDER_DEF STREQUAL "")
        file(STRINGS "${PROVIDER_DEF}" provider_def_lines)
        foreach(line IN LISTS provider_def_lines)
            # "    IsWindow=gf_compat_IsWindow": the exported name is before '='.
            if(line MATCHES "^[ \t]+([A-Za-z_][A-Za-z0-9_]*)")
                list(APPEND provider_accepted "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    string(TOLOWER "${PROVIDER}" provider_lower)
endif()

set(boundary_failures "")
foreach(binary IN LISTS boundary_binaries)
    if(NOT EXISTS "${binary}")
        message(FATAL_ERROR "CheckAllocatorBoundary: ${binary} does not exist")
    endif()
    if(NOT binary MATCHES "\\.([dD][lL][lL]|[eE][xX][eE])$")
        message(FATAL_ERROR "CheckAllocatorBoundary: ${binary} is not a PE image (.dll or .exe)")
    endif()
    execute_process(COMMAND "${TOOL_OBJDUMP}" -p "${binary}"
        OUTPUT_VARIABLE listing ERROR_VARIABLE listing_error RESULT_VARIABLE listed)
    if(NOT listed EQUAL 0)
        message(FATAL_ERROR "CheckAllocatorBoundary: objdump failed on ${binary}: ${listing_error}")
    endif()
    gui_forms_pe_exports("${listing}" exports)
    gui_forms_pe_imports("${listing}" imports)
    # Every one of these images imports something (KERNEL32 at least): an
    # empty table means the listing was not read.
    if(NOT imports)
        message(FATAL_ERROR "CheckAllocatorBoundary: no imports read from ${binary}")
    endif()
    list(LENGTH exports export_count)
    list(LENGTH imports import_count)
    set(found 0)
    foreach(name IN LISTS exports)
        if(name MATCHES "${heap_pattern}" OR name MATCHES "${operator_pattern}")
            string(APPEND boundary_failures "  ${binary} exports ${name}\n")
            math(EXPR found "${found} + 1")
        endif()
    endforeach()
    foreach(entry IN LISTS imports)
        string(FIND "${entry}" "|" bar)
        string(SUBSTRING "${entry}" 0 ${bar} dll)
        math(EXPR name_start "${bar} + 1")
        string(SUBSTRING "${entry}" ${name_start} -1 name)
        string(TOLOWER "${dll}" dll_lower)
        if(name MATCHES "${operator_pattern}")
            string(APPEND boundary_failures "  ${binary} imports ${name} from ${dll}\n")
            math(EXPR found "${found} + 1")
        elseif(name MATCHES "${heap_pattern}" AND NOT dll_lower MATCHES "${system_crt_pattern}")
            string(APPEND boundary_failures "  ${binary} imports ${name} from ${dll}, not the system CRT\n")
            math(EXPR found "${found} + 1")
        elseif(DEFINED provider_lower AND dll_lower STREQUAL provider_lower)
            list(FIND provider_accepted "${name}" position)
            if(position EQUAL -1)
                string(APPEND boundary_failures "  ${binary} imports ${name} from ${dll}, outside its entry points\n")
                math(EXPR found "${found} + 1")
            endif()
        endif()
    endforeach()
    message(STATUS "${binary}: ${export_count} exports, ${import_count} imports, ${found} crossing the allocator boundary")
endforeach()

if(NOT boundary_failures STREQUAL "")
    message(FATAL_ERROR "CheckAllocatorBoundary: allocation crosses a module boundary:\n${boundary_failures}")
endif()
message(STATUS "CheckAllocatorBoundary: every module allocates and frees through its own runtime or the system CRT")
