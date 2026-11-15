# gui_forms_add_form on a target that shares a source with the form compiler
# and with another target (cp33): the project configures and builds without
# a dependency cycle, the generated header reaches the form target, and a
# changed document rebuilds it. The integrator's M4 build hit the cycle when
# the ordering was a source-file property, which every target compiling the
# shared file took. Run with:
#   -DWORK_DIRECTORY=<scratch directory> -DSOURCE_DIRECTORY=<repository root>
#   -DGENERATOR=<generator> and optionally -DMAKE_PROGRAM, -DCXX_COMPILER,
#   -DCXX_FLAGS, -DLINKER_FLAGS, -DOSX_SYSROOT, -DOSX_ARCHITECTURES and
#   -DEXECUTABLE_SUFFIX, the parent build's settings.
foreach(required IN ITEMS WORK_DIRECTORY SOURCE_DIRECTORY GENERATOR)
    if(NOT ${required})
        message(FATAL_ERROR "form shared source test: ${required} is not set")
    endif()
endforeach()
# Each run works in a directory of its own under WORK_DIRECTORY, so two runs
# in one build tree (a gate's ctest and a manual rerun) never share a
# document or a nested build. This was first suspected as the cause of the
# cp33 failure; the integrator ruled it out (one registration, ctest
# --parallel 1, nothing else in the tree), and the cause is still open. A
# run's directory is removed when the run passes and kept when it fails.
file(MAKE_DIRECTORY "${WORK_DIRECTORY}")
string(TIMESTAMP run_time "%Y%m%d%H%M%S%f" UTC)
string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" run_salt)
set(run "${WORK_DIRECTORY}/run-${run_time}-${run_salt}")
while(EXISTS "${run}")
    string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" run_salt)
    set(run "${WORK_DIRECTORY}/run-${run_time}-${run_salt}")
endwhile()
file(MAKE_DIRECTORY "${run}")
set(project "${run}/project")
set(build "${run}/build")
# A copy, so the test can change the document without touching the source tree.
file(COPY "${SOURCE_DIRECTORY}/tests/cmake/form_shared_source/" DESTINATION "${project}")

set(configure_arguments -S "${project}" -B "${build}" -G "${GENERATOR}"
    "-DGUI_FORMS_FORMS_MODULE=${SOURCE_DIRECTORY}/cmake/GUIFormsForms.cmake"
    -DCMAKE_BUILD_TYPE=Release)
