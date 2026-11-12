# One shared precompiled header for the test programs (owner, 2026-11-09).
#
# Test sources take about half of a build's compile work, and almost all of
# it is parsing the same standard library and toolkit headers (about 1260
# per test source). gui_forms_share_test_precompiled_header() compiles
# tests/support/test_precompiled.hpp once per group of test programs and
# lets every other program of the group reuse it (REUSE_FROM).
#
# A precompiled header may be reused only where it was compiled with the same
# options, so the groups are the test programs whose compile-affecting
# properties are equal: the libraries they link (whose usage requirements give
# the include directories, definitions and options), their own definitions,
# options, include directories, features and standard. The first program of a
# group compiles the header; a group of one gains nothing and is left alone.
#
# A test program is left out when it compiles anything but C++ sources (the
# Objective-C++ macOS host tests), when a source carries options of its own,
# or when a source defines a macro before its last include (NOMINMAX before a
# Windows header): the header is read before the source, so the macro would
# come too late.
#
# GUI_FORMS_TEST_PRECOMPILED_HEADER (default ON) turns it off: the scheduled
# CI build compiles every test without it, so that each test source still
# includes what it uses.
#
# Clang stamps a precompiled header with its build time unless told not to,
# which would make every reuse a compiler-cache miss; -fno-pch-timestamp goes
# on every program of a group, so the group's options stay equal. ccache also
# refuses any unit that reads a precompiled header ("could not use precompiled
# header") unless its sloppiness includes pch_defines and time_macros, so when
# the compiler launcher is ccache these programs get it with that sloppiness
# as a leading argument (ccache 4.8 and later read KEY=VALUE arguments before
# the compiler). Every build tree then caches them, whatever its environment;
# other programs keep ccache's strict defaults.
option(GUI_FORMS_TEST_PRECOMPILED_HEADER "Share one precompiled header among the test programs" ON)

# Whether a test source defines a macro before its last include (see above).
function(_gui_forms_source_defines_before_include source result)
    set(defines_first FALSE)
    get_filename_component(source_path "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(EXISTS "${source_path}")
        file(STRINGS "${source_path}" directive_lines REGEX "^[ \t]*#[ \t]*(define|include)[ \t]")
        set(seen_define FALSE)
        foreach(directive_line IN LISTS directive_lines)
            if(directive_line MATCHES "^[ \t]*#[ \t]*define")
                set(seen_define TRUE)
            elseif(seen_define)
                set(defines_first TRUE)
                break()
            endif()
        endforeach()
    endif()
    set(${result} ${defines_first} PARENT_SCOPE)
endfunction()

# The group signature of a test program, or empty when it is left out.
function(_gui_forms_test_pch_signature target result)
    set(signature "")
    get_target_property(target_sources ${target} SOURCES)
    get_target_property(existing_headers ${target} PRECOMPILE_HEADERS)
    get_target_property(existing_reuse ${target} PRECOMPILE_HEADERS_REUSE_FROM)
    if(existing_headers OR existing_reuse OR NOT target_sources)
        set(${result} "" PARENT_SCOPE)
        return()
    endif()
    foreach(source IN LISTS target_sources)
        # Objects compiled elsewhere (an object library's) read no header here.
        if(source MATCHES "^\\$<TARGET_OBJECTS:[A-Za-z0-9_]+>$")
            continue()
        endif()
        if(source MATCHES "\\$<")
            set(${result} "" PARENT_SCOPE)
            return()
        endif()
        if(NOT source MATCHES "\\.(cpp|cc|cxx)$")
            if(source MATCHES "\\.(c|m|mm)$")
                set(${result} "" PARENT_SCOPE)
                return()
            endif()
            continue()
        endif()
        get_source_file_property(source_options "${source}" COMPILE_OPTIONS)
        get_source_file_property(source_definitions "${source}" COMPILE_DEFINITIONS)
        if(source_options OR source_definitions)
            set(${result} "" PARENT_SCOPE)
            return()
        endif()
        _gui_forms_source_defines_before_include("${source}" defines_first)
        if(defines_first)
            set(${result} "" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    foreach(property IN ITEMS LINK_LIBRARIES COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES
                              COMPILE_FEATURES CXX_STANDARD CXX_EXTENSIONS POSITION_INDEPENDENT_CODE)
        get_target_property(property_value ${target} ${property})
        string(APPEND signature "${property}=${property_value};")
    endforeach()
    set(${result} "${signature}" PARENT_SCOPE)
endfunction()

# Groups the calling directory's test programs (executables named *_tests)
# and gives each group of two or more one precompiled header.
function(gui_forms_share_test_precompiled_header)
    if(NOT GUI_FORMS_TEST_PRECOMPILED_HEADER)
        message(STATUS "Test programs: no shared precompiled header (GUI_FORMS_TEST_PRECOMPILED_HEADER=OFF)")
        return()
    endif()
    set(header "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/test_precompiled.hpp")
    get_property(directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    # signature digest -> the group's programs, in definition order
    set(group_digests "")
    foreach(target IN LISTS directory_targets)
        get_target_property(target_type ${target} TYPE)
        if(NOT target_type STREQUAL "EXECUTABLE" OR NOT target MATCHES "_tests$")
            continue()
        endif()
        _gui_forms_test_pch_signature(${target} signature)
        if(signature STREQUAL "")
            continue()
        endif()
        string(SHA1 digest "${signature}")
        if(NOT digest IN_LIST group_digests)
            list(APPEND group_digests ${digest})
            set(group_${digest} "")
        endif()
        list(APPEND group_${digest} ${target})
    endforeach()
    # The launcher of the programs that use the header (see above): ccache with
    # the sloppiness it needs, or the configured launcher unchanged.
    set(pch_launcher "${CMAKE_CXX_COMPILER_LAUNCHER}")
    if(CMAKE_CXX_COMPILER_LAUNCHER)
        list(GET CMAKE_CXX_COMPILER_LAUNCHER 0 launcher_program)
        get_filename_component(launcher_name "${launcher_program}" NAME_WE)
        string(TOLOWER "${launcher_name}" launcher_name)
        if(launcher_name STREQUAL "ccache")
            list(APPEND pch_launcher "sloppiness=pch_defines,time_macros")
        endif()
    endif()
    set(shared_programs 0)
    set(shared_groups 0)
    foreach(digest IN LISTS group_digests)
        list(LENGTH group_${digest} group_size)
        if(group_size LESS 2)
            continue()
        endif()
        list(GET group_${digest} 0 compiling_target)
        foreach(target IN LISTS group_${digest})
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                target_compile_options(${target} PRIVATE "SHELL:-Xclang -fno-pch-timestamp")
            endif()
            if(pch_launcher)
                set_property(TARGET ${target} PROPERTY CXX_COMPILER_LAUNCHER "${pch_launcher}")
            endif()
            if(target STREQUAL compiling_target)
                target_precompile_headers(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${header}>")
            else()
                target_precompile_headers(${target} REUSE_FROM ${compiling_target})
            endif()
        endforeach()
        math(EXPR shared_programs "${shared_programs} + ${group_size}")
        math(EXPR shared_groups "${shared_groups} + 1")
    endforeach()
    message(STATUS "Test programs: ${shared_programs} share ${shared_groups} precompiled headers")
endfunction()
