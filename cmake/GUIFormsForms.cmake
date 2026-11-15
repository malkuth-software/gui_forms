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
#   CLASS        the generated class's name (a class template's without DERIVED).
#   NAMESPACE    its namespace (`a::b`); the global namespace when omitted.
#   OUTPUT       the header's path; default <binary dir>/gui_forms_forms/<stem>_form.hpp.
#   TYPES        application types the document may name, each
#                "<name>,<C++ class>,<registry base type>,<header>" (see the
#                compiler's --type).
#   COMPOSITES   composite controls the document may name (cp34), each
#                "<document.control>,<C++ class>,<header>": the document
#                relative to the current source directory, and the class and
#                header gui_forms_add_control generates from it (see the
#                compiler's --composite). A form names one by the document's
#                name and fills its slot with its children; one without a
#                slot refuses them (E809). Later entries may name earlier ones.
#   DERIVED      generate the class deriving from the root's type (stage B):
#                a gui_forms::Window subclass for a form root, a subclass of
#                the root control's type otherwise, instead of the document
#                class template over an owner. A root control whose type is
#                final (the layout panels: flex, grid, block, flow, table)
#                is composed instead: the class holds it as a member, and
#                root_control() is the control to show.
#   MODEL        the model a derived class binds (`a::Type`); implies DERIVED.
#                Required when the document binds.
#   MODEL_HEADER the header declaring MODEL, included by the generated one.
#   COMPILER     the form compiler target that generates the header; default
#                GUIForms::form_compiler. The GUI.Forms build passes
#                gui_forms_form_compiler_lab for the Gallery's pages, which
#                name the lab and demonstration controls (audit F9).
# The header is regenerated when the document, a composite's document or the
# compiler changes; the compiler writes it only when its bytes change.

if(COMMAND gui_forms_add_form)
    return()
endif()

