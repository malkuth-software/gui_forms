# Runs a macOS test bundle through LaunchServices (`open`) so that it is an
# activated application whose window can become key. A process started
# directly from a shell outside the console session (ssh, some CI agents)
# cannot activate and receives no keyboard focus.
#
# `open -W` waits for the program but does not return its exit status, and it
# does not pass the caller's environment on. The sanitizer options are
# forwarded with --env, and the run passes only when the program's last
# output line is "failures=0" and its standard error holds no sanitizer
# report.
#
# Inputs: BUNDLE, the .app directory; OUTPUT_DIRECTORY, where the program's
# standard output and error are kept (written first in the temporary
# directory, see below).

if(NOT IS_DIRECTORY "${BUNDLE}")
    message(FATAL_ERROR "Test bundle is missing: ${BUNDLE}")
endif()
get_filename_component(bundle_name "${BUNDLE}" NAME_WE)
set(stdout_file "${OUTPUT_DIRECTORY}/${bundle_name}.stdout.txt")
set(stderr_file "${OUTPUT_DIRECTORY}/${bundle_name}.stderr.txt")
file(REMOVE "${stdout_file}" "${stderr_file}")
# LaunchServices opens --stdout and --stderr for the launched program, which
# macOS refuses on a removable volume (a build tree on an external disk:
# privacy approval per application, error -10810): the program writes into
# the temporary directory, and the files are copied beside the build after.
set(launch_directory "$ENV{TMPDIR}")
if(launch_directory STREQUAL "")
    set(launch_directory "/tmp")
endif()
string(RANDOM LENGTH 12 launch_suffix)
set(launch_directory "${launch_directory}/gui_forms_activated_${bundle_name}_${launch_suffix}")
file(MAKE_DIRECTORY "${launch_directory}")
set(launch_stdout "${launch_directory}/stdout.txt")
set(launch_stderr "${launch_directory}/stderr.txt")

set(open_arguments -W -n --stdout "${launch_stdout}" --stderr "${launch_stderr}")
foreach(variable IN ITEMS ASAN_OPTIONS UBSAN_OPTIONS LSAN_OPTIONS)
    if(DEFINED ENV{${variable}})
        list(APPEND open_arguments --env "${variable}=$ENV{${variable}}")
    endif()
endforeach()

execute_process(
    COMMAND open ${open_arguments} "${BUNDLE}"
    RESULT_VARIABLE open_result
    TIMEOUT 120)

foreach(stream IN ITEMS stdout stderr)
    if(EXISTS "${launch_directory}/${stream}.txt")
        file(COPY_FILE "${launch_directory}/${stream}.txt" "${OUTPUT_DIRECTORY}/${bundle_name}.${stream}.txt")
    endif()
endforeach()
file(REMOVE_RECURSE "${launch_directory}")
set(program_output "")
set(program_errors "")
if(EXISTS "${stdout_file}")
    file(READ "${stdout_file}" program_output)
endif()
if(EXISTS "${stderr_file}")
    file(READ "${stderr_file}" program_errors)
endif()
message("${program_output}")
if(NOT program_errors STREQUAL "")
    message("${program_errors}")
endif()

if(NOT open_result EQUAL 0)
    message(FATAL_ERROR "open failed for ${BUNDLE}: ${open_result}")
endif()
if(program_errors MATCHES "Sanitizer|runtime error")
    message(FATAL_ERROR "${bundle_name} reported a sanitizer finding")
endif()
if(NOT program_output MATCHES "\nfailures=0\n$")
    message(FATAL_ERROR "${bundle_name} did not finish with failures=0")
endif()
