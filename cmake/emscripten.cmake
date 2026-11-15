# The Emscripten toolchain for GUI.Forms' web host (GUI_FORMS_ENABLE_WEB_HOST),
# pinned to one emsdk release. Pass it to the provider and every consumer:
#
#   cmake -S . -B .build/web -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/emscripten.cmake
#
# The emsdk is found through GUI_FORMS_EMSDK (a cache entry or the environment)
# or EMSDK (set by emsdk_env.sh); no path is written here (on the M4, STXlib's
# integrator installs the release once and supplies its root). It must be the
# pinned release, with its
# upstream/emscripten tree. Emscripten's clang is its own snapshot, so the
# emsdk release, not LLVM 22.1, is the compiler contract on this target
# (CMakeLists.txt skips the LLVM 22.1 check only under this file).
#
# The flags every translation unit takes, the stx package's included (it is
# built with this file and these flags, cmake/Stx.cmake):
#   -pthread             the application runs on a worker thread
#                        (PROXY_TO_PTHREAD) and the engine's pools are threads
#   -msimd128            WebAssembly's 128-bit SIMD; stx_skia's WebAssembly
#                        baseline adds -msse4.1 over it on its own target
#   -fwasm-exceptions    native WebAssembly exceptions (no JavaScript
#                        exception emulation, no Asyncify)
#   -sSUPPORT_LONGJMP=wasm  setjmp and longjmp through the same mechanism
#
# The target is wasm32 (no -m64, no MEMORY64). The pthread pool's link flags
# (PTHREAD_POOL_SIZE, PTHREAD_POOL_SIZE_STRICT) are STXlib's web profile's,
# not set here or by the host; the host keeps PROXY_TO_PTHREAD
# (CMakeLists.txt). The allocator is Emscripten's own (no mimalloc).
set(GUI_FORMS_EMSDK_VERSION "6.0.13" CACHE INTERNAL "The emsdk release GUI.Forms' web host is built with")

set(_gf_emsdk "")
if(DEFINED GUI_FORMS_EMSDK AND NOT GUI_FORMS_EMSDK STREQUAL "")
    set(_gf_emsdk "${GUI_FORMS_EMSDK}")
elseif(DEFINED ENV{GUI_FORMS_EMSDK})
    set(_gf_emsdk "$ENV{GUI_FORMS_EMSDK}")
elseif(DEFINED ENV{EMSDK})
    set(_gf_emsdk "$ENV{EMSDK}")
endif()
if(_gf_emsdk STREQUAL "")
    message(FATAL_ERROR "Set GUI_FORMS_EMSDK (or source emsdk_env.sh) to emsdk ${GUI_FORMS_EMSDK_VERSION}")
endif()
file(TO_CMAKE_PATH "${_gf_emsdk}" _gf_emsdk)
set(GUI_FORMS_EMSDK "${_gf_emsdk}" CACHE PATH "The emsdk directory (emsdk ${GUI_FORMS_EMSDK_VERSION})")

set(_gf_emscripten "${_gf_emsdk}/upstream/emscripten")
set(_gf_version_file "${_gf_emscripten}/emscripten-version.txt")
if(NOT EXISTS "${_gf_version_file}")
    message(FATAL_ERROR "${_gf_emsdk} holds no upstream/emscripten tree (activate emsdk ${GUI_FORMS_EMSDK_VERSION})")
endif()
# The file holds the version in quotes: "6.0.13".
file(READ "${_gf_version_file}" _gf_version_text)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _gf_version "${_gf_version_text}")
if(NOT _gf_version STREQUAL GUI_FORMS_EMSDK_VERSION)
    message(FATAL_ERROR "GUI.Forms' web host needs emsdk ${GUI_FORMS_EMSDK_VERSION}; ${_gf_emsdk} is ${_gf_version}")
endif()

include("${_gf_emscripten}/cmake/Modules/Platform/Emscripten.cmake")

set(_gf_web_flags "-pthread -msimd128 -fwasm-exceptions -sSUPPORT_LONGJMP=wasm")
set(CMAKE_C_FLAGS_INIT "${_gf_web_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_gf_web_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_gf_web_flags}")
