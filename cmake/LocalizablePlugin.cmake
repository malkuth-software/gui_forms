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
# toolchain's lib/cmake/clang). A package can be present but unusable (CI's
# Linux runners carry ClangTargets.cmake without the static libraries it
# names), so the main configure proves the plugin usable before relying on
# it: it configures and builds the plugin project and compiles a probe unit
# with it, and the build uses the plugin only when all three succeed.
# Otherwise the build goes on without the plugin and says so in one status
# line; the plugin's own configure never fails the main build.

option(GUI_FORMS_LOCALIZABLE_PLUGIN
       "Build the Clang plugin that records localizable string literals, and compile the toolkit's programs with it"
       ON)
set(GUI_FORMS_CLANG_CMAKE_DIR "" CACHE PATH
    "Directory holding ClangConfig.cmake for the plugin (default: the compiler's ../lib/cmake/clang)")

# Configures and builds the plugin project in plugin_dir/build, then compiles
# a probe unit with the result. Sets `result` in the caller to TRUE when the
# compiler loaded the plugin and it recorded the probe's literal, and to
# FALSE otherwise; `reason` names the first step that failed. The steps'
# output goes to plugin_dir/probe.log. The probe starts from an empty
# plugin_dir/build and no plugin: a cache left by another compiler or
# package would be wiped by CMake on reconfigure, dropping the -D arguments
# (the macOS minimum among them). A successful probe leaves the configured
# and built plugin project in place, where the ExternalProject below finds
# it up to date.
function(_gui_forms_probe_localizable_plugin plugin_dir plugin_path result reason)
    set(_gf_log "${plugin_dir}/probe.log")
    file(REMOVE_RECURSE "${plugin_dir}/build")
    file(REMOVE "${plugin_path}")
    file(MAKE_DIRECTORY "${plugin_dir}/build")
    set(_gf_generator_arguments -G "${CMAKE_GENERATOR}")
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND _gf_generator_arguments "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_SOURCE_DIR}/tools/localizable_plugin"
                -B "${plugin_dir}/build" ${_gf_generator_arguments} ${ARGN}
        RESULT_VARIABLE _gf_status OUTPUT_VARIABLE _gf_output ERROR_VARIABLE _gf_output)
    file(WRITE "${_gf_log}" "configure (exit ${_gf_status}):\n${_gf_output}\n")
    if(NOT _gf_status EQUAL 0)
        set(${result} FALSE PARENT_SCOPE)
        set(${reason} "its project does not configure with this Clang package" PARENT_SCOPE)
        return()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${plugin_dir}/build"
        RESULT_VARIABLE _gf_status OUTPUT_VARIABLE _gf_output ERROR_VARIABLE _gf_output)
    file(APPEND "${_gf_log}" "build (exit ${_gf_status}):\n${_gf_output}\n")
    if(NOT _gf_status EQUAL 0 OR NOT EXISTS "${plugin_path}")
        set(${result} FALSE PARENT_SCOPE)
        set(${reason} "it does not build against this Clang package" PARENT_SCOPE)
        return()
    endif()
    # The probe unit marks its own parameter, so it needs no GUI.Forms header.
    set(_gf_probe_source "${plugin_dir}/probe.cpp")
    set(_gf_probe_object "${plugin_dir}/probe${CMAKE_CXX_OUTPUT_EXTENSION}")
    file(WRITE "${_gf_probe_source}"
         "void gui_forms_probe_set_text([[clang::annotate(\"gui_forms.localizable:text\")]] const char* text);\n"
         "void gui_forms_probe() { gui_forms_probe_set_text(\"GUIFormsLocalizableProbe\"); }\n")
    file(REMOVE "${_gf_probe_object}")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -std=c++20 "-fplugin=${plugin_path}"
                "-fplugin-arg-gui_forms_localizable-root=${plugin_dir}"
                -c "${_gf_probe_source}" -o "${_gf_probe_object}"
        RESULT_VARIABLE _gf_status OUTPUT_VARIABLE _gf_output ERROR_VARIABLE _gf_output)
    file(APPEND "${_gf_log}" "probe compile (exit ${_gf_status}):\n${_gf_output}\n")
    if(NOT _gf_status EQUAL 0 OR NOT EXISTS "${_gf_probe_object}")
        set(${result} FALSE PARENT_SCOPE)
        set(${reason} "the compiler cannot load it" PARENT_SCOPE)
        return()
    endif()
    # The table's note name and the recorded literal are both in the object.
    file(STRINGS "${_gf_probe_object}" _gf_note_names REGEX "GUIForms")
    file(STRINGS "${_gf_probe_object}" _gf_literals REGEX "GUIFormsLocalizableProbe")
    if(NOT _gf_note_names OR NOT _gf_literals)
        set(${result} FALSE PARENT_SCOPE)
        set(${reason} "the compiler loads it but it records no table" PARENT_SCOPE)
        return()
    endif()
    set(${result} TRUE PARENT_SCOPE)
    set(${reason} "" PARENT_SCOPE)
