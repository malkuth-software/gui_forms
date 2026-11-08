# Source-only Unicode 17 line breaking for the text engine. No headers or target are exported.
# libunibreak 8.0 at its pin, from the stx source's third_party/ (cmake/Stx.cmake).
set(GUI_FORMS_LINEBREAK_SOURCE "${GUI_FORMS_STX_THIRD_PARTY_DIR}/libunibreak/src")
if(NOT EXISTS "${GUI_FORMS_LINEBREAK_SOURCE}/linebreak.c")
    message(FATAL_ERROR "The stx source at ${GUI_FORMS_STX_SOURCE_DIR} has no third_party/libunibreak")
endif()
add_library(gui_forms_linebreak STATIC
    "${GUI_FORMS_LINEBREAK_SOURCE}/unibreakbase.c"
    "${GUI_FORMS_LINEBREAK_SOURCE}/unibreakdef.c"
    "${GUI_FORMS_LINEBREAK_SOURCE}/linebreak.c"
    "${GUI_FORMS_LINEBREAK_SOURCE}/linebreakdata.c"
    "${GUI_FORMS_LINEBREAK_SOURCE}/linebreakdef.c"
    "${GUI_FORMS_LINEBREAK_SOURCE}/eastasianwidthdef.c"
    "${GUI_FORMS_LINEBREAK_SOURCE}/emojidef.c")
target_include_directories(gui_forms_linebreak SYSTEM PUBLIC "${GUI_FORMS_LINEBREAK_SOURCE}")
set_target_properties(gui_forms_linebreak PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(GUI_FORMS_BUILD_TESTS)
    # Upstream driver references all three algorithms. Word/grapheme objects are
    # test-only; the production dependency remains the line-break subset above.
    add_executable(gui_forms_linebreak_conformance_tests
        "${GUI_FORMS_LINEBREAK_SOURCE}/tests.c"
        "${GUI_FORMS_LINEBREAK_SOURCE}/wordbreak.c"
        "${GUI_FORMS_LINEBREAK_SOURCE}/graphemebreak.c")
    target_link_libraries(gui_forms_linebreak_conformance_tests PRIVATE gui_forms_linebreak)
    add_test(NAME gui_forms_linebreak_conformance
        COMMAND gui_forms_linebreak_conformance_tests line)
    set_tests_properties(gui_forms_linebreak_conformance PROPERTIES
        WORKING_DIRECTORY "${GUI_FORMS_LINEBREAK_SOURCE}" TIMEOUT 30)
endif()
