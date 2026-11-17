# The Windows MSVC target, x86_64-pc-windows-msvc, compiled by the pinned
# LLVM 22 clang (never the MSVC compiler) and linked by lld-link (owner
# decision, 2026-11-15). Pass to the provider and the consumer:
#   -DCMAKE_TOOLCHAIN_FILE=.../llvm22-windows-msvc.cmake
#
# Stage B (this file today): Microsoft's startup objects (always static)
# and vcruntime over the UCRT DLL, the one dynamic system C library. Two
# runtime forms, GUI_FORMS_MSVC_VCRUNTIME:
#   DLL (default; the EH runtime study's Stage 0, MSVC_ABI_EH_RUNTIME_STUDY.md
#     section 6): /MD objects, vcruntime140.dll and the UCRT DLL imported;
#   STATIC: the "hybrid" runtime, /MT objects (libcmt's startup,
#     libvcruntime) with the static UCRT exchanged for the UCRT DLL's import
#     library at the link, so a program imports no runtime DLL but the UCRT.
# The C++ library is LLVM's libc++ 22, static (GUI_FORMS_LIBCXX_PREFIX,
# below), or temporarily Microsoft's STL (msvcp140.dll with DLL, libcpmt with
# STATIC) where no libc++ for this target is given. Stage C replaces the startup objects and vcruntime with
# an own minimal C runtime over UCRT.
#
# The Visual C++ tools and the Windows SDK are located, never hard-coded:
# the Visual Studio installation by vswhere (the newest one carrying the x64
# tools), its default tools version from
# VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt, and the SDK root
# and version from the registry key the SDK installer writes
# (HKLM\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0, 32-bit view:
# InstallationFolder and ProductVersion). GUI_FORMS_MSVC_TOOLS_DIR,
# GUI_FORMS_WINDOWS_SDK_DIR and GUI_FORMS_WINDOWS_SDK_VERSION override them.
#
# The driver receives the locations as -Xmicrosoft-visualc-tools-root,
# -Xmicrosoft-windows-sdk-root and -Xmicrosoft-windows-sdk-version, the GNU
# driver's spelling of clang-cl's /vctoolsdir, /winsdkdir and /winsdkversion.
# It then searches clang's own resource headers first and the tools' and the
# SDK's include directories (VC include, ucrt, shared, um, winrt, cppwinrt)
# after them, as system headers, and passes the matching library directories
# to lld-link. Plain -isystem flags would put the tools' intrin.h and
# stddef.h ahead of clang's. Neither INCLUDE nor LIB from the environment is
# read once the roots are given.

# Not a cross build: the host is Windows x64 and runs what this target
# builds (the form compiler, the tests), so CMAKE_SYSTEM_NAME is left unset
# (setting it would mark the build as cross compiling).
set(GUI_FORMS_WINDOWS_TARGET "x86_64-pc-windows-msvc")

