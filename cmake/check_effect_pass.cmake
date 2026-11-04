# One case of the effect pass's tests (CMakeLists.txt, tests/effect_pass):
# builds TARGET, the case's object library, in BUILD_DIR after removing the
# objects under OBJECT_DIR (relative to BUILD_DIR, so that the test names no
# build output: tools/ci_build.py would build a named output before the test,
# and a rejected case's object fails to build by design), so the compiler
# runs every time, and matches
# the output against the case's head line in SOURCE:
#   // expect: reject <regex>   the compile fails with a diagnostic matching <regex>;
#   // expect: accept           the compile succeeds without an error;
#   // expect: warn <regex>     the compile succeeds with a diagnostic matching <regex>.
foreach(_gf_required BUILD_DIR TARGET SOURCE)
    if(NOT DEFINED ${_gf_required})
        message(FATAL_ERROR "check_effect_pass.cmake needs -D${_gf_required}=...")
    endif()
endforeach()
file(STRINGS "${SOURCE}" _gf_head LIMIT_COUNT 1)
if(NOT _gf_head MATCHES "^// expect: (reject|accept|warn) ?(.*)$")
    message(FATAL_ERROR "${SOURCE} does not begin with an `// expect:` line")
endif()
set(_gf_mode "${CMAKE_MATCH_1}")
set(_gf_pattern "${CMAKE_MATCH_2}")
if(DEFINED OBJECT_DIR)
    file(GLOB_RECURSE _gf_objects "${BUILD_DIR}/${OBJECT_DIR}/*.o" "${BUILD_DIR}/${OBJECT_DIR}/*.obj")
    if(_gf_objects)
        file(REMOVE ${_gf_objects})
    endif()
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target "${TARGET}"
                RESULT_VARIABLE _gf_status OUTPUT_VARIABLE _gf_output ERROR_VARIABLE _gf_output)
message("${_gf_output}")
if(_gf_mode STREQUAL "reject")
    if(_gf_status EQUAL 0)
        message(FATAL_ERROR "the effect pass accepted ${SOURCE}; expected a diagnostic matching: ${_gf_pattern}")
    endif()
    if(NOT _gf_output MATCHES "${_gf_pattern}")
        message(FATAL_ERROR "the compile of ${SOURCE} failed without a diagnostic matching: ${_gf_pattern}")
    endif()
elseif(_gf_mode STREQUAL "accept")
    if(NOT _gf_status EQUAL 0 OR _gf_output MATCHES "error:")
        message(FATAL_ERROR "the effect pass rejected ${SOURCE}")
    endif()
else()
    if(NOT _gf_status EQUAL 0 OR NOT _gf_output MATCHES "${_gf_pattern}")
        message(FATAL_ERROR "the compile of ${SOURCE} did not succeed with a diagnostic matching: ${_gf_pattern}")
    endif()
endif()
message("effect pass case ${TARGET}: ${_gf_mode} as expected")
