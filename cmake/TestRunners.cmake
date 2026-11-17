# Shared test runners (owner, 2026-11-17: build times). Each test program
# linked the whole toolkit into its own image, and several hundred of them
# made linking most of the build (79 of the Windows runner's 112 minutes).
# gui_forms_merge_test_programs() puts many test programs into one runner
# program each, keeping one process per test:
#
#   - the program's source compiles as an object library,
#     <program>_objects, with the program's compile settings;
#   - llvm-objcopy renames that object's `main` to gf_test_main_<program>
#     (the source is untouched, and main's signature does not matter: every
#     host's calling convention lets a main that takes no arguments be
#     called with argc and argv);
#   - a runner, gui_forms_test_runner_<n>, links the renamed objects of its
#     programs with the test runtime, and its main calls the program named
#     by its first argument, with that argument removed (argv[0] stays the
#     runner's path, beside the old programs in the same directory);
#   - the program itself becomes a launcher of a few lines of C that runs
#     `<runner> <program> <arguments>` and returns its exit status, so
#     every add_test, test property, label and working directory is as it
#     was, and so is every target name.
#
# A program joins a runner when it links only the test runtime
# (cmake/TestRuntime.cmake has made that its whole link), has one C++
# source, keeps its helpers in an anonymous namespace and defines no
# C-linkage name (its external names could otherwise collide with another
# program's), does not replace
# operator new or delete (a replacement would apply to every program in the
# runner), and is not read or run by path elsewhere in the build
# (keep_separate below). Every other program links as before.
#
# Programs are put into runners by the first hex digit of their name's MD5,
# so adding or removing a program relinks one runner and moves no other.
# GUI_FORMS_TEST_RUNNERS=OFF links every program on its own.

option(GUI_FORMS_TEST_RUNNERS "Link the test programs into shared runner programs (cmake/TestRunners.cmake)" ON)

# Programs another part of the build reads or runs by their file
# ($<TARGET_FILE:...> outside add_test): their image must be their own.
set(_gui_forms_test_runner_keep_separate
    gui_forms_fail_fast_tests
    gui_forms_localizable_plugin_tests
    gui_forms_host_protocol_tests
    gui_forms_bundled_font_set_tests
    gui_forms_build_profile_tests
    gui_forms_borrowed_view_tests
    gui_forms_abi_runtime_c11_tests)

