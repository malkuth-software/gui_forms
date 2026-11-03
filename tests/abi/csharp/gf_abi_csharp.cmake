# The C# interop's test: checks the generated files are current, builds the
# interop and its test program offline with the .NET SDK (an empty package
# source, no package references), and runs it against the built library. Run
# with cmake -P from ctest.
#
# Inputs (-D): DOTNET (the dotnet program), PYTHON, SOURCE_DIR (the gui_forms
# checkout), LIBRARY_DIR (the directory holding the DLL and the libraries it
# loads), WORK_DIR (scratch: obj/, bin/ and the empty package source).
foreach(required DOTNET PYTHON SOURCE_DIR LIBRARY_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "gf_abi_csharp: ${required} is not set")
    endif()
endforeach()

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
