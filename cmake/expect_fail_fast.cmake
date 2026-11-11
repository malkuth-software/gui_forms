# Runs a fail-fast test mode and passes only if the program ended through GUI.Forms'
# fail-fast path: it must not exit with status 0, its output must carry the expected
# "GUI.Forms: fatal exception at <site>: <text>" line, and it must never print "survived".
#
# fail_fast() aborts. On Windows abort() ends the process with status 3, an ordinary exit
# status; on Linux and macOS it is SIGABRT, which execute_process reports as a string
# ("Child aborted", "Subprocess aborted", "SIGABRT"). CTest itself counts a signal as a
# failure whatever PASS_REGULAR_EXPRESSION matches, so the modes run through this script.
#
# Usage: cmake -DPROGRAM=<path> -DMODE=<argument> -DEXPECTED=<regex> -P expect_fail_fast.cmake
if(NOT PROGRAM OR NOT MODE OR NOT EXPECTED)
    message(FATAL_ERROR "expect_fail_fast.cmake: PROGRAM, MODE and EXPECTED are required")
endif()
execute_process(COMMAND "${PROGRAM}" "${MODE}" RESULT_VARIABLE result
                OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(result STREQUAL "0")
    message(FATAL_ERROR "${PROGRAM} ${MODE} exited with status 0; expected the fail-fast abort\n${output}")
endif()
if(output MATCHES "survived")
    message(FATAL_ERROR "${PROGRAM} ${MODE} printed \"survived\"\n${output}")
endif()
if(NOT output MATCHES "${EXPECTED}")
    message(FATAL_ERROR "${PROGRAM} ${MODE} ended with \"${result}\" without the expected report\n${output}")
endif()
message(STATUS "${PROGRAM} ${MODE} failed fast: ${result}")
