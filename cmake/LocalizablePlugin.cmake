# The localizable-literal Clang plugin (tools/localizable_plugin): it records
# every string literal passed to a GUI_FORMS_LOCALIZABLE parameter or field in
# a table in the object file, which gui_forms::localizable_literals() reads
# back at run time. The toolkit builds it against the compiler's own LLVM and
# Clang headers (an ExternalProject, so none of this project's toolchain
# flags, runtime or sanitizers reach a library the compiler loads), compiles
# its own programs with it (gui_forms_compile_with_localizable_plugin), and
# installs it for the package's consumers (cmake/GUIFormsConfig.cmake.in).
#
# The plugin needs only LLVM 22's and Clang 22's headers under
# GUI_FORMS_LLVM_ROOT (about 400 files, listed by tools/localizable_plugin/
# header_closure.py) and, on Windows, the import libraries of the
# libclang-cpp and libLLVM-22 DLLs the compiler loads; no CMake package
# (Homebrew llvm@22; on Linux llvm-22-dev and libclang-22-dev; in MSYS2's
# clang64 mingw-w64-clang-x86_64-llvm beside the compiler's packages). The
# headers can still be present but unusable, so the main configure proves the
# plugin usable before relying on it: it configures and builds the plugin
# project and compiles a probe unit with it, and the build uses the plugin
# only when all three succeed.
# Otherwise the build goes on without the plugin and says so in one status
# line; the plugin's own configure never fails the main build.

option(GUI_FORMS_LOCALIZABLE_PLUGIN
       "Build the Clang plugin that records localizable string literals, and compile the toolkit's programs with it"
       ON)
# The plugin's second pass verifies effect programs (tools/localizable_plugin/
# effect_pass.cpp; gui_forms/engine/effect/effect.hpp). Switching it off, or
# a toolchain that cannot load the plugin, still builds the effects, but
# unverified: configure warns naming the targets, each file that registers
# effects prints a #warning, and the registry labels them unverified and
# counts them (EffectRegistry::unverified_count). Not recommended.
option(GUI_FORMS_EFFECT_VERIFY
       "Verify effect programs with the gui_forms Clang plugin's effect pass (OFF builds them unverified)"
       ON)
# C-10 (owner, 2026-11-08): Linux CI installs LLVM's and Clang's development
# headers and sets this, so a toolchain that cannot verify the effects fails
# the configure there instead of building them labelled unverified.
option(GUI_FORMS_EFFECT_VERIFY_REQUIRED
       "Fail the configure when effect programs cannot be verified (Linux CI sets it)"
       OFF)
