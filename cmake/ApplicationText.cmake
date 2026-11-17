# Development text implementations use static Core for provider-only tests.
# Native consumers use the same implementation inside Application, linked
# whole with the engine's archives. Keep the old source-target names as
# consumer interfaces.
function(gui_forms_application_text consumer implementation public_name)
    if(NOT TARGET ${implementation})
        return()
    endif()
    add_library(${consumer} INTERFACE)
    add_library(GUIForms::${public_name} ALIAS ${consumer})
    if(NOT TARGET gui_forms_application)
        target_link_libraries(${consumer} INTERFACE ${implementation})
        return()
    endif()

    # Include every public entry point, even when the host itself never calls
    # it: the implementation joins the archives Application links whole.
    gui_forms_application_add_archives(gui_forms_application ${implementation})
    # Application is not an exported target since one module (the SDK's
    # GUIForms::Application is an interface over the merged archives, and an
    # install interface naming it would resolve to the build tree's alias of
    # the unexported library). The build tree links the library; the SDK's
    # GUIFormsConfig.cmake links GUIForms::TextMasks to GUIForms::Application
    # once both are imported.
    target_link_libraries(${consumer} INTERFACE $<BUILD_INTERFACE:gui_forms_application>)
endfunction()

gui_forms_application_text(gui_forms_text_masks gui_forms_text_masks_impl TextMasks)
gui_forms_application_text(gui_forms_prepared_text gui_forms_prepared_text_impl PreparedText)

# With a native host the text masks are inside the Application library, and
# GUIForms::TextMasks is an interface on it: that installs as the TextMasks
# component with the text-mask headers. A hostless build's TextMasks carries
# a static Core and the private text engine archives, which the SDK does not
# ship, so installing it stays refused.
if(TARGET gui_forms_text_masks AND TARGET gui_forms_application)
    set_target_properties(gui_forms_text_masks PROPERTIES EXPORT_NAME TextMasks)
    install(TARGETS gui_forms_text_masks EXPORT GUIFormsTextMasksTargets)
    install(EXPORT GUIFormsTextMasksTargets
            FILE GUIFormsTextMasksTargets.cmake
            NAMESPACE GUIForms::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
    install(FILES include/gui_forms/text_mask.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gui_forms)
    install(DIRECTORY include/gui_forms/text_mask DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gui_forms)
    # The text-mask API is written in the prepared-text vocabulary (font
    # sources, encoded font leases, the wake target), so the headers
    # text_mask_service.hpp includes install with it. The PreparedText
    # service itself, its raster header and its umbrella header do not.
    foreach(prepared_header IN ITEMS types/prepared_text_types.hpp layout/prepared_text_layout.hpp
                                     service/prepared_text_service.hpp)
        get_filename_component(prepared_directory "${prepared_header}" DIRECTORY)
        install(FILES include/gui_forms/prepared_text/${prepared_header}
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gui_forms/prepared_text/${prepared_directory})
    endforeach()
elseif(TARGET gui_forms_text_masks)
    install(CODE "message(FATAL_ERROR \"The text masks install only with a native host (Application)\")")
endif()

if(TARGET gui_forms_application AND GUI_FORMS_BUILD_PREPARED_TEXT)
    target_compile_definitions(gui_forms_application PUBLIC
        $<BUILD_INTERFACE:GUI_FORMS_PREPARED_TEXT>)
endif()

