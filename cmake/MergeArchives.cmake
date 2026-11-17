# Merges static archives into one: cmake -DOUTPUT=<archive> -DINPUTS=<a;b;...>
# -DTOOL=<merger> -DKIND=<libtool|mri> -P cmake/MergeArchives.cmake.
#
# Inputs: OUTPUT, the archive written (replaced whole); INPUTS, the archives
# whose members it holds, in order (a member name repeated across inputs is
# kept twice: the linker reads both); TOOL, Apple's libtool (KIND libtool) or
# llvm-ar or GNU ar (KIND mri, reading an MRI script). Failure: the configure-time
# FATAL_ERROR names the tool's output, and OUTPUT is left absent, never half
# written (the tool writes a temporary name, renamed when it succeeds).
foreach(_gf_variable IN ITEMS OUTPUT INPUTS TOOL KIND)
    if(NOT DEFINED ${_gf_variable} OR "${${_gf_variable}}" STREQUAL "")
        message(FATAL_ERROR "MergeArchives.cmake needs ${_gf_variable}")
    endif()
endforeach()
set(_gf_partial "${OUTPUT}.partial")
file(REMOVE "${OUTPUT}" "${_gf_partial}")
if(KIND STREQUAL "libtool")
    execute_process(COMMAND "${TOOL}" -static -no_warning_for_no_symbols -o "${_gf_partial}" ${INPUTS}
                    RESULT_VARIABLE _gf_result OUTPUT_VARIABLE _gf_output ERROR_VARIABLE _gf_output)
elseif(KIND STREQUAL "mri")
    set(_gf_script "CREATE ${_gf_partial}\n")
    foreach(_gf_input IN LISTS INPUTS)
        string(APPEND _gf_script "ADDLIB ${_gf_input}\n")
    endforeach()
    string(APPEND _gf_script "SAVE\nEND\n")
    file(WRITE "${OUTPUT}.mri" "${_gf_script}")
    execute_process(COMMAND "${TOOL}" -M INPUT_FILE "${OUTPUT}.mri"
                    RESULT_VARIABLE _gf_result OUTPUT_VARIABLE _gf_output ERROR_VARIABLE _gf_output)
else()
    message(FATAL_ERROR "MergeArchives.cmake: KIND is libtool or mri, not ${KIND}")
endif()
if(NOT _gf_result EQUAL 0 OR NOT EXISTS "${_gf_partial}")
    file(REMOVE "${_gf_partial}")
    message(FATAL_ERROR "merging ${OUTPUT} failed (${_gf_result}): ${_gf_output}")
endif()
file(RENAME "${_gf_partial}" "${OUTPUT}")