if(MAKE_PROGRAM)
    list(APPEND configure_arguments "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(CXX_COMPILER)
    list(APPEND configure_arguments "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(CXX_FLAGS)
    list(APPEND configure_arguments "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}")
endif()
if(LINKER_FLAGS)
    list(APPEND configure_arguments "-DCMAKE_EXE_LINKER_FLAGS=${LINKER_FLAGS}")
endif()
if(OSX_SYSROOT)
    list(APPEND configure_arguments "-DCMAKE_OSX_SYSROOT=${OSX_SYSROOT}")
endif()
if(OSX_ARCHITECTURES)
    list(APPEND configure_arguments "-DCMAKE_OSX_ARCHITECTURES=${OSX_ARCHITECTURES}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${configure_arguments}
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "form shared source test: configuring failed:\n${output}")
endif()

# Evidence for a failure (the cp33 gate saw the second build print 7 once,
# right after a full rebuild, and ten serial reruns pass): every nested build's
# output is kept, the second build runs ninja with -d explain, and a failure
# prints both with the stamps ninja compares (the document, the header, the
# reader's object, the stand-in compiler), the header's bytes, the compiler
# launcher the nested project took from the environment (ccache on the M4),
# the reader object's recorded dependencies, and the nested .ninja_log, whose
# recorded mtimes are what a restat edge is judged by.
set_property(GLOBAL PROPERTY form_shared_source_log "")

function(append_log text)
    get_property(log GLOBAL PROPERTY form_shared_source_log)
    string(APPEND log "${text}\n")
    set_property(GLOBAL PROPERTY form_shared_source_log "${log}")
endfunction()

function(stamp_of label path)
    if(EXISTS "${path}")
        file(TIMESTAMP "${path}" stamp "%Y-%m-%dT%H:%M:%S.%f" UTC)
    else()
        set(stamp "(missing)")
    endif()
    append_log("  ${label}: ${stamp}  ${path}")
endfunction()

function(fail_with_evidence message_text)
    append_log("-- stamps (UTC, microseconds)")
    stamp_of("document" "${project}/sample.form")
    stamp_of("header" "${build}/gui_forms_forms/sample_form.hpp")
    stamp_of("reader object" "${build}/CMakeFiles/form_reader.dir/form_reader.cpp.o")
    stamp_of("reader" "${build}/form_reader${EXECUTABLE_SUFFIX}")
    stamp_of("stand-in compiler" "${build}/fake_form_compiler${EXECUTABLE_SUFFIX}")
    if(EXISTS "${build}/gui_forms_forms/sample_form.hpp")
        file(READ "${build}/gui_forms_forms/sample_form.hpp" header_bytes)
        append_log("-- header bytes\n${header_bytes}")
    endif()
    if(EXISTS "${build}/CMakeCache.txt")
        file(STRINGS "${build}/CMakeCache.txt" launcher REGEX "^CMAKE_CXX_COMPILER_LAUNCHER")
        append_log("-- launcher: ${launcher}")
    endif()
    if(EXISTS "${build}/.ninja_log")
        file(READ "${build}/.ninja_log" ninja_log)
        append_log("-- .ninja_log (start ms, end ms, recorded mtime, output, command hash)\n${ninja_log}")
    endif()
    if(GENERATOR MATCHES "Ninja")
        set(ninja ninja)
        if(MAKE_PROGRAM)
            set(ninja "${MAKE_PROGRAM}")
        endif()
        execute_process(COMMAND "${ninja}" --version OUTPUT_VARIABLE ninja_version ERROR_VARIABLE ninja_version)
        append_log("-- ninja ${ninja_version}")
        # The reader object's recorded dependencies: the header must be among
        # them, or a changed header cannot rebuild the object.
        execute_process(COMMAND "${ninja}" -C "${build}" -t deps CMakeFiles/form_reader.dir/form_reader.cpp.o
                        OUTPUT_VARIABLE deps ERROR_VARIABLE deps)
        append_log("-- ninja -t deps (reader object)\n${deps}")
    endif()
    get_property(log GLOBAL PROPERTY form_shared_source_log)
    message(FATAL_ERROR "form shared source test: ${message_text}\nKept in ${run}.\n${log}")
endfunction()

# Builds the project and runs form_reader, whose output must be `expected`.
# `explain` asks ninja why it reruns each edge (Ninja generators only).
function(build_and_read expected stage explain)
    set(build_arguments "${CMAKE_COMMAND}" --build "${build}" --config Release)
    if(explain AND GENERATOR MATCHES "Ninja")
        list(APPEND build_arguments -- -d explain)
    endif()
    execute_process(COMMAND ${build_arguments}
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    append_log("-- build (${stage}), exit ${result}\n${output}")
    if(NOT result EQUAL 0)
        fail_with_evidence("building (${stage}) failed")
    endif()
    set(reader "${build}/form_reader${EXECUTABLE_SUFFIX}")
    if(NOT EXISTS "${reader}")
        set(reader "${build}/Release/form_reader${EXECUTABLE_SUFFIX}")
    endif()
    execute_process(COMMAND "${reader}" RESULT_VARIABLE result OUTPUT_VARIABLE printed
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT result EQUAL 0)
        fail_with_evidence("form_reader (${stage}) exited ${result}")
    endif()
    if(NOT printed STREQUAL expected)
        fail_with_evidence("form_reader (${stage}) printed '${printed}', expected '${expected}'")
    endif()
endfunction()

append_log("-- configure\n${output}")
build_and_read("7" "first build" OFF)
append_log("-- before the rewrite")
stamp_of("document" "${project}/sample.form")
stamp_of("header" "${build}/gui_forms_forms/sample_form.hpp")
file(WRITE "${project}/sample.form" "9\n")
append_log("-- after the rewrite")
stamp_of("document" "${project}/sample.form")
build_and_read("9" "after the document changed" ON)
file(REMOVE_RECURSE "${run}")
