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
    # No -stdlib=libc++ when compiling: -nostdinc++ and the runtime's -isystem
    # choose the headers, so clang reported -stdlib unused on every compile
    # (1325 warnings a Linux build); the link flags keep it.
    set(_gf_cxx "-nostdinc++ -isystem \"${_gf_runtime}/include/c++/v1\"")
    set(CMAKE_CXX_FLAGS_INIT "${_gf_cxx}")
    set(CMAKE_OBJCXX_FLAGS_INIT "${_gf_cxx}")
    # A program is one module (owner decision, 2026-11-15): it links the
    # runtime's libc++, libc++abi and libunwind archives statically, as on
    # Linux and Windows, when the runtime tree carries them (template_harness's
    # build_macos_runtimes.py builds them hermetic, claude/macos-static-libcxx).
    # A runtime tree with dylibs only is linked as before, through the dylibs
    # and the rpath; GUI_FORMS_STATIC_CXX_RUNTIME says which, for the checks
    # that refuse a shared C++ runtime.
    if(EXISTS "${_gf_runtime}/lib/libc++.a" AND EXISTS "${_gf_runtime}/lib/libc++abi.a"
       AND EXISTS "${_gf_runtime}/lib/libunwind.a")
        set(_gf_link "-nostdlib++")
        set(CMAKE_CXX_STANDARD_LIBRARIES_INIT
            "\"${_gf_runtime}/lib/libc++.a\" \"${_gf_runtime}/lib/libc++abi.a\" \"${_gf_runtime}/lib/libunwind.a\"")
        set(CMAKE_OBJCXX_STANDARD_LIBRARIES_INIT "${CMAKE_CXX_STANDARD_LIBRARIES_INIT}")
        set(GUI_FORMS_STATIC_CXX_RUNTIME ON CACHE BOOL "The C++ runtime is linked statically (cmake/llvm22.cmake)" FORCE)
    else()
        set(_gf_link "-L\"${_gf_runtime}/lib\" -Wl,-rpath,\"${_gf_runtime}/lib\" -lunwind")
        set(GUI_FORMS_STATIC_CXX_RUNTIME OFF CACHE BOOL "The C++ runtime is linked statically (cmake/llvm22.cmake)" FORCE)
    endif()
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_gf_link}")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    # Linux builds against LLVM's libc++ 22 and libc++abi, as macOS and
    # Windows do (owner decision, 2026-11-15), and links them statically,
    # with LLVM's unwinder: a shipped program depends on no C++ runtime
    # library (2026-11-15). The runtime is the harness's own on every Linux,
    # glibc or musl: template_harness's build_linux_runtimes.py builds
    # libc++, libc++abi and libunwind from the pinned LLVM 22.1.8 as
    # position-independent, hermetic static archives into the prefix named by
    # GUI_FORMS_LLVM_RUNTIME (.build/toolchain/llvm-22.1.8-linux-<arch>-<libc>).
    # Position independence lets gui_forms_abi, a shared library, carry it
    # (Alpine's own libc++abi.a is not PIC); hidden visibility keeps it from
    # exporting the runtime's symbols. A distribution's archives are refused:
    # its packages are only the bootstrap compiler.
    #
    # -nostdinc++ and -isystem select the runtime's configured headers;
    # -nostdlib++ drops the driver's own -lc++ (and the -lm it adds for C++),
    # and the archives come last on every C++ link line through
    # CMAKE_CXX_STANDARD_LIBRARIES, after the objects that need them. musl's
    # clang uses compiler-rt, so the link names it, and -unwindlib=none keeps
    # the driver from adding a shared libunwind beside the archive; musl is
    # recognised by its dynamic loader.
    if(NOT DEFINED ENV{GUI_FORMS_LLVM_RUNTIME})
        message(FATAL_ERROR "Set GUI_FORMS_LLVM_RUNTIME to the LLVM 22.1.8 Linux runtime prefix "
                            "(template_harness/harness/build_linux_runtimes.py)")
    endif()
    file(TO_CMAKE_PATH "$ENV{GUI_FORMS_LLVM_RUNTIME}" _gf_runtime)
    foreach(_gf_required IN ITEMS lib/libc++.a lib/libc++abi.a lib/libunwind.a include/c++/v1/__config_site
                                  runtime-manifest.json)
        if(NOT EXISTS "${_gf_runtime}/${_gf_required}")
            message(FATAL_ERROR "GUI_FORMS_LLVM_RUNTIME (${_gf_runtime}) lacks ${_gf_required}; build it with "
                                "template_harness/harness/build_linux_runtimes.py")
        endif()
    endforeach()
    # No -stdlib=libc++ when compiling: -nostdinc++ and the runtime's -isystem
    # choose the headers, so clang reported -stdlib unused on every compile
    # (1325 warnings a Linux build); the link flags keep it.
    set(_gf_cxx "-nostdinc++ -isystem \"${_gf_runtime}/include/c++/v1\"")
    set(_gf_link "-stdlib=libc++ -nostdlib++")
    file(GLOB _gf_musl_loader "/lib/ld-musl-*.so.1")
    if(_gf_musl_loader)
        string(APPEND _gf_link " -rtlib=compiler-rt -unwindlib=none")
    endif()
    set(_gf_runtime_archives
        "\"${_gf_runtime}/lib/libc++.a\" \"${_gf_runtime}/lib/libc++abi.a\" \"${_gf_runtime}/lib/libunwind.a\" -lm")
    set(CMAKE_CXX_FLAGS_INIT "${_gf_cxx}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_gf_link}")
    set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "${_gf_runtime_archives}")
    set(GUI_FORMS_STATIC_CXX_RUNTIME ON CACHE BOOL "The C++ runtime is linked statically (cmake/llvm22.cmake)" FORCE)
    # A -DCMAKE_<...>_FLAGS on the command line replaces the _INIT value
    # (template_harness sanitizer_options passes the sanitizer flags that way
    # on Linux), which would silently select libstdc++ or the distribution's
    # libc++; a tree configured before the harness runtime has the old cached
    # values. Such a value gets the library flags appended once, at the
    # configure that first sees it: compile flags until they name -nostdinc++,
    # link flags until they name -nostdlib++, and the standard libraries
    # until they name libc++abi.a. CMakeLists.txt then refuses any tree whose
    # C++ library is not libc++ 22.
    foreach(_gf_variable CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
            CMAKE_MODULE_LINKER_FLAGS CMAKE_CXX_STANDARD_LIBRARIES)
        if(_gf_variable STREQUAL "CMAKE_CXX_FLAGS")
            set(_gf_added "${_gf_cxx}")
            set(_gf_marker "-nostdinc\\+\\+")
            # A tree configured before compiling stopped naming -stdlib
            # (2026-11-17) caches it; clang reports it unused on every compile.
            if(DEFINED CACHE{CMAKE_CXX_FLAGS} AND "$CACHE{CMAKE_CXX_FLAGS}" MATCHES "-stdlib=libc\\+\\+")
                string(REPLACE "-stdlib=libc++" "" _gf_stripped "$CACHE{CMAKE_CXX_FLAGS}")
                string(STRIP "${_gf_stripped}" _gf_stripped)
                set(CMAKE_CXX_FLAGS "${_gf_stripped}" CACHE STRING
                    "Flags with the Linux C++ library (cmake/llvm22.cmake)" FORCE)
            endif()
        elseif(_gf_variable STREQUAL "CMAKE_CXX_STANDARD_LIBRARIES")
            set(_gf_added "${_gf_runtime_archives}")
            set(_gf_marker "libc\\+\\+abi\\.a")
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
