# The D15 vocabulary rewriter (tools/d15_rewrite/README.md). A development
# program of its own, built against Clang 22's LibTooling, never linked into
# any GUI.Forms product. It follows tools/house_policy_check: LLVM's and
# Clang's CMake packages and their shared libraries (clang-cpp, LLVM), which
# Homebrew's llvm@22 and Debian's llvm-22-dev/libclang-22-dev provide.
cmake_minimum_required(VERSION 3.25)
project(gui_forms_d15_rewrite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
if(NOT LLVM_VERSION_MAJOR STREQUAL "22")
    message(FATAL_ERROR "d15_rewrite is written for Clang 22; LLVM_DIR found LLVM ${LLVM_PACKAGE_VERSION}")
endif()

add_executable(gui_forms_d15_rewrite
    src/main.cpp
    src/families.cpp
    src/edits.cpp
    src/analysis.cpp
    src/closure.cpp
)
target_include_directories(gui_forms_d15_rewrite SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
target_compile_definitions(gui_forms_d15_rewrite PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(gui_forms_d15_rewrite PRIVATE clang-cpp LLVM)
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(gui_forms_d15_rewrite PRIVATE -fno-rtti)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(gui_forms_d15_rewrite PRIVATE -Wall -Wextra -Wpedantic)
endif()
