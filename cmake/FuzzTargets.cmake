# The fuzz targets (fuzz/), each built two ways (audit I1, I3, N115).
#
# gui_forms_add_fuzz_test(<name> SOURCE <file> LIBRARIES <targets...>
#                         [ARGUMENTS <paths...>])
#   The suite's program gui_forms_<name>_fuzz_tests: the target's entry
#   point with its standalone driver (GUI_FORMS_FUZZ_STANDALONE; most use
#   fuzz/fuzz_standalone.hpp), linked to the libraries it reads, run by
#   ctest with ARGUMENTS (the seed corpus: files and directories).
#
# gui_forms_add_libfuzzer_target(<name> SOURCES <files...>
#                                [INCLUDES <directories...>] [LIBRARIES <targets...>]
#                                [STANDALONE_DEFINITION <macro>])
#   The libFuzzer program gui_forms_<name>_fuzz (GUI_FORMS_BUILD_FUZZERS),
#   with ASan and UBSan, or, where the compiler has no libFuzzer runtime,
#   the standalone driver under the same sanitizers (STANDALONE_DEFINITION,
#   default GUI_FORMS_FUZZ_STANDALONE, names the driver's macro; stx's own
#   harnesses use theirs). The sanitizers and coverage reach SOURCES; a
#   target that links LIBRARIES reaches into them only when the whole tree
#   is configured with -fsanitize=fuzzer-no-link,address,undefined in
#   CMAKE_CXX_FLAGS.

function(gui_forms_add_fuzz_test name)
    cmake_parse_arguments(PARSE_ARGV 1 fuzz "" "SOURCE" "LIBRARIES;ARGUMENTS")
    set(target gui_forms_${name}_fuzz_tests)
    add_executable(${target} ${fuzz_SOURCE})
    target_link_libraries(${target} PRIVATE ${fuzz_LIBRARIES})
    target_compile_definitions(${target} PRIVATE GUI_FORMS_FUZZ_STANDALONE=1)
    add_test(NAME ${target} COMMAND ${target} ${fuzz_ARGUMENTS})
endfunction()

function(gui_forms_add_libfuzzer_target name)
    cmake_parse_arguments(PARSE_ARGV 1 fuzz "" "STANDALONE_DEFINITION" "SOURCES;INCLUDES;LIBRARIES")
    if(NOT fuzz_STANDALONE_DEFINITION)
        set(fuzz_STANDALONE_DEFINITION GUI_FORMS_FUZZ_STANDALONE)
    endif()
    set(target gui_forms_${name}_fuzz)
    add_executable(${target} ${fuzz_SOURCES})
    target_include_directories(${target} PRIVATE ${fuzz_INCLUDES})
    target_link_libraries(${target} PRIVATE ${fuzz_LIBRARIES})
    target_compile_features(${target} PRIVATE cxx_std_20)
    set(sanitizers -fsanitize=address,undefined)
    if(GUI_FORMS_HAS_LIBFUZZER_RUNTIME)
        set(sanitizers -fsanitize=fuzzer,address,undefined)
    else()
        target_compile_definitions(${target} PRIVATE ${fuzz_STANDALONE_DEFINITION}=1)
    endif()
    target_compile_options(${target} PRIVATE ${sanitizers} -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE ${sanitizers})
endfunction()
