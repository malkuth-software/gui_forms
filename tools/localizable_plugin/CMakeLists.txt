# The GUI.Forms localizable-literal Clang plugin. A project of its own: it is
# loaded into the compiler, so it is built against the compiler's Clang (its
# headers, the compiler's own C++ library and Clang's shared libraries),
# never with the toolkit's toolchain file, runtime or sanitizers. The toolkit
# builds it with ExternalProject (GUI_FORMS_LOCALIZABLE_PLUGIN).
cmake_minimum_required(VERSION 3.25)
project(GUIFormsLocalizablePlugin LANGUAGES CXX)

# What the plugin needs from LLVM 22 is its headers and Clang's, and on
# Windows the import libraries of the two DLLs clang.exe loads. No CMake
# package is loaded: LLVMConfig.cmake's exports demand every static library
# of the distribution (388 MB in MSYS2's clang64) and ClangConfig.cmake
# names libraries some distributions split off, while the plugin uses about
# 400 headers (tools/localizable_plugin/header_closure.py lists them). Everything is
# found under GUI_FORMS_LLVM_ROOT, the LLVM prefix (the toolkit passes the
# compiler's), and nowhere else; each path can also be set directly.
set(GUI_FORMS_LLVM_ROOT "" CACHE PATH "The LLVM 22 prefix holding include/llvm and include/clang")
if(NOT GUI_FORMS_LLVM_ROOT)
    message(FATAL_ERROR "The localizable-literal plugin needs GUI_FORMS_LLVM_ROOT, the LLVM 22 prefix")
endif()
find_path(GUI_FORMS_LLVM_INCLUDE_DIR llvm/Config/llvm-config.h
          PATHS "${GUI_FORMS_LLVM_ROOT}/include" NO_DEFAULT_PATH)
if(NOT GUI_FORMS_LLVM_INCLUDE_DIR)
    message(FATAL_ERROR "The localizable-literal plugin needs LLVM 22's headers: there is no "
                        "llvm/Config/llvm-config.h in ${GUI_FORMS_LLVM_ROOT}/include (MSYS2 clang64: "
                        "mingw-w64-clang-x86_64-llvm; Debian and Ubuntu: llvm-22-dev; Homebrew: llvm@22)")
endif()
file(STRINGS "${GUI_FORMS_LLVM_INCLUDE_DIR}/llvm/Config/llvm-config.h" _gf_version_line
     REGEX "^#define LLVM_VERSION_MAJOR [0-9]+")
string(REGEX REPLACE "^#define LLVM_VERSION_MAJOR ([0-9]+).*$" "\\1" _gf_llvm_major "${_gf_version_line}")
if(NOT _gf_llvm_major STREQUAL "22")
    message(FATAL_ERROR "The localizable-literal plugin is built for Clang 22; "
                        "${GUI_FORMS_LLVM_INCLUDE_DIR}/llvm/Config/llvm-config.h is LLVM '${_gf_llvm_major}'")
endif()
find_path(GUI_FORMS_CLANG_INCLUDE_DIR clang/AST/ASTConsumer.h
          PATHS "${GUI_FORMS_LLVM_ROOT}/include" NO_DEFAULT_PATH)
if(NOT GUI_FORMS_CLANG_INCLUDE_DIR)
    message(FATAL_ERROR "The localizable-literal plugin needs Clang 22's headers: there is no "
                        "clang/AST/ASTConsumer.h in ${GUI_FORMS_LLVM_ROOT}/include (MSYS2 clang64: "
                        "mingw-w64-clang-x86_64-clang; Debian and Ubuntu: libclang-22-dev; Homebrew: llvm@22)")
endif()

add_library(gui_forms_localizable MODULE localizable_plugin.cpp effect_pass.cpp)
set_target_properties(gui_forms_localizable PROPERTIES
    PREFIX ""
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden)
target_include_directories(gui_forms_localizable SYSTEM PRIVATE ${GUI_FORMS_LLVM_INCLUDE_DIR}
                           ${GUI_FORMS_CLANG_INCLUDE_DIR})
# The definitions LLVM's own build gives its users (LLVM_DEFINITIONS in its
# CMake package): the C99 limit and format macros everywhere, and 64-bit
# file offsets except on Apple, where they are the only kind. The plugin
# uses no RTTI and is compiled without it, which links against an LLVM built
# either way (with RTTI, the plugin's own classes merely carry no type
# information; the reverse would need typeinfo symbols an LLVM built without
# RTTI does not have), so the configure need not ask how LLVM was built.
target_compile_definitions(gui_forms_localizable PRIVATE
    __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS)
target_compile_options(gui_forms_localizable PRIVATE -fno-rtti)
if(NOT APPLE)
    target_compile_definitions(gui_forms_localizable PRIVATE _FILE_OFFSET_BITS=64)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(gui_forms_localizable PRIVATE _GNU_SOURCE)
endif()
# The Clang and LLVM symbols come from the compiler that loads the plugin.
# macOS looks them up when it loads (dynamic_lookup); an ELF module leaves
# them undefined for the loader. A Windows DLL must resolve them when it is
# linked: against the import libraries of the shared libraries clang.exe
# itself loads (libclang-cpp.dll and libLLVM-22.dll in MSYS2's clang64,
# whose import libraries come with the compiler's own packages), so that the
# plugin registers into the compiler's own registries.
if(APPLE)
    target_link_options(gui_forms_localizable PRIVATE -undefined dynamic_lookup)
elseif(WIN32)
    find_library(GUI_FORMS_CLANG_CPP_LIBRARY NAMES clang-cpp PATHS "${GUI_FORMS_LLVM_ROOT}/lib" NO_DEFAULT_PATH)
    find_library(GUI_FORMS_LLVM_LIBRARY NAMES LLVM-22 PATHS "${GUI_FORMS_LLVM_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT GUI_FORMS_CLANG_CPP_LIBRARY OR NOT GUI_FORMS_LLVM_LIBRARY)
        message(FATAL_ERROR "The localizable-literal plugin links against Clang's and LLVM's shared libraries on "
                            "Windows; found clang-cpp '${GUI_FORMS_CLANG_CPP_LIBRARY}' and LLVM-22 "
                            "'${GUI_FORMS_LLVM_LIBRARY}' in ${GUI_FORMS_LLVM_ROOT}/lib (MSYS2 clang64: "
                            "mingw-w64-clang-x86_64-clang and mingw-w64-clang-x86_64-llvm-tools)")
    endif()
    target_link_libraries(gui_forms_localizable PRIVATE ${GUI_FORMS_CLANG_CPP_LIBRARY} ${GUI_FORMS_LLVM_LIBRARY})
endif()
install(TARGETS gui_forms_localizable LIBRARY DESTINATION lib)
