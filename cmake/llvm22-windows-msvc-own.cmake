# The MSVC target's "own runtime" mode (GUI_FORMS_MSVC_RUNTIME=OWN), read by
# cmake/llvm22-windows-msvc.cmake after it has found the LLVM tools: the
# no-MinGW switch (owner, 2026-11-17; projects desks/coordinator/
# NO_MINGW_SWITCH.md). Every Microsoft-runtime-shaped piece is STXlib's:
#   - the C library headers and the UCRT's export list
#     (src/hosted/windows/crt/include, crt/ucrtbase.def);
#   - the Windows API headers and one export list per system DLL
#     (src/hosted/windows/api/include, api/lib/<dll>.def);
#   - the startup objects (src/hosted/windows_msvc_startup) and the C++
#     runtime (src/hosted/windows/msvc_runtime);
#   - libc++ 22, static, built against those headers with
#     _LIBCPP_NO_VCRUNTIME (tools/windows_libcxx).
# The one system C library is the UCRT DLL (ucrtbase.dll). No Visual C++
# tools, Windows SDK or MinGW folder is searched for a header or a library;
# only the compiler binaries and clang's own resource headers come from the
# pinned toolchain folder.
#
# Settings (cache or -D; all forwarded to try_compile):
#   GUI_FORMS_OWN_RUNTIME_SOURCE  the STXlib source holding the headers and
#                                 the export lists (required);
#   GUI_FORMS_OWN_RUNTIME_PREFIX  the build tree of STXlib's
#                                 src/hosted/windows/own_runtime project: its
#                                 lib/ holds the startup and runtime archives
#                                 and the import libraries made from the
#                                 export lists. Empty is the bootstrap: the
#                                 headers only, for building that project
#                                 and libc++ themselves (nothing is linked,
#                                 and try_compile builds static libraries);
#   GUI_FORMS_LIBCXX_PREFIX       libc++'s install (headers and archives);
#                                 empty while libc++ itself is built;
#   GUI_FORMS_LINK_MAP            ON writes a link map beside every image
#                                 (lld-link /lldmap: every input by its path), the evidence that nothing
#                                 was read from a Microsoft or MinGW folder.
#
# Include order (the switch's contract): libc++'s headers, clang's resource
# headers, the C library headers, the Windows API headers. -nostdinc drops
# every directory the driver would add (the tools', the SDK's, INCLUDE from
# the environment, and the resource directory, which is named again).
#
# Link: -nostdlib (no libcmt or oldnames default library), /NODEFAULTLIB
# (every /DEFAULTLIB directive in an object is ignored, so a library is read
# only when it is named), and every archive and import library in the
# prefix's lib/ named on each link. The driver still passes lld-link
# library directories inside the Visual C++ tools and the Windows SDK it
# detects, even with -nostdlib, so both roots are pointed at an empty
# directory of this build; only clang's own resource lib/windows directory
# stays on the search path, and no library of that name is ever asked for.

set(GUI_FORMS_OWN_RUNTIME_SOURCE "${GUI_FORMS_OWN_RUNTIME_SOURCE}" CACHE PATH
    "STXlib source with the C library and Windows API headers and export lists (cmake/llvm22-windows-msvc-own.cmake)")
set(GUI_FORMS_OWN_RUNTIME_PREFIX "${GUI_FORMS_OWN_RUNTIME_PREFIX}" CACHE PATH
    "STXlib's own_runtime build tree (startup, runtime, import libraries); empty for the bootstrap")
if(NOT GUI_FORMS_LIBCXX_PREFIX AND DEFINED ENV{GUI_FORMS_LIBCXX_PREFIX})
    file(TO_CMAKE_PATH "$ENV{GUI_FORMS_LIBCXX_PREFIX}" GUI_FORMS_LIBCXX_PREFIX)
endif()
set(GUI_FORMS_LIBCXX_PREFIX "${GUI_FORMS_LIBCXX_PREFIX}" CACHE PATH
    "libc++ 22 for x86_64-pc-windows-msvc (static, _LIBCPP_NO_VCRUNTIME); empty while it is built")
option(GUI_FORMS_LINK_MAP "Write a link map beside every image (lld-link /lldmap)" OFF)

