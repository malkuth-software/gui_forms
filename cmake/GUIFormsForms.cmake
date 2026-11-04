# Form documents in a CMake build (stage FD, phase FD6.1): the form compiler
# generates each document's C++ header at build time. Included by the GUI.Forms
# build and by an installed SDK's GUIFormsConfig.cmake, so a consumer writes:
#
#   find_package(GUIForms CONFIG REQUIRED)
#   gui_forms_add_form(my_app forms/settings.form CLASS SettingsForm NAMESPACE my_app
#                      TYPES "note_card,my_app::NoteCard,user_control,my_app/note_card.hpp")
#
# and includes "settings_form.hpp". Arguments:
#   <target>     the target that includes the header; it gains the output
#                directory as a private include directory and the header as a
#                source, so the header is generated before the target builds.
#   <document>   the .form file, relative to the current source directory.
#   CLASS        the generated class template's name.
#   NAMESPACE    its namespace (`a::b`); the global namespace when omitted.
#   OUTPUT       the header's path; default <binary dir>/gui_forms_forms/<stem>_form.hpp.
#   TYPES        application types the document may name, each
#                "<name>,<C++ class>,<registry base type>,<header>" (see the
#                compiler's --type).
# The header is regenerated when the document or the compiler changes; the
# compiler writes it only when its bytes change.

if(COMMAND gui_forms_add_form)
    return()
endif()

function(gui_forms_add_form target document)
    cmake_parse_arguments(PARSE_ARGV 2 FORM "" "CLASS;NAMESPACE;OUTPUT" "TYPES")
    if(FORM_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "gui_forms_add_form: unknown arguments ${FORM_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT FORM_CLASS)
        message(FATAL_ERROR "gui_forms_add_form: CLASS is required")
    endif()
    if(NOT TARGET GUIForms::form_compiler)
        message(FATAL_ERROR "gui_forms_add_form: GUIForms::form_compiler is not available "
                            "(build or install GUI.Forms with GUI_FORMS_BUILD_FORM_COMPILER)")
    endif()
    get_filename_component(source "${document}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(stem "${document}" NAME_WE)
    if(FORM_OUTPUT)
        get_filename_component(output "${FORM_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    else()
        set(output "${CMAKE_CURRENT_BINARY_DIR}/gui_forms_forms/${stem}_form.hpp")
    endif()
    get_filename_component(output_directory "${output}" DIRECTORY)
    set(arguments "${source}" --class "${FORM_CLASS}" --output "${output}")
    if(FORM_NAMESPACE)
        list(APPEND arguments --namespace "${FORM_NAMESPACE}")
    endif()
    foreach(type IN LISTS FORM_TYPES)
        list(APPEND arguments --type "${type}")
    endforeach()
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND GUIForms::form_compiler ${arguments}
        DEPENDS "${source}" "$<TARGET_FILE:GUIForms::form_compiler>"
        COMMENT "Generating ${stem}_form.hpp from ${document}"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${output_directory}")
endfunction()
