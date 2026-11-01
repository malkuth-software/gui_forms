# The GUI.Forms localizable-literal Clang plugin. A project of its own: it is
# loaded into the compiler, so it is built against the compiler's Clang (its
# CMake package and headers, the compiler's own C++ library and no RTTI),
# never with the toolkit's toolchain file, runtime or sanitizers. The toolkit
# builds it with ExternalProject (GUI_FORMS_LOCALIZABLE_PLUGIN).
cmake_minimum_required(VERSION 3.25)
# C as well: LLVMConfig.cmake's FindLibEdit runs check_include_file, a C
# try_compile, which fails the configure when only C++ is enabled (it did in
# the sanitizer tree, where pkg-config did not find libedit first).
project(GUIFormsLocalizablePlugin LANGUAGES C CXX)

# Clang's package version file accepts only its exact version, so the major
# version is checked here.
find_package(Clang REQUIRED CONFIG)
if(NOT LLVM_VERSION_MAJOR EQUAL 22)
    message(FATAL_ERROR "The localizable-literal plugin is built for Clang 22; found ${LLVM_PACKAGE_VERSION}")
endif()

add_library(gui_forms_localizable MODULE localizable_plugin.cpp)
set_target_properties(gui_forms_localizable PROPERTIES
    PREFIX ""
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden)
target_include_directories(gui_forms_localizable SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
separate_arguments(_gf_llvm_definitions NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(gui_forms_localizable PRIVATE ${_gf_llvm_definitions})
if(NOT LLVM_ENABLE_RTTI)
    target_compile_options(gui_forms_localizable PRIVATE -fno-rtti)
endif()
# The Clang and LLVM symbols come from the compiler that loads the plugin.
if(APPLE)
    target_link_options(gui_forms_localizable PRIVATE -undefined dynamic_lookup)
endif()
install(TARGETS gui_forms_localizable LIBRARY DESTINATION lib)
