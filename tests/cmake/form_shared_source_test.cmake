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
# Each run works in a directory of its own under WORK_DIRECTORY. The
# directory was fixed (WORK_DIRECTORY itself): two runs at once in one build
# tree (a gate's ctest and a rerun, or a repeated test) shared it, and the
# second run's REMOVE_RECURSE and fresh copy put the first's document back
# to 7 between its rewrite and its second build, which then printed 7. A
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

# Builds the project and runs form_reader, whose output must be `expected`.
function(build_and_read expected stage)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "form shared source test: building (${stage}) failed:\n${output}")
    endif()
    set(reader "${build}/form_reader${EXECUTABLE_SUFFIX}")
    if(NOT EXISTS "${reader}")
        set(reader "${build}/Release/form_reader${EXECUTABLE_SUFFIX}")
    endif()
    execute_process(COMMAND "${reader}" RESULT_VARIABLE result OUTPUT_VARIABLE printed
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "form shared source test: form_reader (${stage}) exited ${result}")
    endif()
    if(NOT printed STREQUAL expected)
        message(FATAL_ERROR "form shared source test: form_reader (${stage}) printed "
                            "'${printed}', expected '${expected}'")
    endif()
endfunction()

build_and_read("7" "first build")
file(WRITE "${project}/sample.form" "9\n")
build_and_read("9" "after the document changed")
file(REMOVE_RECURSE "${run}")
