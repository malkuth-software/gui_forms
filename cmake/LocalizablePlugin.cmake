# The localizable-literal Clang plugin (tools/localizable_plugin): it records
# every string literal passed to a GUI_FORMS_LOCALIZABLE parameter or field in
# a table in the object file, which gui_forms::localizable_literals() reads
# back at run time. The toolkit builds it with the compiler's own Clang CMake
# package (an ExternalProject, so none of this project's toolchain flags,
# runtime or sanitizers reach a library the compiler loads), compiles its own
# programs with it (gui_forms_compile_with_localizable_plugin), and installs it
# for the package's consumers (cmake/GUIFormsConfig.cmake.in).
#
# The plugin needs the Clang CMake package of the LLVM 22 the toolkit is built
# with (Homebrew llvm@22 has it; on Linux the libclang-22-dev package, or the
# toolchain's lib/cmake/clang). Without it the build goes on without the
# plugin and says so.

option(GUI_FORMS_LOCALIZABLE_PLUGIN
       "Build the Clang plugin that records localizable string literals, and compile the toolkit's programs with it"
       ON)
set(GUI_FORMS_CLANG_CMAKE_DIR "" CACHE PATH
    "Directory holding ClangConfig.cmake for the plugin (default: the compiler's ../lib/cmake/clang)")

set(GUI_FORMS_LOCALIZABLE_PLUGIN_PATH "")
if(GUI_FORMS_LOCALIZABLE_PLUGIN)
    set(_gf_clang_dir "${GUI_FORMS_CLANG_CMAKE_DIR}")
    if(NOT _gf_clang_dir)
        get_filename_component(_gf_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
        get_filename_component(_gf_compiler_dir "${_gf_compiler}" DIRECTORY)
        get_filename_component(_gf_clang_dir "${_gf_compiler_dir}/../lib/cmake/clang" ABSOLUTE)
    endif()
    if(NOT EXISTS "${_gf_clang_dir}/ClangConfig.cmake")
        message(WARNING
            "GUI_FORMS_LOCALIZABLE_PLUGIN: no ClangConfig.cmake in ${_gf_clang_dir}; building without the "
            "localizable-literal plugin. Install the LLVM 22 Clang development package, set "
            "GUI_FORMS_CLANG_CMAKE_DIR, or turn GUI_FORMS_LOCALIZABLE_PLUGIN off.")
    else()
        include(ExternalProject)
        set(_gf_plugin_dir "${CMAKE_CURRENT_BINARY_DIR}/localizable_plugin")
        set(GUI_FORMS_LOCALIZABLE_PLUGIN_PATH
            "${_gf_plugin_dir}/gui_forms_localizable${CMAKE_SHARED_MODULE_SUFFIX}")
        # The plugin project enables C for LLVM's own package checks; it
        # uses the C driver beside the compiler, the same Clang.
        get_filename_component(_gf_cxx_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
        get_filename_component(_gf_cxx_compiler_dir "${_gf_cxx_compiler}" DIRECTORY)
        set(_gf_plugin_arguments
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_C_COMPILER=${_gf_cxx_compiler_dir}/clang"
            "-DClang_DIR=${_gf_clang_dir}"
            "-DCMAKE_BUILD_TYPE=Release"
            # Empty on purpose: the sanitizer tree's CFLAGS, CXXFLAGS and
            # LDFLAGS (template_harness sanitizer_options on macOS) would
            # otherwise instrument the plugin, and an ASan runtime cannot be
            # dlopened into an uninstrumented compiler ("interceptors are not
            # working"). A -D value replaces the environment's initial flags.
            "-DCMAKE_C_FLAGS="
            "-DCMAKE_CXX_FLAGS="
            "-DCMAKE_MODULE_LINKER_FLAGS="
            "-DCMAKE_SHARED_LINKER_FLAGS="
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${_gf_plugin_dir}")
        if(APPLE)
            list(APPEND _gf_plugin_arguments
                "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
                "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_HOST_SYSTEM_PROCESSOR}")
        endif()
        # BUILD_ALWAYS: the sub-build is up to date in a moment, and an edit
        # to the plugin rebuilds it and, through OBJECT_DEPENDS, everything
        # compiled with it.
        ExternalProject_Add(gui_forms_localizable_plugin
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/localizable_plugin"
            BINARY_DIR "${_gf_plugin_dir}/build"
            CMAKE_ARGS ${_gf_plugin_arguments}
            BUILD_ALWAYS ON
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${GUI_FORMS_LOCALIZABLE_PLUGIN_PATH}")
        install(FILES "${GUI_FORMS_LOCALIZABLE_PLUGIN_PATH}" DESTINATION ${CMAKE_INSTALL_LIBDIR}/gui_forms)
        message(STATUS "GUI.Forms localizable-literal plugin: ${GUI_FORMS_LOCALIZABLE_PLUGIN_PATH}")
    endif()
endif()

# Compiles the C++ sources of `target` (and, with SOURCES, only those) with
# the plugin, recording file names relative to the source tree. Does nothing
# when the plugin is not built.
function(gui_forms_compile_with_localizable_plugin target)
    cmake_parse_arguments(PARSE_ARGV 1 _gf "" "" "SOURCES")
    if(NOT GUI_FORMS_LOCALIZABLE_PLUGIN_PATH)
        return()
    endif()
    set(_gf_flags
        "-fplugin=${GUI_FORMS_LOCALIZABLE_PLUGIN_PATH}"
        "-fplugin-arg-gui_forms_localizable-root=${CMAKE_SOURCE_DIR}")
    add_dependencies(${target} gui_forms_localizable_plugin)
    if(_gf_SOURCES)
        set(_gf_sources ${_gf_SOURCES})
        set_property(SOURCE ${_gf_sources} TARGET_DIRECTORY ${target} APPEND PROPERTY COMPILE_OPTIONS ${_gf_flags})
    else()
        get_target_property(_gf_sources ${target} SOURCES)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${_gf_flags}>")
    endif()
    # The compiler loads the plugin: a new plugin recompiles the sources.
    set_property(SOURCE ${_gf_sources} TARGET_DIRECTORY ${target} APPEND PROPERTY
                 OBJECT_DEPENDS "${GUI_FORMS_LOCALIZABLE_PLUGIN_PATH}")
endfunction()
