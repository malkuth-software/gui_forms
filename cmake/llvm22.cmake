# Pass to the provider AND consumer: -DCMAKE_TOOLCHAIN_FILE=.../llvm22.cmake.
# CC/CXX names intentionally match ccache's content-checked compiler identity.
find_program(_gf_clang NAMES clang REQUIRED)
find_program(_gf_clangxx NAMES clang++ REQUIRED)
set(CMAKE_C_COMPILER "${_gf_clang}" CACHE FILEPATH "LLVM C driver")
set(CMAKE_CXX_COMPILER "${_gf_clangxx}" CACHE FILEPATH "LLVM C++ driver")
set(CMAKE_OBJCXX_COMPILER "${_gf_clangxx}" CACHE FILEPATH "LLVM Objective-C++ driver")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _gf_sdk
                    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(CMAKE_OSX_SYSROOT "${_gf_sdk}" CACHE PATH "Selected Xcode SDK")
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Supported Apple architecture")
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum supported macOS")
    if(NOT DEFINED ENV{GUI_FORMS_LLVM_RUNTIME})
        message(FATAL_ERROR "Set GUI_FORMS_LLVM_RUNTIME to the LLVM 22.1.8 macOS 14 runtime prefix")
    endif()
    file(TO_CMAKE_PATH "$ENV{GUI_FORMS_LLVM_RUNTIME}" _gf_runtime)
    set(_gf_cxx "-stdlib=libc++ -nostdinc++ -isystem \"${_gf_runtime}/include/c++/v1\"")
    set(CMAKE_CXX_FLAGS_INIT "${_gf_cxx}")
    set(CMAKE_OBJCXX_FLAGS_INIT "${_gf_cxx}")
    set(_gf_link "-L\"${_gf_runtime}/lib\" -Wl,-rpath,\"${_gf_runtime}/lib\" -lunwind")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_gf_link}")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    # Linux builds against LLVM's libc++ 22 and libc++abi, as macOS and
    # Windows do (owner decision, 2026-11-15; STXlib's Linux CI is the
    # model), and links them statically, with LLVM's unwinder, as Windows
    # does: a shipped program depends on no C++ runtime library, the owner's
    # rule for every shipped product (2026-11-15). Each executable and shared
    # library this toolchain links holds its own copy; on ELF their symbols
    # stay exported, so the dynamic linker binds every module to one of the
    # copies (the first loaded).
    #
    # -nostdlib++ drops the driver's own -lc++ (a shared libc++ when one is
    # installed; clang's -static-libstdc++ does not cover libc++abi or the
    # unwinder), and the archives come last on every C++ link line through
    # CMAKE_CXX_STANDARD_LIBRARIES, after the objects that need them.
    # Packages: Ubuntu, apt.llvm.org's libc++-22-dev, libc++abi-22-dev and
    # libunwind-22-dev (libc++.a, libc++abi.a, libunwind.a); musl (Alpine
    # 3.24), libc++-dev, libc++-static, llvm-libunwind-static and
    # compiler-rt. musl's libc++ is built over compiler-rt, so the link names
    # it, and -unwindlib=none keeps the driver from adding a shared
    # libunwind beside the archive; musl is recognised by its dynamic loader.
    set(_gf_cxx "-stdlib=libc++")
    set(_gf_link "-stdlib=libc++ -nostdlib++")
    file(GLOB _gf_musl_loader "/lib/ld-musl-*.so.1")
    if(_gf_musl_loader)
        string(APPEND _gf_link " -rtlib=compiler-rt -unwindlib=none")
    endif()
    set(_gf_runtime_archives "-Wl,-Bstatic -lc++ -lc++abi -lunwind -Wl,-Bdynamic")
    set(CMAKE_CXX_FLAGS_INIT "${_gf_cxx}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "${_gf_runtime_archives}")
    # A -DCMAKE_<...>_FLAGS on the command line replaces the _INIT value
    # (template_harness sanitizer_options passes the sanitizer flags that way
    # on Linux), which would silently select libstdc++; a tree configured
    # before the static link has the old cached values. Such a value gets the
    # library flags appended once, at the configure that first sees it:
    # compile flags until they name -stdlib=libc++, link flags until they
    # name -nostdlib++, and the standard libraries until they name
    # -lc++abi. CMakeLists.txt then refuses any tree whose C++ library is not
    # libc++ 22.
    foreach(_gf_variable CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
            CMAKE_MODULE_LINKER_FLAGS CMAKE_CXX_STANDARD_LIBRARIES)
        if(_gf_variable STREQUAL "CMAKE_CXX_FLAGS")
            set(_gf_added "${_gf_cxx}")
            set(_gf_marker "-stdlib=libc\\+\\+")
        elseif(_gf_variable STREQUAL "CMAKE_CXX_STANDARD_LIBRARIES")
            set(_gf_added "${_gf_runtime_archives}")
            set(_gf_marker "-lc\\+\\+abi")
        else()
            set(_gf_added "${_gf_link}")
            set(_gf_marker "-nostdlib\\+\\+")
        endif()
        if(DEFINED CACHE{${_gf_variable}} AND NOT "$CACHE{${_gf_variable}}" MATCHES "${_gf_marker}")
            set(${_gf_variable} "$CACHE{${_gf_variable}} ${_gf_added}" CACHE STRING
                "Flags with the Linux C++ library (cmake/llvm22.cmake)" FORCE)
        endif()
    endforeach()
endif()
