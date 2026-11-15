# The embedded font pack (font pack stage 4): every bundled face of
# assets/fonts/bundled-fonts.sha256, packed by the stx_fontpack tool into the
# groups of assets/fonts/font-pack.manifest and compiled into Core as a byte
# array (src/core/resources/fonts/bundled_font_pack.cpp). It is the only font
# source the GUI.Forms runtime and GUI.Drawing's raster C API read in a
# shipped program. A build configured
# with GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY also reads a font directory the
# hosts find (GUI_FORMS_FONT_DIR first), for work on the faces themselves.
#
# The pack is verified twice: the tool's pack command loads its own output and
# compares every face with its input before it writes anything, and the test
# gui_forms_font_pack_round_trip loads the built pack again against the files.
# At run time each face is checked against the CRC-32 the pack stores for it.
#
# With GUI_FORMS_INSTALL_FALLBACK_FONTS off, the pack leaves out the Noto
# fallback faces (font-pack-without-fallback.manifest) and the install their
# licences. No face file is installed: only the licences and attribution.
#
# A cross build cannot run the tool it compiles, so it names a host-built one
# in GUI_FORMS_FONTPACK_TOOL.
#
# Needs: GUI_FORMS_BUNDLED_FONT_MANIFEST, GUI_FORMS_INSTALL_FALLBACK_FONTS and
# GUI_FORMS_FALLBACK_FONT_FILES (CMakeLists.txt), and the stx_fontpack_tool
# target (stx_fontpack/). Sets GUI_FORMS_FONT_PACK_RECORDS, the generated
# initializer, and GUI_FORMS_FONT_PACK_FACES, the packed files.

option(GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY
       "Development builds only: read the bundled faces from a font directory the hosts find (GUI_FORMS_FONT_DIR first) before the embedded pack"
       OFF)
set(GUI_FORMS_FONTPACK_TOOL "" CACHE FILEPATH
    "A host-built stx_fontpack tool to run in place of the one this build compiles (cross builds)")
if(GUI_FORMS_FONTPACK_TOOL)
    set(_gf_fontpack_tool ${GUI_FORMS_FONTPACK_TOOL})
else()
    # A target name in COMMAND and DEPENDS runs its file and orders the build.
    set(_gf_fontpack_tool stx_fontpack_tool)
endif()

set(_gf_font_source ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts)
if(GUI_FORMS_INSTALL_FALLBACK_FONTS)
    set(GUI_FORMS_FONT_PACK_MANIFEST ${_gf_font_source}/font-pack.manifest)
else()
    set(GUI_FORMS_FONT_PACK_MANIFEST ${_gf_font_source}/font-pack-without-fallback.manifest)
endif()

# The packed faces: the face manifest's files, in its order, less the fallback
# faces when they are left out. Read at configure time; editing the manifest
# reconfigures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GUI_FORMS_BUNDLED_FONT_MANIFEST})
file(STRINGS ${GUI_FORMS_BUNDLED_FONT_MANIFEST} _gf_face_lines)
set(GUI_FORMS_FONT_PACK_FACES)
foreach(_gf_raw_line IN LISTS _gf_face_lines)
    string(REPLACE "\r" "" _gf_line "${_gf_raw_line}")
    if(NOT _gf_line MATCHES "^[0-9a-f]+  ([A-Za-z0-9._-]+)$")
        message(FATAL_ERROR "Bundled font manifest line is malformed: ${_gf_line}")
    endif()
    set(_gf_face ${CMAKE_MATCH_1})
    if(NOT GUI_FORMS_INSTALL_FALLBACK_FONTS AND _gf_face IN_LIST GUI_FORMS_FALLBACK_FONT_FILES)
        continue()
    endif()
    list(APPEND GUI_FORMS_FONT_PACK_FACES ${_gf_font_source}/${_gf_face})
endforeach()

set(GUI_FORMS_FONT_PACK ${CMAKE_CURRENT_BINARY_DIR}/generated/bundled_fonts/bundled_fonts.fpk)
set(GUI_FORMS_FONT_PACK_RECORDS ${CMAKE_CURRENT_BINARY_DIR}/generated/bundled_fonts/bundled_font_pack.inc)
add_custom_command(
    OUTPUT ${GUI_FORMS_FONT_PACK}
    # The tool opens its output with fopen, which makes no directory.
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated/bundled_fonts
    COMMAND ${_gf_fontpack_tool} pack ${GUI_FORMS_FONT_PACK}
        --manifest ${GUI_FORMS_FONT_PACK_MANIFEST} ${GUI_FORMS_FONT_PACK_FACES}
    DEPENDS
        ${GUI_FORMS_FONT_PACK_FACES}
        ${GUI_FORMS_FONT_PACK_MANIFEST}
        ${_gf_fontpack_tool}
    COMMENT "Packing the bundled fonts"
    VERBATIM)
add_custom_command(
    OUTPUT ${GUI_FORMS_FONT_PACK_RECORDS}
    COMMAND ${_gf_fontpack_tool} embed ${GUI_FORMS_FONT_PACK} ${GUI_FORMS_FONT_PACK_RECORDS}
    DEPENDS ${GUI_FORMS_FONT_PACK} ${_gf_fontpack_tool}
    COMMENT "Embedding the bundled font pack"
    VERBATIM)

if(GUI_FORMS_BUILD_TESTS)
    if(GUI_FORMS_FONTPACK_TOOL)
        set(_gf_fontpack_tool_file ${GUI_FORMS_FONTPACK_TOOL})
    else()
        set(_gf_fontpack_tool_file $<TARGET_FILE:stx_fontpack_tool>)
    endif()
    add_test(NAME gui_forms_font_pack_round_trip
             COMMAND ${_gf_fontpack_tool_file} verify ${GUI_FORMS_FONT_PACK} ${GUI_FORMS_FONT_PACK_FACES})
endif()
