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
set(project "${WORK_DIRECTORY}/project")
set(build "${WORK_DIRECTORY}/build")
file(REMOVE_RECURSE "${WORK_DIRECTORY}")
file(MAKE_DIRECTORY "${WORK_DIRECTORY}")
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
# The changed document must be strictly newer than the header the first
# build generated, or the build tool may take it as unchanged: on a
# filesystem with coarse timestamps (one second on HFS+, two on FAT) a fast
# first build (cached objects) leaves the rewrite in the header's tick. So
# the rewrite waits until the clock is two whole seconds past the header's
# timestamp, the coarsest granularity a build tree may sit on, and checks
# the document's timestamp before building.
set(header "${build}/gui_forms_forms/sample_form.hpp")
if(NOT EXISTS "${header}")
    message(FATAL_ERROR "form shared source test: the first build generated no ${header}")
endif()
file(TIMESTAMP "${header}" header_seconds "%s" UTC)
math(EXPR rewrite_after "${header_seconds} + 2")
string(TIMESTAMP now_seconds "%s" UTC)
while(now_seconds LESS_EQUAL rewrite_after)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.25)
    string(TIMESTAMP now_seconds "%s" UTC)
endwhile()
file(WRITE "${project}/sample.form" "9\n")
file(TIMESTAMP "${project}/sample.form" document_seconds "%s" UTC)
if(NOT document_seconds GREATER header_seconds)
    message(FATAL_ERROR "form shared source test: the rewritten document (${document_seconds}) is not newer "
                        "than the generated header (${header_seconds}); the filesystem's clock is behind")
endif()
build_and_read("9" "after the document changed")