# Whether `program` may join a runner (see the conditions above); the
# reason it may not otherwise, for the summary.
function(_gui_forms_test_runner_eligible program result reason)
    set(${result} FALSE PARENT_SCOPE)
    if(program IN_LIST _gui_forms_test_runner_keep_separate)
        set(${reason} "read by path" PARENT_SCOPE)
        return()
    endif()
    get_target_property(links ${program} LINK_LIBRARIES)
    if(NOT links)
        set(${reason} "no links" PARENT_SCOPE)
        return()
    endif()
    foreach(library IN LISTS links)
        if(NOT library STREQUAL "gui_forms_test_runtime" AND NOT library MATCHES "^\\$<COMPILE_ONLY:")
            set(${reason} "links beyond the test runtime" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    if(NOT "gui_forms_test_runtime" IN_LIST links)
        set(${reason} "links beyond the test runtime" PARENT_SCOPE)
        return()
    endif()
    # The directory's own link options (add_link_options) reach every target,
    # the runners too; a program's options of its own keep it apart.
    get_target_property(link_options ${program} LINK_OPTIONS)
    get_directory_property(directory_link_options LINK_OPTIONS)
    if(link_options AND NOT link_options STREQUAL directory_link_options)
        set(${reason} "own link options" PARENT_SCOPE)
        return()
    endif()
    foreach(property IN ITEMS LINK_FLAGS MACOSX_BUNDLE WIN32_EXECUTABLE OUTPUT_NAME
                              RUNTIME_OUTPUT_DIRECTORY PRECOMPILE_HEADERS PRECOMPILE_HEADERS_REUSE_FROM)
        get_target_property(value ${program} ${property})
        if(value)
            set(${reason} "sets ${property}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    get_target_property(sources ${program} SOURCES)
    list(LENGTH sources source_count)
    if(NOT source_count EQUAL 1 OR NOT sources MATCHES "\\.(cpp|cc|cxx)$" OR sources MATCHES "\\$<")
        set(${reason} "not one C++ source" PARENT_SCOPE)
        return()
    endif()
    get_source_file_property(source_options "${sources}" COMPILE_OPTIONS)
    get_source_file_property(source_definitions "${sources}" COMPILE_DEFINITIONS)
    if(source_options OR source_definitions)
        set(${reason} "per-source settings" PARENT_SCOPE)
        return()
    endif()
    get_filename_component(path "${sources}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    file(READ "${path}" text)
    if(NOT text MATCHES "\nnamespace {")
        set(${reason} "external helpers" PARENT_SCOPE)
        return()
    endif()
    # A C-linkage name (the fuzzers' LLVMFuzzerTestOneInput) is the same in
    # every program, whatever namespace holds it.
    if(text MATCHES "extern \"C\"" OR text MATCHES "LLVMFuzzerTestOneInput")
        set(${reason} "C-linkage names" PARENT_SCOPE)
        return()
    endif()
    if(text MATCHES "\n[ \t]*(\\[\\[[a-z_:]+\\]\\][ \t]*)*void[ \t]*\\*[ \t]*operator new" OR
       text MATCHES "\n[ \t]*void[ \t]+operator delete")
        set(${reason} "replaces operator new or delete" PARENT_SCOPE)
        return()
    endif()
    set(${result} TRUE PARENT_SCOPE)
endfunction()

# Call once, after gui_forms_link_tests_to_runtime() and before the
# directory's first-party options, hardening and the shared precompiled
# header are applied (they then apply to the object libraries, runners and
# launchers as they did to the programs).
function(gui_forms_merge_test_programs)
    if(NOT GUI_FORMS_TEST_RUNNERS OR NOT TARGET gui_forms_test_runtime)
        message(STATUS "Test programs: each links on its own (GUI_FORMS_TEST_RUNNERS=OFF or no test runtime)")
        return()
    endif()
    get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(GUI_FORMS_LLVM_OBJCOPY NAMES llvm-objcopy HINTS "${compiler_directory}")
    if(NOT GUI_FORMS_LLVM_OBJCOPY)
        message(STATUS "Test programs: each links on its own (no llvm-objcopy beside ${CMAKE_CXX_COMPILER})")
        return()
    endif()
    # The C symbol of main: Mach-O prefixes an underscore; ELF and 64-bit
    # COFF do not.
    set(symbol_prefix "")
    if(APPLE)
        set(symbol_prefix "_")
    endif()

    get_property(directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(merged "")
    set(kept 0)
    set(reasons "")
    foreach(target IN LISTS directory_targets)
        get_target_property(target_type ${target} TYPE)
        if(NOT target_type STREQUAL "EXECUTABLE" OR NOT target MATCHES "_tests$")
            continue()
        endif()
        _gui_forms_test_runner_eligible(${target} eligible reason)
        if(eligible)
            list(APPEND merged ${target})
        else()
            math(EXPR kept "${kept} + 1")
            string(MAKE_C_IDENTIFIER "${reason}" reason_key)
            if(NOT reason_key IN_LIST reasons)
                list(APPEND reasons ${reason_key})
                set(reason_text_${reason_key} "${reason}")
                set(reason_count_${reason_key} 0)
            endif()
            math(EXPR reason_count_${reason_key} "${reason_count_${reason_key}} + 1")
        endif()
    endforeach()
    set(reason_summary "")
    foreach(reason_key IN LISTS reasons)
        string(APPEND reason_summary " ${reason_text_${reason_key}}: ${reason_count_${reason_key}};")
    endforeach()
    message(STATUS "Test programs kept on their own by reason:${reason_summary}")
    list(LENGTH merged merged_count)
    if(merged_count EQUAL 0)
        message(STATUS "Test programs: none can share a runner (cmake/TestRunners.cmake)")
        return()
    endif()

    set(work "${CMAKE_CURRENT_BINARY_DIR}/test_runners")
    file(MAKE_DIRECTORY "${work}")
    set(buckets 0 1 2 3 4 5 6 7 8 9 a b c d e f)
    foreach(bucket IN LISTS buckets)
        set(bucket_${bucket} "")
    endforeach()
    foreach(program IN LISTS merged)
        string(MD5 digest "${program}")
        string(SUBSTRING "${digest}" 0 1 bucket)
        list(APPEND bucket_${bucket} ${program})
    endforeach()

    set(runner_count 0)
    foreach(bucket IN LISTS buckets)
        if(NOT bucket_${bucket})
            continue()
        endif()
        math(EXPR runner_count "${runner_count} + 1")
        set(runner gui_forms_test_runner_${bucket})
        set(declarations "")
        set(entries "")
        set(objects "")
        foreach(program IN LISTS bucket_${bucket})
            get_target_property(sources ${program} SOURCES)
            # The program's code, compiled as it was.
            add_library(${program}_objects OBJECT ${sources})
            foreach(property IN ITEMS COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES COMPILE_FEATURES
                                      CXX_STANDARD CXX_STANDARD_REQUIRED CXX_EXTENSIONS POSITION_INDEPENDENT_CODE
                                      LINK_LIBRARIES)
                get_target_property(value ${program} ${property})
                if(NOT value STREQUAL "value-NOTFOUND")
                    set_property(TARGET ${program}_objects PROPERTY ${property} "${value}")
                endif()
            endforeach()
            # Native objects (not bitcode) for llvm-objcopy.
            set_property(TARGET ${program}_objects PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
            # Its main, renamed. The object list leaves out a precompiled
            # header's own outputs (cmake_pch.*) where this library builds
            # the header its group shares.
            set(renamed "${work}/${program}.main${CMAKE_CXX_OUTPUT_EXTENSION}")
            set(program_object "$<FILTER:$<TARGET_OBJECTS:${program}_objects>,EXCLUDE,cmake_pch>")
            add_custom_command(
                OUTPUT "${renamed}"
                COMMAND "${GUI_FORMS_LLVM_OBJCOPY}"
                        "--redefine-sym=${symbol_prefix}main=${symbol_prefix}gf_test_main_${program}"
                        "${program_object}" "${renamed}"
                DEPENDS ${program}_objects "${program_object}"
                COMMENT "Renaming ${program}'s main for ${runner}"
                VERBATIM)
            list(APPEND objects "${renamed}")
            string(APPEND declarations "extern \"C\" int gf_test_main_${program}(int argument_count, char** arguments);\n")
            string(APPEND entries "    TestProgram{\"${program}\", &gf_test_main_${program}},\n")
            # The program becomes its launcher.
            set_property(TARGET ${program} PROPERTY SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/test_launcher.c")
            set_property(TARGET ${program} PROPERTY LINK_LIBRARIES "")
            foreach(property IN ITEMS COMPILE_OPTIONS COMPILE_FEATURES INCLUDE_DIRECTORIES COMPILE_DEFINITIONS)
                set_property(TARGET ${program} PROPERTY ${property} "")
            endforeach()
            set_property(TARGET ${program} PROPERTY LINKER_LANGUAGE C)
            target_compile_definitions(${program} PRIVATE "GF_TEST_PROGRAM=\"${program}\""
                                                          "GF_TEST_RUNNER=\"$<TARGET_FILE:${runner}>\"")
            add_dependencies(${program} ${runner})
        endforeach()
        set(registry "${work}/${runner}.cpp")
        set(registry_text
"// Generated by cmake/TestRunners.cmake: the test programs of ${runner}.
// The runner calls the program its first argument names, with that
// argument removed; argv[0] stays the runner's path.

#include <cstdio>
#include <cstring>

${declarations}
namespace {

struct TestProgram final {
    const char* name;
    int (*entry)(int argument_count, char** arguments);
};

constexpr TestProgram programs[] = {
${entries}};

} // namespace

int main(int argument_count, char** arguments) {
    if (argument_count < 2) {
        std::fputs(\"usage: ${runner} <test program> [arguments]\\n\", stderr);
        return 2;
    }
    for (const TestProgram& program : programs) {
        if (std::strcmp(program.name, arguments[1]) != 0) continue;
        arguments[1] = arguments[0];
        const int status = program.entry(argument_count - 1, arguments + 1);
        return status;
    }
    std::fprintf(stderr, \"${runner}: no test program %s\\n\", arguments[1]);
    return 2;
}
")
        file(CONFIGURE OUTPUT "${registry}" CONTENT "${registry_text}" @ONLY)
        add_executable(${runner} "${registry}" ${objects})
        set_source_files_properties(${objects} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_link_libraries(${runner} PRIVATE gui_forms_test_runtime)
    endforeach()
    message(STATUS "Test programs: ${merged_count} in ${runner_count} shared runners, ${kept} on their own "
                   "(cmake/TestRunners.cmake)")
endfunction()