endfunction()

set(GUI_FORMS_LOCALIZABLE_PLUGIN_PATH "")
if(GUI_FORMS_LOCALIZABLE_PLUGIN)
    set(_gf_clang_dir "${GUI_FORMS_CLANG_CMAKE_DIR}")
    if(NOT _gf_clang_dir)
        get_filename_component(_gf_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
        get_filename_component(_gf_compiler_dir "${_gf_compiler}" DIRECTORY)
        get_filename_component(_gf_clang_dir "${_gf_compiler_dir}/../lib/cmake/clang" ABSOLUTE)
    endif()
    set(_gf_plugin_dir "${CMAKE_CURRENT_BINARY_DIR}/localizable_plugin")
    set(_gf_plugin_path "${_gf_plugin_dir}/gui_forms_localizable${CMAKE_SHARED_MODULE_SUFFIX}")
    # The plugin project enables C for LLVM's own package checks; it uses
    # this project's C compiler, the same Clang.
    set(_gf_plugin_arguments
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
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
        # The installed plugin is held to the package's macOS minimum
        # (template_harness audit_macos_minimum.py) like every other library.
        list(APPEND _gf_plugin_arguments
            "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    # A probe that succeeded is not repeated while its inputs stay the same;
    # a failed one is repeated at every configure, so installing the package
    # takes effect without clearing the cache.
    string(JOIN "|" _gf_probe_key "${CMAKE_GENERATOR}" ${_gf_plugin_arguments})
    set(_gf_usable FALSE)
    set(_gf_reason "")
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 22
       OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 23)
        set(_gf_reason "the compiler is not Clang 22")
    elseif(NOT EXISTS "${_gf_clang_dir}/ClangConfig.cmake")
        set(_gf_reason "there is no ClangConfig.cmake in ${_gf_clang_dir}")
    elseif(GUI_FORMS_LOCALIZABLE_PLUGIN_PROBED STREQUAL _gf_probe_key AND EXISTS "${_gf_plugin_path}")
        set(_gf_usable TRUE)
    else()
        _gui_forms_probe_localizable_plugin("${_gf_plugin_dir}" "${_gf_plugin_path}" _gf_usable _gf_reason
                                            ${_gf_plugin_arguments})
    endif()
    if(_gf_usable)
        set(GUI_FORMS_LOCALIZABLE_PLUGIN_PROBED "${_gf_probe_key}" CACHE INTERNAL
            "Inputs of the last successful localizable-literal plugin probe")
    else()
        unset(GUI_FORMS_LOCALIZABLE_PLUGIN_PROBED CACHE)
    endif()
    if(NOT _gf_usable)
        message(STATUS "GUI.Forms localizable-literal plugin: skipped, ${_gf_reason} "
                       "(see ${_gf_plugin_dir}/probe.log; GUI_FORMS_CLANG_CMAKE_DIR names another package)")
    else()
        include(ExternalProject)
        set(GUI_FORMS_LOCALIZABLE_PLUGIN_PATH "${_gf_plugin_path}")
        # BUILD_ALWAYS: the sub-build is up to date in a moment, and an edit
        # to the plugin rebuilds it and, through OBJECT_DEPENDS, everything
        # compiled with it. The probe already configured BINARY_DIR with
        # the same arguments and generator.
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
