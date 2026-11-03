foreach(required IN ITEMS CORE_LIBRARY SKIA_ADAPTER
                          BENCHMARK_BINARY PUBLIC_INCLUDE_DIRECTORY SKIA_ARCHIVE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

foreach(path IN ITEMS "${CORE_LIBRARY}" "${SKIA_ADAPTER}" "${BENCHMARK_BINARY}"
                      "${SKIA_ARCHIVE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Renderer audit input is absent: ${path}")
    endif()
endforeach()

execute_process(
    COMMAND nm -gU "${CORE_LIBRARY}"
    RESULT_VARIABLE core_nm_result
    OUTPUT_VARIABLE core_symbols
    ERROR_VARIABLE core_nm_error
)
if(NOT core_nm_result EQUAL 0)
    message(FATAL_ERROR "nm failed for renderer-free core: ${core_nm_error}")
endif()
foreach(pattern IN ITEMS "Sk[A-Z]" "_CG[A-Za-z]" "_CT[A-Za-z]"
                         "AppKit" "Metal" "OpenGL")
    if(core_symbols MATCHES "${pattern}")
        message(FATAL_ERROR
            "Renderer-free core contains forbidden renderer/platform symbol: ${pattern}")
    endif()
endforeach()

file(GLOB_RECURSE public_headers
    "${PUBLIC_INCLUDE_DIRECTORY}/*.h"
    "${PUBLIC_INCLUDE_DIRECTORY}/*.hpp")
foreach(header IN LISTS public_headers)
    file(READ "${header}" header_text)
    foreach(pattern IN ITEMS "Sk[A-Z]" "CoreGraphics" "CGContext"
                             "CoreText" "AppKit" "NSView" "Metal")
        if(header_text MATCHES "${pattern}")
            message(FATAL_ERROR
                "Public header ${header} exposes renderer/platform token: ${pattern}")
        endif()
    endforeach()
endforeach()

execute_process(
    COMMAND otool -L "${BENCHMARK_BINARY}"
    RESULT_VARIABLE otool_result
    OUTPUT_VARIABLE dependencies
    ERROR_VARIABLE otool_error
)
if(NOT otool_result EQUAL 0)
    message(FATAL_ERROR "otool failed for benchmark binary: ${otool_error}")
endif()
foreach(pattern IN ITEMS "Metal.framework" "MetalKit.framework"
                         "OpenGL.framework" "WebKit.framework")
    if(dependencies MATCHES "${pattern}")
        message(FATAL_ERROR "Forbidden runtime dependency found: ${pattern}")
    endif()
endforeach()

file(SIZE "${CORE_LIBRARY}" core_bytes)
file(SIZE "${SKIA_ADAPTER}" skia_adapter_bytes)
file(SIZE "${SKIA_ARCHIVE}" skia_archive_bytes)
file(SIZE "${BENCHMARK_BINARY}" benchmark_bytes)

set(report
"renderer_boundary_audit=passed\ncore_bytes=${core_bytes}\nskia_adapter_bytes=${skia_adapter_bytes}\nskia_archive_bytes=${skia_archive_bytes}\nbenchmark_bytes=${benchmark_bytes}\ndependencies:\n${dependencies}")
if(DEFINED OUTPUT_FILE)
    file(WRITE "${OUTPUT_FILE}" "${report}")
endif()
message(STATUS "${report}")
