# Public headers name no renderer or platform type. Every header under
# PUBLIC_INCLUDE_DIRECTORY is read with its comments removed (// to the end
# of the line, and /* ... */ blocks), so a comment that names AppKit or Skia
# to say the header does not use them passes; a token in the code fails.
# Run directly with -P, or include()d by check_renderer_boundaries.cmake.
if(NOT DEFINED PUBLIC_INCLUDE_DIRECTORY)
    message(FATAL_ERROR "PUBLIC_INCLUDE_DIRECTORY is required")
endif()

file(GLOB_RECURSE public_headers
    "${PUBLIC_INCLUDE_DIRECTORY}/*.h"
    "${PUBLIC_INCLUDE_DIRECTORY}/*.hpp")
if(NOT public_headers)
    message(FATAL_ERROR "No public headers under ${PUBLIC_INCLUDE_DIRECTORY}")
endif()
foreach(header IN LISTS public_headers)
    file(READ "${header}" header_text)
    # Blocks first, so a // inside a block goes with it. CMake's regular
    # expressions have no lazy match, so each /* is paired with the next */.
    # An unclosed block runs to the end of the file.
    string(FIND "${header_text}" "/*" block_start)
    while(NOT block_start EQUAL -1)
        string(SUBSTRING "${header_text}" 0 ${block_start} before_block)
        math(EXPR after_opening "${block_start} + 2")
        string(SUBSTRING "${header_text}" ${after_opening} -1 rest)
        string(FIND "${rest}" "*/" block_end)
        if(block_end EQUAL -1)
            set(rest "")
        else()
            math(EXPR after_block "${block_end} + 2")
            string(SUBSTRING "${rest}" ${after_block} -1 rest)
        endif()
        set(header_text "${before_block} ${rest}")
        string(FIND "${header_text}" "/*" block_start)
    endwhile()
    string(REGEX REPLACE "//[^\n]*" "" header_text "${header_text}")
    foreach(pattern IN ITEMS "Sk[A-Z]" "CoreGraphics" "CGContext"
                             "CoreText" "AppKit" "NSView" "Metal")
        if(header_text MATCHES "${pattern}")
            message(FATAL_ERROR
                "Public header ${header} exposes renderer/platform token: ${pattern}")
        endif()
    endforeach()
endforeach()
