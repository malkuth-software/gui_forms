execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${SOURCE}" -B "${BINARY}" -G Ninja -C "${CACHE}"
    "-DGUI_FORMS_SOURCE_DIR=${PROVIDER}" "-DLINK_CASE=${LINK_CASE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${BINARY}/configure-result.log" "${output}${error}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Application + TextMasks + Audio rejected: ${output}${error}")
endif()
