# The pinned FreeType and HarfBuzz that GUI.Forms links, installed for the
# products that call them directly (Pocket Library's page compositor includes
# ft2build.h and hb.h). GUI_FORMS_INSTALL_TEXT_STACK is off by default and only
# `tools/build_sdk.py --features all` turns it on, so the default SDK keeps
# them private to the text engine.
#
# Installed form: the two static libraries GUI.Forms links, from the stx
# package (cmake/stx_package/, both compiled with GUI.Forms' options, FreeType
# without zlib: cmake/freetype/gui_forms_ftoption.h), the upstream header
# layouts the package installed (include/freetype2 and include/harfbuzz), and
# one targets file, GUIFormsTextStackTargets.cmake (from
# GUIFormsTextStackTargets.cmake.in), importing them as GUIForms::FreeType and
# GUIForms::HarfBuzz. GUIForms::HarfBuzz links GUIForms::FreeType. The
# licences are installed with the text engine's (CMakeLists.txt,
# HarfBuzz-COPYING.txt and FreeType-FTL.txt) and listed in THIRD_PARTY_NOTICES.md.



if(GUI_FORMS_INSTALL_TEXT_STACK)
    if(NOT TARGET stx_text::freetype OR NOT TARGET stx_text::harfbuzz)
        message(FATAL_ERROR "GUI_FORMS_INSTALL_TEXT_STACK requires GUI_FORMS_ENABLE_HARFBUZZ_TEXT")
    endif()
    install(DIRECTORY "${GUI_FORMS_STX_BUILD_PREFIX_DIR}/include/freetype2"
        "${GUI_FORMS_STX_BUILD_PREFIX_DIR}/include/harfbuzz"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES $<TARGET_FILE:stx_text::freetype> $<TARGET_FILE:stx_text::harfbuzz>
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
    # The targets file names the archives as the package built them.
    configure_file("${CMAKE_CURRENT_LIST_DIR}/GUIFormsTextStackTargets.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/GUIFormsTextStackTargets.cmake.generate" @ONLY)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/GUIFormsTextStackTargets.cmake"
        INPUT "${CMAKE_CURRENT_BINARY_DIR}/GUIFormsTextStackTargets.cmake.generate")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/GUIFormsTextStackTargets.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
endif()
