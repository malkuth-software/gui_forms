# The C ABI as a Visual Studio program sees it: MSVC's cl compiles the layout
# probe (every size and offset of the manifest, asserted by MSVC's own layout),
# the reference consumer and the C++ wrappers' test (gf_abi.hpp at C++20)
# against the import library, and each runs against the built DLL. Run with
# cmake -P from ctest.
#
# Inputs (-D): VCVARS (vcvars64.bat), SOURCE_DIR (the gui_forms checkout),
# IMPORT_LIBRARY (gui_forms_abi1.lib), LIBRARY_DIR (the directory holding the
# DLL and the libraries it loads), WORK_DIR (scratch, rewritten each run).
foreach(required VCVARS SOURCE_DIR IMPORT_LIBRARY LIBRARY_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "gf_abi_msvc_consumer: ${required} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
get_filename_component(import_directory "${IMPORT_LIBRARY}" DIRECTORY)
get_filename_component(import_name "${IMPORT_LIBRARY}" NAME)
file(TO_NATIVE_PATH "${VCVARS}" vcvars_native)
file(TO_NATIVE_PATH "${SOURCE_DIR}/include" include_native)
file(TO_NATIVE_PATH "${SOURCE_DIR}/tests/abi/gf_abi_layout_probe.c" probe_native)
file(TO_NATIVE_PATH "${SOURCE_DIR}/examples/reference/c_abi_consumer.c" consumer_native)
file(TO_NATIVE_PATH "${SOURCE_DIR}/tests/abi/cpp/gf_abi_cpp_wrappers.cpp" wrappers_native)
file(TO_NATIVE_PATH "${import_directory}" import_native)
file(TO_NATIVE_PATH "${LIBRARY_DIR}" library_native)

# /W4 /WX: the headers must compile warning-free under MSVC's strictest common level.
set(c_flags "/nologo /W4 /WX /std:c11 /I \"${include_native}\"")
set(cpp_flags "/nologo /W4 /WX /std:c++20 /EHsc /I \"${include_native}\"")
set(link_flags "/link /LIBPATH:\"${import_native}\" ${import_name}")
file(WRITE "${WORK_DIR}/build.bat"
    "@echo off\r\n"
    "call \"${vcvars_native}\" >nul || exit /b 1\r\n"
    "cl ${c_flags} /Fe:layout_probe.exe \"${probe_native}\" || exit /b 1\r\n"
    "cl ${c_flags} /Fe:c_abi_consumer.exe \"${consumer_native}\" ${link_flags} || exit /b 1\r\n"
    "cl ${cpp_flags} /Fe:cpp_wrappers.exe \"${wrappers_native}\" ${link_flags} || exit /b 1\r\n"
    "exit /b 0\r\n")
# The script by its full path: a shell may have dropped the working directory
# from cmd's program search (NoDefaultCurrentDirectoryInExePath).
file(TO_NATIVE_PATH "${WORK_DIR}/build.bat" build_script)
execute_process(COMMAND cmd /c "${build_script}" WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE build_result
                OUTPUT_VARIABLE build_output ERROR_VARIABLE build_output)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "gf_abi_msvc_consumer: MSVC did not build the consumers:\n${build_output}")
endif()

set(ENV{PATH} "${library_native};$ENV{PATH}")
foreach(program layout_probe c_abi_consumer cpp_wrappers)
    execute_process(COMMAND "${WORK_DIR}/${program}.exe" WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE run_result
                    OUTPUT_VARIABLE run_output ERROR_VARIABLE run_output)
    message(STATUS "${program}: ${run_output}")
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "gf_abi_msvc_consumer: ${program} failed (${run_result})")
    endif()
endforeach()
