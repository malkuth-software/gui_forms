# The libunibreak 8.0 conformance driver of the stx package
# (stx_text::linebreak_conformance, compiled with the line-break subset the
# text engine links plus upstream's word and grapheme objects), run over
# upstream's UAX #14 test data, which the package installs beside it. No
# headers or target are exported.
if(GUI_FORMS_BUILD_TESTS)
    add_test(NAME gui_forms_linebreak_conformance
        COMMAND stx_text::linebreak_conformance line)
    set_tests_properties(gui_forms_linebreak_conformance PROPERTIES
        WORKING_DIRECTORY "${GUI_FORMS_STX_BUILD_PREFIX_DIR}/share/stx_text/linebreak" TIMEOUT 30)
endif()
