# The C# interop's test: checks the generated files are current, builds the
# interop and its test program offline with the .NET SDK (an empty package
# source, no package references), and runs it against the built library. Run
# with cmake -P from ctest.
#
# Skips (the test's SKIP_REGULAR_EXPRESSION) with a notice when the host has
# no .NET SDK of version 10 or later: nothing is downloaded or installed.
#
# Inputs (-D): DOTNET (the dotnet program, or a -NOTFOUND value), PYTHON,
# SOURCE_DIR (the gui_forms checkout), LIBRARY_DIR (the directory holding the
# DLL and the libraries it loads), WORK_DIR (scratch: obj/, bin/ and the empty
# package source).
foreach(required PYTHON SOURCE_DIR LIBRARY_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "gf_abi_csharp: ${required} is not set")
    endif()
endforeach()

# The host's SDKs: one of major 10 or later builds the net10.0 projects.
if(SANITIZED)
    message(STATUS "gui_forms_abi_csharp: SKIPPED: ASan runtime is dlopen'ed into dotnet; "
                   "interceptors unavailable; C# runs in the plain tree")
    return()
endif()

set(sdk_found FALSE)
set(sdk_list "")
if(DOTNET AND EXISTS "${DOTNET}")
    execute_process(COMMAND "${DOTNET}" --list-sdks OUTPUT_VARIABLE sdk_list ERROR_QUIET)
    string(REPLACE "\n" ";" sdk_lines "${sdk_list}")
    foreach(sdk_line IN LISTS sdk_lines)
        if(sdk_line MATCHES "^([0-9]+)\\.")
            if(CMAKE_MATCH_1 GREATER_EQUAL 10)
                set(sdk_found TRUE)
            endif()
        endif()
    endforeach()
endif()
if(NOT sdk_found)
    string(STRIP "${sdk_list}" sdk_list)
    if(sdk_list STREQUAL "")
        set(sdk_list "none")
    endif()
    message(STATUS "gui_forms_abi_csharp: SKIPPED: no .NET SDK 10 or later on this host (found: ${sdk_list}); "
                   "nothing is downloaded or installed")
    return()
endif()

execute_process(COMMAND "${PYTHON}" -B "${SOURCE_DIR}/tools/abi_bindings.py" csharp --check
                RESULT_VARIABLE check_result)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "gf_abi_csharp: the generated C# is stale")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}/empty_source")
set(ENV{DOTNET_CLI_TELEMETRY_OPTOUT} 1)
set(ENV{DOTNET_NOLOGO} 1)
set(ENV{DOTNET_SKIP_FIRST_TIME_EXPERIENCE} 1)
execute_process(
    COMMAND "${DOTNET}" build "${SOURCE_DIR}/tests/abi/csharp/GuiForms.Interop.Tests/GuiForms.Interop.Tests.csproj"
        --nologo --configuration Release --source "${WORK_DIR}/empty_source" --output "${WORK_DIR}/bin"
        -p:GfIntermediateRoot=${WORK_DIR}/obj
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_output)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "gf_abi_csharp: the C# build failed:\n${build_output}")
endif()

file(TO_NATIVE_PATH "${LIBRARY_DIR}" library_native)
if(WIN32)
    set(ENV{PATH} "${library_native};$ENV{PATH}")
elseif(APPLE)
    set(ENV{DYLD_LIBRARY_PATH} "${library_native}:$ENV{DYLD_LIBRARY_PATH}")
else()
    set(ENV{LD_LIBRARY_PATH} "${library_native}:$ENV{LD_LIBRARY_PATH}")
endif()
execute_process(COMMAND "${DOTNET}" "${WORK_DIR}/bin/GuiForms.Interop.Tests.dll"
                RESULT_VARIABLE run_result OUTPUT_VARIABLE run_output ERROR_VARIABLE run_output)
message(STATUS "${run_output}")
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "gf_abi_csharp: the C# checks failed (${run_result})")
endif()
