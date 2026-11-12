# GUI.Forms Correspondence: the fixture-backed mail-client demonstration
# (POLISH_STAGE_PLAN W5.3, Modern.Forms "Outlaw" equivalent). Included from the
# root CMakeLists.txt inside if(GUI_FORMS_BUILD_GALLERY), after the Gallery and
# Showcase targets, so every path below is relative to the repository root and
# GUI_FORMS_GALLERY_FONTS / GUI_FORMS_GALLERY_FONT_NOTICES are already set.

add_library(gui_forms_correspondence_model STATIC
    demo/correspondence/correspondence_model.cpp
    demo/correspondence/correspondence_controls.cpp
    demo/correspondence/correspondence.cpp
)
target_include_directories(gui_forms_correspondence_model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/demo
    ${CMAKE_CURRENT_SOURCE_DIR}/demo/correspondence
)
target_link_libraries(gui_forms_correspondence_model PUBLIC gui_forms_lab)

if(GUI_FORMS_ENABLE_MACOS_HOST)
    set_source_files_properties(
        ${GUI_FORMS_GALLERY_FONTS}
        ${GUI_FORMS_GALLERY_FONT_NOTICES}
        PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/fonts"
    )
    add_executable(gui_forms_correspondence MACOSX_BUNDLE
        src/host/macos/correspondence_main.mm
        ${GUI_FORMS_GALLERY_FONTS}
        ${GUI_FORMS_GALLERY_FONT_NOTICES}
    )
    target_compile_options(gui_forms_correspondence PRIVATE -fobjc-arc)
    target_link_libraries(gui_forms_correspondence PRIVATE
        gui_forms_correspondence_model
        gui_forms_host_macos
    )
    set_target_properties(gui_forms_correspondence PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "GUI.Forms Correspondence"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.guiforms.correspondence"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.2"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        OUTPUT_NAME "GUI.Forms Correspondence"
    )
endif()

if(GUI_FORMS_ENABLE_WINDOWS_HOST)
    add_executable(gui_forms_correspondence_windows
        src/host/windows/correspondence_main.cpp
    )
    target_link_libraries(gui_forms_correspondence_windows PRIVATE
        gui_forms_correspondence_model
        gui_forms_host_windows
    )
    target_link_options(gui_forms_correspondence_windows PRIVATE
        -static
    )
    set_target_properties(gui_forms_correspondence_windows PROPERTIES
        OUTPUT_NAME "GUI.Forms Correspondence"
    )
endif()

if(GUI_FORMS_ENABLE_LINUX_HOST)
    add_executable(gui_forms_correspondence_linux
        src/host/linux/correspondence_main.cpp
    )
    target_link_libraries(gui_forms_correspondence_linux PRIVATE
        gui_forms_correspondence_model
        gui_forms_host_linux
    )
    set_target_properties(gui_forms_correspondence_linux PROPERTIES
        OUTPUT_NAME "gui-forms-correspondence"
    )
endif()

# The Windows and X11 hosts discover bundled faces in <executable dir>/fonts;
# CMakeLists.txt stages them there for every program that bundles fonts.

if(GUI_FORMS_BUILD_TESTS)
    enable_testing()
    add_executable(gui_forms_correspondence_tests
        tests/correspondence_tests.cpp
    )
    target_link_libraries(gui_forms_correspondence_tests PRIVATE
        gui_forms_correspondence_model
    )
    add_test(NAME gui_forms_correspondence_tests
             COMMAND gui_forms_correspondence_tests)

    add_executable(gui_forms_correspondence_interaction_tests
        tests/correspondence_interaction_tests.cpp
    )
    target_link_libraries(gui_forms_correspondence_interaction_tests PRIVATE
        gui_forms_correspondence_model
    )
    add_test(NAME gui_forms_correspondence_interaction_tests
             COMMAND gui_forms_correspondence_interaction_tests)

    if(TARGET gui_forms_correspondence)
        add_test(
            NAME gui_forms_correspondence_font_policy
            COMMAND ${CMAKE_COMMAND}
                -DFONT_DIRECTORY=$<TARGET_BUNDLE_DIR:gui_forms_correspondence>/Contents/Resources/fonts
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_gallery_fonts.cmake
        )
    endif()
endif()