# Appends --composite for each "<document>,<C++ class>,<header>" in the list
# named by `composites_variable` to the list named by `arguments_variable`,
# the document made absolute against the current source directory, and each
# document to the list named by `documents_variable`.
# The parameters hold the names of the caller's variables; they are named
# apart from those variables (arguments, documents), which a parameter of
# the same name would hide.
function(gui_forms_composite_arguments composites_variable arguments_variable documents_variable)
    set(appended ${${arguments_variable}})
    set(read ${${documents_variable}})
    foreach(composite IN LISTS ${composites_variable})
        if(NOT composite MATCHES "^(.+),([^,]+),([^,]+)$")
            message(FATAL_ERROR "gui_forms: COMPOSITES expects <document>,<C++ class>,<header>: ${composite}")
        endif()
        get_filename_component(composite_document "${CMAKE_MATCH_1}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND appended --composite "${composite_document},${CMAKE_MATCH_2},${CMAKE_MATCH_3}")
        list(APPEND read "${composite_document}")
    endforeach()
    set(${arguments_variable} ${appended} PARENT_SCOPE)
    set(${documents_variable} ${read} PARENT_SCOPE)
endfunction()

function(gui_forms_add_form target document)
    cmake_parse_arguments(PARSE_ARGV 2 FORM "DERIVED" "CLASS;NAMESPACE;OUTPUT;MODEL;MODEL_HEADER;COMPILER"
                          "TYPES;COMPOSITES")
    if(FORM_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "gui_forms_add_form: unknown arguments ${FORM_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT FORM_CLASS)
        message(FATAL_ERROR "gui_forms_add_form: CLASS is required")
    endif()
    if(FORM_COMPILER)
        set(compiler "${FORM_COMPILER}")
    else()
        set(compiler GUIForms::form_compiler)
    endif()
    if(NOT TARGET ${compiler})
        message(FATAL_ERROR "gui_forms_add_form: ${compiler} is not available "
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
    if(FORM_DERIVED)
        list(APPEND arguments --derived)
    endif()
    if(FORM_MODEL)
        list(APPEND arguments --model "${FORM_MODEL}")
    endif()
    if(FORM_MODEL_HEADER)
        list(APPEND arguments --model-header "${FORM_MODEL_HEADER}")
    endif()
    foreach(type IN LISTS FORM_TYPES)
        list(APPEND arguments --type "${type}")
    endforeach()
    set(documents "${source}")
    gui_forms_composite_arguments(FORM_COMPOSITES arguments documents)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND ${compiler} ${arguments}
        DEPENDS ${documents} "$<TARGET_FILE:${compiler}>"
        COMMENT "Generating ${stem}_form.hpp from ${document}"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${output_directory}")
endfunction()

# Composite controls (cp34): the form compiler's control mode generates a
# `.control` document's C++ header at build time, checking the document as
# the run-time loader does. A consumer writes:
#
#   gui_forms_add_control(my_app controls/toggle-pill.control CLASS TogglePill NAMESPACE my_app)
#
# and includes "toggle_pill_control.hpp", whose class derives from
# gui_forms::CompositeControl and is made with gui_forms::make_control.
# Arguments are gui_forms_add_form's, without DERIVED, MODEL and
# MODEL_HEADER: the target, the document, CLASS, NAMESPACE, OUTPUT (default
# <binary dir>/gui_forms_controls/<stem>_control.hpp, a '-' in the stem
# becoming '_'), TYPES (application types the children may name),
# COMPOSITES (composite controls the children may name) and COMPILER.

if(COMMAND gui_forms_add_control)
    return()
endif()

function(gui_forms_add_control target document)
    cmake_parse_arguments(PARSE_ARGV 2 CONTROL "" "CLASS;NAMESPACE;OUTPUT;COMPILER" "TYPES;COMPOSITES")
    if(CONTROL_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "gui_forms_add_control: unknown arguments ${CONTROL_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT CONTROL_CLASS)
        message(FATAL_ERROR "gui_forms_add_control: CLASS is required")
    endif()
    if(CONTROL_COMPILER)
        set(compiler "${CONTROL_COMPILER}")
    else()
        set(compiler GUIForms::form_compiler)
    endif()
    if(NOT TARGET ${compiler})
        message(FATAL_ERROR "gui_forms_add_control: ${compiler} is not available "
                            "(build or install GUI.Forms with GUI_FORMS_BUILD_FORM_COMPILER)")
    endif()
    get_filename_component(source "${document}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(stem "${document}" NAME_WE)
    string(REPLACE "-" "_" stem "${stem}")
    if(CONTROL_OUTPUT)
        get_filename_component(output "${CONTROL_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    else()
        set(output "${CMAKE_CURRENT_BINARY_DIR}/gui_forms_controls/${stem}_control.hpp")
    endif()
    get_filename_component(output_directory "${output}" DIRECTORY)
    set(arguments "${source}" --control --class "${CONTROL_CLASS}" --output "${output}")
    if(CONTROL_NAMESPACE)
        list(APPEND arguments --namespace "${CONTROL_NAMESPACE}")
    endif()
    foreach(type IN LISTS CONTROL_TYPES)
        list(APPEND arguments --type "${type}")
    endforeach()
    set(documents "${source}")
    gui_forms_composite_arguments(CONTROL_COMPOSITES arguments documents)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND ${compiler} ${arguments}
        DEPENDS ${documents} "$<TARGET_FILE:${compiler}>"
        COMMENT "Generating ${stem}_control.hpp from ${document}"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${output_directory}")
endfunction()

# The form schema as a designer reads it (cp34): the form compiler's
# --schema mode writes, at build time, every type a document compiled with
# the same TYPES and COMPOSITES may name, with each type's properties,
# interactions and where its children go, and each composite's slot, events
# and commands (form_schema_export.hpp in the GUI.Forms sources). A
# consumer writes:
#
#   gui_forms_add_form_schema(my_app OUTPUT my_app_form_schema.json
#       COMPOSITES "controls/toggle-pill.control,my_app::TogglePill,toggle_pill_control.hpp")
#
# OUTPUT is relative to the current binary directory (default
# gui_forms_forms/form_schema.json); the target gains the file as a source,
# so it is written before the target builds. TYPES, COMPOSITES and COMPILER
# are gui_forms_add_form's.

if(COMMAND gui_forms_add_form_schema)
    return()
endif()

function(gui_forms_add_form_schema target)
    cmake_parse_arguments(PARSE_ARGV 1 SCHEMA "" "OUTPUT;COMPILER" "TYPES;COMPOSITES")
    if(SCHEMA_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "gui_forms_add_form_schema: unknown arguments ${SCHEMA_UNPARSED_ARGUMENTS}")
    endif()
    if(SCHEMA_COMPILER)
        set(compiler "${SCHEMA_COMPILER}")
    else()
        set(compiler GUIForms::form_compiler)
    endif()
    if(NOT TARGET ${compiler})
        message(FATAL_ERROR "gui_forms_add_form_schema: ${compiler} is not available "
                            "(build or install GUI.Forms with GUI_FORMS_BUILD_FORM_COMPILER)")
    endif()
    if(SCHEMA_OUTPUT)
        get_filename_component(output "${SCHEMA_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    else()
        set(output "${CMAKE_CURRENT_BINARY_DIR}/gui_forms_forms/form_schema.json")
    endif()
    get_filename_component(output_directory "${output}" DIRECTORY)
    set(arguments --schema --output "${output}")
    foreach(type IN LISTS SCHEMA_TYPES)
        list(APPEND arguments --type "${type}")
    endforeach()
    set(documents "")
    gui_forms_composite_arguments(SCHEMA_COMPOSITES arguments documents)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND ${compiler} ${arguments}
        DEPENDS ${documents} "$<TARGET_FILE:${compiler}>"
        COMMENT "Generating the form schema ${output}"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
endfunction()