# CC/CXX names intentionally match ccache's content-checked compiler identity
# (cmake/llvm22.cmake).
find_program(_gf_clang NAMES clang REQUIRED)
find_program(_gf_clangxx NAMES clang++ REQUIRED)
get_filename_component(_gf_llvm_bin "${_gf_clang}" DIRECTORY)
find_program(_gf_lld_link NAMES lld-link HINTS "${_gf_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(_gf_llvm_ar NAMES llvm-ar HINTS "${_gf_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(_gf_llvm_ranlib NAMES llvm-ranlib HINTS "${_gf_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(_gf_llvm_lib NAMES llvm-lib HINTS "${_gf_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(_gf_llvm_rc NAMES llvm-rc HINTS "${_gf_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(_gf_llvm_mt NAMES llvm-mt HINTS "${_gf_llvm_bin}" NO_DEFAULT_PATH)
set(CMAKE_C_COMPILER "${_gf_clang}" CACHE FILEPATH "LLVM C driver")
set(CMAKE_CXX_COMPILER "${_gf_clangxx}" CACHE FILEPATH "LLVM C++ driver")
set(CMAKE_C_COMPILER_TARGET "${GUI_FORMS_WINDOWS_TARGET}")
set(CMAKE_CXX_COMPILER_TARGET "${GUI_FORMS_WINDOWS_TARGET}")
set(CMAKE_LINKER "${_gf_lld_link}" CACHE FILEPATH "LLVM COFF linker")
set(CMAKE_LINKER_TYPE LLD)
# CMake's rules for a GNU-style clang driver on this target archive with
# ar's command line (Platform/Windows-Clang.cmake: "<CMAKE_AR> qc"), so the
# archiver is llvm-ar; its COFF archives are what llvm-lib writes.
# llvm-lib makes the import libraries from .def files (GUI_FORMS_LLVM_LIB).
set(CMAKE_AR "${_gf_llvm_ar}" CACHE FILEPATH "LLVM archiver")
set(CMAKE_RANLIB "${_gf_llvm_ranlib}" CACHE FILEPATH "LLVM archive indexer")
set(CMAKE_RC_COMPILER "${_gf_llvm_rc}" CACHE FILEPATH "LLVM resource compiler")
if(_gf_llvm_mt)
    set(CMAKE_MT "${_gf_llvm_mt}" CACHE FILEPATH "LLVM manifest tool")
endif()
set(GUI_FORMS_LLVM_LIB "${_gf_llvm_lib}" CACHE FILEPATH "LLVM import library writer")

# --- the Visual C++ tools ----------------------------------------------------
if(DEFINED ENV{GUI_FORMS_MSVC_TOOLS_DIR})
    file(TO_CMAKE_PATH "$ENV{GUI_FORMS_MSVC_TOOLS_DIR}" _gf_vc_tools)
else()
    set(_gf_program_files_x86 "$ENV{ProgramFiles\(x86\)}")
    if(NOT _gf_program_files_x86)
        set(_gf_program_files_x86 "C:/Program Files (x86)")
    endif()
    file(TO_CMAKE_PATH "${_gf_program_files_x86}/Microsoft Visual Studio/Installer/vswhere.exe" _gf_vswhere)
    if(NOT EXISTS "${_gf_vswhere}")
        message(FATAL_ERROR "vswhere.exe not found at ${_gf_vswhere}: install Visual Studio with the "
                            "x64 C++ tools, or set GUI_FORMS_MSVC_TOOLS_DIR")
    endif()
    execute_process(
        COMMAND "${_gf_vswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
                -property installationPath -utf8
        OUTPUT_VARIABLE _gf_vs_root OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _gf_vswhere_result)
    if(NOT _gf_vswhere_result EQUAL 0 OR NOT _gf_vs_root)
        message(FATAL_ERROR "vswhere found no Visual Studio with the x64 C++ tools")
    endif()
    file(TO_CMAKE_PATH "${_gf_vs_root}" _gf_vs_root)
    file(STRINGS "${_gf_vs_root}/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" _gf_vc_version LIMIT_COUNT 1)
    string(STRIP "${_gf_vc_version}" _gf_vc_version)
    set(_gf_vc_tools "${_gf_vs_root}/VC/Tools/MSVC/${_gf_vc_version}")
endif()
if(NOT EXISTS "${_gf_vc_tools}/include/vcruntime.h" OR NOT EXISTS "${_gf_vc_tools}/lib/x64/libvcruntime.lib")
    message(FATAL_ERROR "No x64 Visual C++ tools at ${_gf_vc_tools}")
endif()

# --- the Windows SDK -----------------------------------------------------------
if(DEFINED ENV{GUI_FORMS_WINDOWS_SDK_DIR})
    file(TO_CMAKE_PATH "$ENV{GUI_FORMS_WINDOWS_SDK_DIR}" _gf_sdk_root)
else()
    cmake_host_system_information(RESULT _gf_sdk_root QUERY WINDOWS_REGISTRY
        "HKLM/SOFTWARE/Microsoft/Microsoft SDKs/Windows/v10.0" VALUE InstallationFolder VIEW 32)
    file(TO_CMAKE_PATH "${_gf_sdk_root}" _gf_sdk_root)
endif()
string(REGEX REPLACE "/$" "" _gf_sdk_root "${_gf_sdk_root}")
if(DEFINED ENV{GUI_FORMS_WINDOWS_SDK_VERSION})
    set(_gf_sdk_version "$ENV{GUI_FORMS_WINDOWS_SDK_VERSION}")
else()
    cmake_host_system_information(RESULT _gf_sdk_product QUERY WINDOWS_REGISTRY
        "HKLM/SOFTWARE/Microsoft/Microsoft SDKs/Windows/v10.0" VALUE ProductVersion VIEW 32)
    # The registry gives three fields (10.0.28000); the directories carry four.
    set(_gf_sdk_version "${_gf_sdk_product}.0")
endif()
if(NOT EXISTS "${_gf_sdk_root}/Include/${_gf_sdk_version}/um/windows.h"
   OR NOT EXISTS "${_gf_sdk_root}/Lib/${_gf_sdk_version}/ucrt/x64/ucrt.lib")
    message(FATAL_ERROR "No Windows SDK ${_gf_sdk_version} at ${_gf_sdk_root}")
endif()
set(GUI_FORMS_MSVC_TOOLS_DIR "${_gf_vc_tools}" CACHE PATH "Visual C++ tools (located by vswhere)" FORCE)
set(GUI_FORMS_WINDOWS_SDK_DIR "${_gf_sdk_root}" CACHE PATH "Windows SDK root (from the registry)" FORCE)
set(GUI_FORMS_WINDOWS_SDK_VERSION "${_gf_sdk_version}" CACHE STRING "Windows SDK version" FORCE)

# --- flags ---------------------------------------------------------------------
set(_gf_roots "-Xmicrosoft-visualc-tools-root \"${_gf_vc_tools}\" -Xmicrosoft-windows-sdk-root \"${_gf_sdk_root}\" -Xmicrosoft-windows-sdk-version ${_gf_sdk_version}")
set(CMAKE_C_FLAGS_INIT "${_gf_roots}")
set(CMAKE_CXX_FLAGS_INIT "${_gf_roots}")
# Resource scripts: CMake preprocesses them with clang and compiles them
# with llvm-rc, passing the same flags to both, so the SDK's and the tools'
# include directories are given as -I, which both read.
set(_gf_sdk_include "${_gf_sdk_root}/Include/${_gf_sdk_version}")
set(CMAKE_RC_FLAGS_INIT "-I \"${_gf_sdk_include}/um\" -I \"${_gf_sdk_include}/shared\" -I \"${_gf_sdk_include}/ucrt\" -I \"${_gf_vc_tools}/include\"")
# CMake spells /MD and /MT for this driver as -D_DLL/-D_MT and
# --dependent-lib (CMAKE_MSVC_RUNTIME_LIBRARY, Platform/Windows-Clang.cmake).
# -rtlib=platform: the toolchain's compiler-rt builtins are MinGW's (built
# with the stack protector against MinGW's CRT), so none is named here.
set(GUI_FORMS_MSVC_VCRUNTIME "DLL" CACHE STRING "vcruntime form: DLL or STATIC (cmake/llvm22-windows-msvc.cmake)")
set(_gf_link "-rtlib=platform ${_gf_roots}")
if(GUI_FORMS_MSVC_VCRUNTIME STREQUAL "STATIC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "The static Microsoft runtime objects" FORCE)
    string(APPEND _gf_link " -Xlinker /NODEFAULTLIB:libucrt.lib -Xlinker /DEFAULTLIB:ucrt.lib")
    set(_gf_static_cxx ON)
elseif(GUI_FORMS_MSVC_VCRUNTIME STREQUAL "DLL")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "The DLL Microsoft runtime" FORCE)
    set(_gf_static_cxx OFF)
else()
    message(FATAL_ERROR "GUI_FORMS_MSVC_VCRUNTIME is DLL or STATIC, not ${GUI_FORMS_MSVC_VCRUNTIME}")
endif()
# The C++ library. GUI_FORMS_LIBCXX_PREFIX (cache or environment) names an
# install of LLVM's libc++ 22 built static for this target with
# LIBCXX_CXX_ABI=vcruntime (Stage 0: its std::exception, type_info and
# operator new come from vcruntime and the startup objects); its headers
# replace the tools' STL headers (-nostdinc++ keeps the tools' include
# directory, which also holds vcruntime's own headers) and its archives come
# last on every C++ link. Without it the build uses Microsoft's STL, marked
# temporary (GUI_FORMS_TEMPORARY_MSVC_STL).
if(NOT GUI_FORMS_LIBCXX_PREFIX AND DEFINED ENV{GUI_FORMS_LIBCXX_PREFIX})
    file(TO_CMAKE_PATH "$ENV{GUI_FORMS_LIBCXX_PREFIX}" GUI_FORMS_LIBCXX_PREFIX)
endif()
set(GUI_FORMS_LIBCXX_PREFIX "${GUI_FORMS_LIBCXX_PREFIX}" CACHE PATH "libc++ 22 for x86_64-pc-windows-msvc (static, vcruntime ABI)")
if(GUI_FORMS_LIBCXX_PREFIX)
    if(NOT EXISTS "${GUI_FORMS_LIBCXX_PREFIX}/include/c++/v1/__config_site"
       OR NOT EXISTS "${GUI_FORMS_LIBCXX_PREFIX}/lib/libc++.lib")
        message(FATAL_ERROR "No static libc++ for this target at ${GUI_FORMS_LIBCXX_PREFIX}")
    endif()
    string(APPEND CMAKE_CXX_FLAGS_INIT " -nostdinc++ -isystem \"${GUI_FORMS_LIBCXX_PREFIX}/include/c++/v1\"")
    # libc++ is built with UCRT's ISO wide printf specifiers, and the UCRT
    # headers record the choice as a /FAILIFMISMATCH directive in every
    # object that includes them, C as well as C++.
    string(APPEND CMAKE_C_FLAGS_INIT " -D_CRT_STDIO_ISO_WIDE_SPECIFIERS")
    # The archives are named on the link by path, so libc++'s automatic
    # "#pragma comment(lib, \"libc++.lib\")" (its __config) is turned off.
    string(APPEND CMAKE_CXX_FLAGS_INIT " -D_CRT_STDIO_ISO_WIDE_SPECIFIERS -D_LIBCPP_NO_AUTO_LINK")
    # Stage 0: libc++'s std::exception_ptr on the vcruntime ABI calls
    # Microsoft's __ExceptionPtr* functions, which only the STL's runtime
    # exports (msvcp140.dll, or libcpmt for STATIC); the linker takes them
    # from there when a program keeps exception_ptr (the EH runtime study,
    # section 4.2). Stage 2's own runtime replaces them.
    if(GUI_FORMS_MSVC_VCRUNTIME STREQUAL "STATIC")
        string(APPEND _gf_link " -Xlinker /DEFAULTLIB:libcpmt.lib")
    else()
        string(APPEND _gf_link " -Xlinker /DEFAULTLIB:msvcprt.lib")
    endif()
    # The platform module replaces CMAKE_CXX_STANDARD_LIBRARIES_INIT with
    # its Windows system libraries (Platform/Windows-Clang.cmake), so the
    # cache value is given here: the archives, then those same libraries.
    set(CMAKE_CXX_STANDARD_LIBRARIES
        "\"${GUI_FORMS_LIBCXX_PREFIX}/lib/libc++.lib\" \"${GUI_FORMS_LIBCXX_PREFIX}/lib/libc++experimental.lib\" -lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -loldnames"
        CACHE STRING "libc++ and the Windows system libraries (cmake/llvm22-windows-msvc.cmake)" FORCE)
    set(_gf_static_cxx ON)
    set(_gf_temporary_stl OFF)
else()
    set(_gf_temporary_stl ON)
endif()
# The startup objects and the C++ exception runtime (the EH runtime study's
# stages, projects desks/coordinator/MSVC_ABI_EH_RUNTIME_STUDY.md, section 6).
# - GUI_FORMS_MSVC_STARTUP=VCRUNTIME (Stage 0): Microsoft's startup objects
#   from msvcrt.lib (or libcmt.lib for STATIC), as the driver names them.
# - GUI_FORMS_MSVC_STARTUP=STXLIB (Stage 1): STXlib's startup shim
#   (src/hosted/windows_msvc_startup), installed at
#   GUI_FORMS_MSVC_STARTUP_PREFIX: entry points, initializer tables, TLS,
#   thread-safe statics, the cookie, the load configuration, __chkstk and
#   atexit over the UCRT's documented exports. Microsoft's startup
#   libraries are refused with /NODEFAULTLIB; the UCRT's import library is
#   named instead. With a vcruntime-mode libc++ the shim's operator new and
#   delete archive is linked too.
# - GUI_FORMS_MSVC_EH_RUNTIME=VCRUNTIME (Stages 0 and 1): the exception
#   runtime, RTTI, terminate and the string group from vcruntime140.dll.
# - GUI_FORMS_MSVC_EH_RUNTIME=STXLIB (Stage 2): STXlib's own runtime, the
#   archives listed in GUI_FORMS_MSVC_EH_RUNTIME_LIBRARIES; vcruntime and
#   the STL's runtime are refused, and libc++ must be built with
#   _LIBCPP_NO_VCRUNTIME so that it owns std::exception, type_info and
#   operator new. Requires the STXLIB startup.
set(GUI_FORMS_MSVC_STARTUP "VCRUNTIME" CACHE STRING "Startup objects: VCRUNTIME (Microsoft's) or STXLIB (cmake/llvm22-windows-msvc.cmake)")
set(GUI_FORMS_MSVC_STARTUP_PREFIX "" CACHE PATH "STXlib's MSVC-target startup shim install (GUI_FORMS_MSVC_STARTUP=STXLIB)")
set(GUI_FORMS_MSVC_EH_RUNTIME "VCRUNTIME" CACHE STRING "C++ exception runtime: VCRUNTIME or STXLIB (cmake/llvm22-windows-msvc.cmake)")
set(GUI_FORMS_MSVC_EH_RUNTIME_LIBRARIES "" CACHE STRING "STXlib's MSVC-target exception runtime archives (GUI_FORMS_MSVC_EH_RUNTIME=STXLIB)")
set(_gf_runtime_libraries "")
if(GUI_FORMS_MSVC_STARTUP STREQUAL "STXLIB")
    if(NOT GUI_FORMS_MSVC_VCRUNTIME STREQUAL "DLL")
        message(FATAL_ERROR "GUI_FORMS_MSVC_STARTUP=STXLIB links the UCRT as a DLL: set GUI_FORMS_MSVC_VCRUNTIME=DLL")
    endif()
    if(NOT EXISTS "${GUI_FORMS_MSVC_STARTUP_PREFIX}/lib/stx_msvc_startup.lib")
        message(FATAL_ERROR "No STXlib startup shim at ${GUI_FORMS_MSVC_STARTUP_PREFIX} (lib/stx_msvc_startup.lib)")
    endif()
    string(APPEND _gf_link " -Xlinker /NODEFAULTLIB:msvcrt.lib -Xlinker /NODEFAULTLIB:libcmt.lib -Xlinker /NODEFAULTLIB:msvcrtd.lib -Xlinker /NODEFAULTLIB:libcmtd.lib")
    string(APPEND _gf_runtime_libraries " \"${GUI_FORMS_MSVC_STARTUP_PREFIX}/lib/stx_msvc_startup.lib\"")
    set(_gf_libcxx_vcruntime ON)
    if(GUI_FORMS_LIBCXX_PREFIX)
        file(STRINGS "${GUI_FORMS_LIBCXX_PREFIX}/include/c++/v1/__config_site" _gf_no_vcruntime REGEX "^#define _LIBCPP_NO_VCRUNTIME")
        if(_gf_no_vcruntime)
            set(_gf_libcxx_vcruntime OFF)
        endif()
    endif()
    if(_gf_libcxx_vcruntime)
        string(APPEND _gf_runtime_libraries " \"${GUI_FORMS_MSVC_STARTUP_PREFIX}/lib/stx_msvc_startup_vcruntime_mode.lib\"")
    endif()
    if(GUI_FORMS_MSVC_EH_RUNTIME STREQUAL "STXLIB")
        if(_gf_libcxx_vcruntime)
            message(FATAL_ERROR "GUI_FORMS_MSVC_EH_RUNTIME=STXLIB needs a libc++ built with _LIBCPP_NO_VCRUNTIME")
        endif()
        if(GUI_FORMS_MSVC_EH_RUNTIME_LIBRARIES STREQUAL "")
            message(FATAL_ERROR "GUI_FORMS_MSVC_EH_RUNTIME=STXLIB needs GUI_FORMS_MSVC_EH_RUNTIME_LIBRARIES")
        endif()
        string(APPEND _gf_link " -Xlinker /NODEFAULTLIB:vcruntime.lib -Xlinker /NODEFAULTLIB:libvcruntime.lib -Xlinker /NODEFAULTLIB:msvcprt.lib -Xlinker /NODEFAULTLIB:libcpmt.lib")
        string(REPLACE " -Xlinker /DEFAULTLIB:msvcprt.lib" "" _gf_link "${_gf_link}")
        foreach(_gf_eh_library IN LISTS GUI_FORMS_MSVC_EH_RUNTIME_LIBRARIES)
            string(APPEND _gf_runtime_libraries " \"${_gf_eh_library}\"")
        endforeach()
    elseif(GUI_FORMS_MSVC_EH_RUNTIME STREQUAL "VCRUNTIME")
        string(APPEND _gf_runtime_libraries " vcruntime.lib")
    else()
        message(FATAL_ERROR "GUI_FORMS_MSVC_EH_RUNTIME is VCRUNTIME or STXLIB, not ${GUI_FORMS_MSVC_EH_RUNTIME}")
    endif()
    string(APPEND _gf_runtime_libraries " ucrt.lib")
    if(GUI_FORMS_LIBCXX_PREFIX)
        set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES}${_gf_runtime_libraries}"
            CACHE STRING "libc++, the runtime and the Windows system libraries (cmake/llvm22-windows-msvc.cmake)" FORCE)
    endif()
    set(CMAKE_C_STANDARD_LIBRARIES "-lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -loldnames${_gf_runtime_libraries}"
        CACHE STRING "The runtime and the Windows system libraries (cmake/llvm22-windows-msvc.cmake)" FORCE)
elseif(NOT GUI_FORMS_MSVC_STARTUP STREQUAL "VCRUNTIME")
    message(FATAL_ERROR "GUI_FORMS_MSVC_STARTUP is VCRUNTIME or STXLIB, not ${GUI_FORMS_MSVC_STARTUP}")
elseif(GUI_FORMS_MSVC_EH_RUNTIME STREQUAL "STXLIB")
    message(FATAL_ERROR "GUI_FORMS_MSVC_EH_RUNTIME=STXLIB needs GUI_FORMS_MSVC_STARTUP=STXLIB")
endif()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_gf_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_gf_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_gf_link}")
set(GUI_FORMS_STATIC_CXX_RUNTIME ${_gf_static_cxx} CACHE BOOL "The C++ library is linked statically (cmake/llvm22-windows-msvc.cmake)" FORCE)
set(GUI_FORMS_TEMPORARY_MSVC_STL ${_gf_temporary_stl} CACHE BOOL "Temporary: Microsoft's STL until libc++ is built for this target" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