set(_gf_own_crt_include "${GUI_FORMS_OWN_RUNTIME_SOURCE}/src/hosted/windows/crt/include")
set(_gf_own_api_include "${GUI_FORMS_OWN_RUNTIME_SOURCE}/src/hosted/windows/api/include")
if(NOT EXISTS "${_gf_own_crt_include}/stdlib.h")
    message(FATAL_ERROR "GUI_FORMS_MSVC_RUNTIME=OWN: no C library headers at ${_gf_own_crt_include} "
                        "(set GUI_FORMS_OWN_RUNTIME_SOURCE to an STXlib source with src/hosted/windows/crt)")
endif()
if(NOT EXISTS "${_gf_own_api_include}/windows.h")
    message(FATAL_ERROR "GUI_FORMS_MSVC_RUNTIME=OWN: no Windows API headers at ${_gf_own_api_include}")
endif()

# clang's resource directory (its stddef.h, stdarg.h, intrinsics), which
# -nostdinc drops with everything else.
execute_process(COMMAND "${_gf_clang}" --target=${GUI_FORMS_WINDOWS_TARGET} -print-resource-dir
    OUTPUT_VARIABLE _gf_resource_dir OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _gf_resource_result)
if(NOT _gf_resource_result EQUAL 0 OR NOT EXISTS "${_gf_resource_dir}/include/stddef.h")
    message(FATAL_ERROR "clang reports no resource directory (${_gf_resource_dir})")
endif()
file(TO_CMAKE_PATH "${_gf_resource_dir}" _gf_resource_dir)

# --- compile ------------------------------------------------------------------
set(_gf_own_c_includes
    "-nostdinc -isystem \"${_gf_resource_dir}/include\" -isystem \"${_gf_own_crt_include}\" -isystem \"${_gf_own_api_include}\"")
set(CMAKE_C_FLAGS_INIT "${_gf_own_c_includes}")
set(CMAKE_ASM_FLAGS_INIT "${_gf_own_c_includes}")
if(GUI_FORMS_LIBCXX_PREFIX)
    if(NOT EXISTS "${GUI_FORMS_LIBCXX_PREFIX}/include/c++/v1/__config_site")
        message(FATAL_ERROR "No libc++ headers at ${GUI_FORMS_LIBCXX_PREFIX}/include/c++/v1")
    endif()
    file(STRINGS "${GUI_FORMS_LIBCXX_PREFIX}/include/c++/v1/__config_site" _gf_no_vcruntime
         REGEX "^#define _LIBCPP_NO_VCRUNTIME")
    if(NOT _gf_no_vcruntime)
        message(FATAL_ERROR "GUI_FORMS_MSVC_RUNTIME=OWN needs a libc++ built with _LIBCPP_NO_VCRUNTIME "
                            "(${GUI_FORMS_LIBCXX_PREFIX})")
    endif()
    # libc++ first, then the C search order above. The archives are named on
    # the link by path, so libc++'s automatic library directive is off.
    # -nostdinc (in the C flags) already drops the C++ directories too; a
    # -nostdinc++ beside it is reported unused, an error under
    # GUI_FORMS_WARNINGS_AS_ERRORS.
    set(CMAKE_CXX_FLAGS_INIT
        "-isystem \"${GUI_FORMS_LIBCXX_PREFIX}/include/c++/v1\" ${_gf_own_c_includes} -D_LIBCPP_NO_AUTO_LINK")
else()
    # The bootstrap that builds libc++ names libc++'s own headers itself.
    set(CMAKE_CXX_FLAGS_INIT "${_gf_own_c_includes}")
endif()
# Resource scripts: CMake preprocesses them with clang and compiles them with
# llvm-rc, passing the same flags to both, so the headers are given as -I.
set(CMAKE_RC_FLAGS_INIT "-I \"${_gf_own_api_include}\" -I \"${_gf_own_crt_include}\"")
# No runtime-library choice: CMake would otherwise add -D_DLL, -D_MT and a
# --dependent-lib directive for Microsoft's msvcrt (Platform/Windows-Clang).
set(CMAKE_MSVC_RUNTIME_LIBRARY "" CACHE STRING "No Microsoft runtime library (own runtime)" FORCE)