# C-12 (owner, 2026-11-08): with the MSVC command line the compiler must be
# clang-cl, which takes the plugin's driver options behind /clang:. Plain
# cl.exe cannot load the plugin and is refused for effect sources
# (gui_forms_verify_effects).
set(GUI_FORMS_CLANG_DRIVER_PREFIX "")
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(GUI_FORMS_CLANG_DRIVER_PREFIX "/clang:")
endif()
set(GUI_FORMS_LLVM_ROOT "" CACHE PATH
    "The LLVM 22 prefix holding include/llvm and include/clang for the plugin (default: the compiler's ..)")

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
        set(${reason} "its project does not configure with these LLVM headers" PARENT_SCOPE)
        return()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${plugin_dir}/build"
        RESULT_VARIABLE _gf_status OUTPUT_VARIABLE _gf_output ERROR_VARIABLE _gf_output)
    file(APPEND "${_gf_log}" "build (exit ${_gf_status}):\n${_gf_output}\n")
    if(NOT _gf_status EQUAL 0 OR NOT EXISTS "${plugin_path}")
        set(${result} FALSE PARENT_SCOPE)
        set(${reason} "it does not build against these LLVM headers" PARENT_SCOPE)
        return()
    endif()
    # The probe unit marks its own parameter, so it needs no GUI.Forms header.
    set(_gf_probe_source "${plugin_dir}/probe.cpp")
    set(_gf_probe_object "${plugin_dir}/probe${CMAKE_CXX_OUTPUT_EXTENSION}")
    file(WRITE "${_gf_probe_source}"
         "void gui_forms_probe_set_text([[clang::annotate(\"gui_forms.localizable:text\")]] const char* text);\n"
         "void gui_forms_probe() { gui_forms_probe_set_text(\"GUIFormsLocalizableProbe\"); }\n")
    file(REMOVE "${_gf_probe_object}")
    # clang-cl spells the standard, the compile and the output its own way.
    if(GUI_FORMS_CLANG_DRIVER_PREFIX)
        set(_gf_probe_command "${CMAKE_CXX_COMPILER}" /std:c++20 "/clang:-fplugin=${plugin_path}"
            "/clang:-fplugin-arg-gui_forms_localizable-root=${plugin_dir}"
            /c "${_gf_probe_source}" "/Fo${_gf_probe_object}")
    else()
        set(_gf_probe_command "${CMAKE_CXX_COMPILER}" -std=c++20 "-fplugin=${plugin_path}"
            "-fplugin-arg-gui_forms_localizable-root=${plugin_dir}"
            -c "${_gf_probe_source}" -o "${_gf_probe_object}")
    endif()
    execute_process(
        COMMAND ${_gf_probe_command}
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
    # The plugin finds LLVM's and Clang's headers (and on Windows the
    # import libraries) under one prefix (tools/localizable_plugin/
    # CMakeLists.txt); no CMake package is loaded.
    set(_gf_llvm_root "${GUI_FORMS_LLVM_ROOT}")
    if(NOT _gf_llvm_root)
        get_filename_component(_gf_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
        get_filename_component(_gf_compiler_dir "${_gf_compiler}" DIRECTORY)
        get_filename_component(_gf_llvm_root "${_gf_compiler_dir}/.." ABSOLUTE)
    endif()
    set(_gf_plugin_dir "${CMAKE_CURRENT_BINARY_DIR}/localizable_plugin")
    set(_gf_plugin_path "${_gf_plugin_dir}/gui_forms_localizable${CMAKE_SHARED_MODULE_SUFFIX}")
    set(_gf_plugin_arguments
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DGUI_FORMS_LLVM_ROOT=${_gf_llvm_root}"
        "-DCMAKE_BUILD_TYPE=Release"
        # Empty on purpose: the sanitizer tree's CXXFLAGS and LDFLAGS
        # (template_harness sanitizer_options on macOS) would otherwise
        # instrument the plugin, and an ASan runtime cannot be dlopened into
        # an uninstrumented compiler ("interceptors are not working"). A -D
        # value replaces the environment's initial flags.
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
    elseif(NOT EXISTS "${_gf_llvm_root}/include/llvm/Config/llvm-config.h")
        set(_gf_reason "there is no llvm/Config/llvm-config.h in ${_gf_llvm_root}/include")
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
                       "(see ${_gf_plugin_dir}/probe.log; GUI_FORMS_LLVM_ROOT names another LLVM prefix)")
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
    set(_gf_prefix "${GUI_FORMS_CLANG_DRIVER_PREFIX}")
    # Ninja runs every compile in the top build directory, where the plugin
    # takes a relative root from (it resolves it against the compiler's
    # working directory). ccache rewrites absolute paths below CCACHE_BASEDIR
    # but not inside a plugin argument, so an absolute root would keep two
    # worktrees from sharing any object compiled with the plugin. Other
    # generators compile in each target's own directory and keep it absolute.
    set(_gf_root "${CMAKE_SOURCE_DIR}")
    if(CMAKE_GENERATOR MATCHES "Ninja")
        # From the real paths: the compiler resolves the root against its
        # working directory, which is the build tree's real path, so a tree
        # reached through a symbolic link (the M4's sanitizer trees on an
        # external drive) would otherwise get a root that names nothing and
        # the plugin would record absolute file names.
        file(REAL_PATH "${CMAKE_BINARY_DIR}" _gf_binary_real)
        file(REAL_PATH "${CMAKE_SOURCE_DIR}" _gf_source_real)
        file(RELATIVE_PATH _gf_root "${_gf_binary_real}" "${_gf_source_real}")
        if(_gf_root STREQUAL "")
            set(_gf_root ".")
        endif()
    endif()
    set(_gf_flags
        "${_gf_prefix}-fplugin=${GUI_FORMS_LOCALIZABLE_PLUGIN_PATH}"
        "${_gf_prefix}-fplugin-arg-gui_forms_localizable-root=${_gf_root}")
    if(NOT GUI_FORMS_EFFECT_VERIFY)
        list(APPEND _gf_flags "${_gf_prefix}-fplugin-arg-gui_forms_localizable-effects=off")
    endif()
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

# Whether effect programs are verified in this build: the effect pass is on
# and the plugin is usable.
if(GUI_FORMS_EFFECT_VERIFY AND GUI_FORMS_LOCALIZABLE_PLUGIN_PATH)
    set(GUI_FORMS_EFFECTS_VERIFIED TRUE)
else()
    set(GUI_FORMS_EFFECTS_VERIFIED FALSE)
endif()

# Compiles `SOURCES` of `target`, the files that register effect programs,
# with the plugin's effect pass, which fails the compile on any effect that
# breaks the rules or its budgets and records a verification for each that
# passes. Without the pass the sources compile with GUI_FORMS_EFFECTS_UNVERIFIED
# defined (each prints its #warning) and configure warns naming the target,
# or fails when GUI_FORMS_EFFECT_VERIFY_REQUIRED is on. Plain cl.exe is
# refused whatever the options (C-12): only clang-cl can load the pass.
function(gui_forms_verify_effects target)
    cmake_parse_arguments(PARSE_ARGV 1 _gf "" "" "SOURCES")
    string(JOIN ", " _gf_files ${_gf_SOURCES})
    if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "GUI.Forms effects in ${target} (${_gf_files}) must be compiled with clang-cl "
                            "(LLVM 22), not cl.exe: only Clang can load the effect pass that bounds them. "
                            "Use the Visual Studio generator's ClangCL toolset (-T ClangCL) or set "
                            "CMAKE_CXX_COMPILER to clang-cl (docs/user/effect-verification.md).")
    endif()
    if(GUI_FORMS_EFFECTS_VERIFIED)
        gui_forms_compile_with_localizable_plugin(${target} SOURCES ${_gf_SOURCES})
        return()
    endif()
    if(NOT GUI_FORMS_EFFECT_VERIFY)
        set(_gf_reason "GUI_FORMS_EFFECT_VERIFY is OFF")
    else()
        set(_gf_reason "the gui_forms Clang plugin is not usable with this toolchain")
    endif()
    if(GUI_FORMS_EFFECT_VERIFY_REQUIRED)
        message(FATAL_ERROR "GUI.Forms effects in ${target} (${_gf_files}) cannot be verified: ${_gf_reason}, "
                            "and GUI_FORMS_EFFECT_VERIFY_REQUIRED is on (see localizable_plugin/probe.log in "
                            "the build tree; on Debian and Ubuntu install llvm-22-dev and libclang-22-dev).")
    endif()
    message(WARNING "GUI.Forms effects in ${target} (${_gf_files}) are unverified: ${_gf_reason}, so their "
                    "bounds are not checked. They run, labelled unverified and counted by the effect registry.")
    set_property(SOURCE ${_gf_SOURCES} TARGET_DIRECTORY ${target} APPEND PROPERTY
                 COMPILE_DEFINITIONS GUI_FORMS_EFFECTS_UNVERIFIED)
endfunction()