# --- link ---------------------------------------------------------------------
# The empty directory both detected roots are pointed at (see above).
set(_gf_no_microsoft_root "${CMAKE_BINARY_DIR}/gui_forms_no_microsoft_root")
file(MAKE_DIRECTORY "${_gf_no_microsoft_root}")
set(_gf_link "-fuse-ld=lld -nostdlib")
string(APPEND _gf_link " -Xmicrosoft-visualc-tools-root \"${_gf_no_microsoft_root}\"")
string(APPEND _gf_link " -Xmicrosoft-windows-sdk-root \"${_gf_no_microsoft_root}\" -Xmicrosoft-windows-sdk-version 10.0.0.0")
string(APPEND _gf_link " -Xlinker /NODEFAULTLIB")
set(_gf_own_libraries "")
if(GUI_FORMS_OWN_RUNTIME_PREFIX)
    set(_gf_own_lib "${GUI_FORMS_OWN_RUNTIME_PREFIX}/lib")
    foreach(_gf_required IN ITEMS stx_msvc_startup.lib stxlib_msvc_runtime_static.lib
                                  stxlib_msvc_runtime_module.lib ucrtbase.lib kernel32.lib)
        if(NOT EXISTS "${_gf_own_lib}/${_gf_required}")
            message(FATAL_ERROR "GUI_FORMS_MSVC_RUNTIME=OWN: ${_gf_own_lib} has no ${_gf_required} "
                                "(build STXlib's src/hosted/windows/own_runtime into GUI_FORMS_OWN_RUNTIME_PREFIX)")
        endif()
    endforeach()
    if(NOT GUI_FORMS_LIBCXX_PREFIX OR NOT EXISTS "${GUI_FORMS_LIBCXX_PREFIX}/lib/libc++.lib")
        message(FATAL_ERROR "GUI_FORMS_MSVC_RUNTIME=OWN links a static libc++: set GUI_FORMS_LIBCXX_PREFIX")
    endif()
    # A target's own system libraries (-lbcrypt, mincore, uiautomationcore)
    # are found here by name.
    string(APPEND _gf_link " -Xlinker \"/LIBPATH:${_gf_own_lib}\"")
    # Everything the prefix built: the startup objects, the runtime, its
    # per-image pieces, builtins, the static GUID data and every import
    # library. lld-link reads an archive member only for a symbol still
    # undefined, so naming them all adds nothing an image does not use.
    file(GLOB _gf_own_archives LIST_DIRECTORIES false "${_gf_own_lib}/*.lib")
    list(SORT _gf_own_archives)
    foreach(_gf_archive IN LISTS _gf_own_archives)
        string(APPEND _gf_own_libraries " \"${_gf_archive}\"")
    endforeach()
    file(GLOB _gf_libcxx_archives LIST_DIRECTORIES false "${GUI_FORMS_LIBCXX_PREFIX}/lib/*.lib")
    list(SORT _gf_libcxx_archives)
    set(_gf_libcxx_libraries "")
    foreach(_gf_archive IN LISTS _gf_libcxx_archives)
        string(APPEND _gf_libcxx_libraries " \"${_gf_archive}\"")
    endforeach()
    # The platform module replaces the _INIT values with its own Windows
    # library list (Platform/Windows-Clang.cmake), so the cache values are
    # given here.
    set(CMAKE_CXX_STANDARD_LIBRARIES "${_gf_libcxx_libraries}${_gf_own_libraries}"
        CACHE STRING "libc++, STXlib's runtime and the import libraries (cmake/llvm22-windows-msvc-own.cmake)" FORCE)
    set(CMAKE_C_STANDARD_LIBRARIES "${_gf_own_libraries}"
        CACHE STRING "STXlib's runtime and the import libraries (cmake/llvm22-windows-msvc-own.cmake)" FORCE)
    if(GUI_FORMS_LINK_MAP)
        string(APPEND _gf_link " -Xlinker /lldmap")
    endif()
else()
    # The bootstrap links nothing: configure checks compile static libraries.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "Nothing is linked in the bootstrap" FORCE)
    set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "Nothing is linked in the bootstrap" FORCE)
endif()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_gf_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_gf_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_gf_link}")

set(GUI_FORMS_STATIC_CXX_RUNTIME ON CACHE BOOL "The C++ library is linked statically (cmake/llvm22-windows-msvc.cmake)" FORCE)
set(GUI_FORMS_TEMPORARY_MSVC_STL OFF CACHE BOOL "Temporary: Microsoft's STL until libc++ is built for this target" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
