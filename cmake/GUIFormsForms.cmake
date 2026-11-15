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
#   USED_BY      the target's sources that include the header, directly or
#                through another header (relative to the current source
#                directory). Only their objects then depend on it, so a
#                changed document rebuilds those alone. Without USED_BY every
#                source of the target depends on it, which is always correct;
#                a source left out of USED_BY that does include the header
#                may not rebuild when the header changes.
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
# compiler changes; the compiler writes it only when its bytes change. The
# target builds after the header's generation (a per-target dependency), and
# each of its own sources' objects depends on the header (OBJECT_DEPENDS,
# so a depfile spelled another way, as ccache with CCACHE_BASEDIR writes it
# under a symlinked build directory, cannot hide a change). A source that a
# target in the compiler's own chain (the compiler or what it links) also
# compiles in the same directory is left without the property, which would
# otherwise make that target wait for the header it generates; likewise for
# gui_forms_add_control.

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

# The targets the generation of a header waits for: `generator` (the form
# compiler) and everything it links or depends on, transitively, as target
# names in the list named by `chain_variable`. Aliases are resolved, and a
# link entry wrapped in $<LINK_ONLY:...> or $<BUILD_INTERFACE:...> is
# unwrapped; other generator expressions and names that are not targets
# are left out. The walk keeps a visited list, so it ends.
function(gui_forms_generator_chain generator chain_variable)
    set(pending "${generator}")
    set(chain "")
    list(LENGTH pending remaining)
    while(remaining GREATER 0)
        list(POP_FRONT pending candidate)
        list(LENGTH pending remaining)
        if(candidate MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE):([^$<>]+)>$")
            set(candidate "${CMAKE_MATCH_2}")
        endif()
        if(candidate MATCHES "\\$<" OR NOT TARGET "${candidate}")
            continue()
        endif()
        get_target_property(aliased "${candidate}" ALIASED_TARGET)
        if(aliased)
            set(candidate "${aliased}")
        endif()
        list(FIND chain "${candidate}" seen)
        if(NOT seen EQUAL -1)
            continue()
        endif()
        list(APPEND chain "${candidate}")
        foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES MANUALLY_ADDED_DEPENDENCIES)
            get_target_property(next "${candidate}" ${property})
            if(next)
                list(APPEND pending ${next})
            endif()
            list(LENGTH pending remaining)
        endforeach()
    endwhile()
    set(${chain_variable} "${chain}" PARENT_SCOPE)
endfunction()

# Makes the objects of `target`'s own sources depend on its generated
# `header` (cpfix-c7). The compiler's dependency file alone is not enough:
# under ccache with CCACHE_BASEDIR set, its paths are rewritten relative to
# the compiler's real working directory, and with the build directory
# behind a symlink that spelling (a long "../../.../gui_forms_forms/x.hpp")
# is another Ninja node than the custom command's output, so a regenerated
# header dirtied nothing. OBJECT_DEPENDS names the header as the custom
# command spells it, so the dependency holds whatever the depfile says.
# OBJECT_DEPENDS is a source property of a directory and reaches every
# target there that compiles the source. That is harmless for a source
# shared with a target downstream of the header (it rebuilds when the
# header changes, as it would anyway once it includes it), but a target in
# `generator`'s chain (the form compiler or anything it links) would then
# wait for the header it produces: the cp33 cycle came from form_catalog.cpp,
# shared with the form compiler. Only such a source is left out; it keeps
# the per-target ordering below. If `target` itself is in the chain, the
# generation already waits for it and nothing is added. Other directories
# keep their own source properties. It runs deferred, at the end of the
# target's directory, so it sees every target and every source added after
# gui_forms_add_form or gui_forms_add_control; sources named by a generator
# expression and sources that are not compiled are left alone. `used_by`
# (USED_BY, absolute paths) narrows the sources to those that include the
# header; empty, every own source of the target gets the dependency.
function(gui_forms_generated_header_dependents target header generator used_by)
    get_target_property(source_directory ${target} SOURCE_DIR)
    if(used_by)
        set(sources "${used_by}")
    else()
        get_target_property(sources ${target} SOURCES)
    endif()
    if(NOT sources)
        return()
    endif()
    gui_forms_generator_chain("${generator}" chain)
    list(FIND chain "${target}" target_in_chain)
    if(NOT target_in_chain EQUAL -1)
        return()
    endif()
    # Every source compiled by a target of the directory in the chain, as
    # absolute paths.
    get_property(targets DIRECTORY "${source_directory}" PROPERTY BUILDSYSTEM_TARGETS)
    set(upstream "")
    foreach(other IN LISTS targets)
        list(FIND chain "${other}" other_in_chain)
        if(other STREQUAL target OR other_in_chain EQUAL -1)
            continue()
        endif()
        get_target_property(other_sources ${other} SOURCES)
        if(NOT other_sources)
            continue()
        endif()
        get_target_property(other_directory ${other} SOURCE_DIR)
        foreach(other_source IN LISTS other_sources)
            if(other_source MATCHES "\\$<")
                continue()
            endif()
            get_filename_component(other_path "${other_source}" ABSOLUTE BASE_DIR "${other_directory}")
            list(APPEND upstream "${other_path}")
        endforeach()
    endforeach()
    foreach(source IN LISTS sources)
        if(source MATCHES "\\$<")
            continue()
        endif()
        if(NOT source MATCHES "\\.(c|cc|cpp|cxx|m|mm)$")
            continue()
        endif()
        get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${source_directory}")
        list(FIND upstream "${path}" upstream_index)
        if(NOT upstream_index EQUAL -1)
            continue()
        endif()
        set_property(SOURCE "${path}" TARGET_DIRECTORY ${target} APPEND PROPERTY OBJECT_DEPENDS "${header}")
    endforeach()
endfunction()

# Defers gui_forms_generated_header_dependents to the end of the current
# directory. The deferred call's arguments would be read at the end of the
# directory, where this function's variables no longer exist, so they are
# written into the call as bracket arguments now.
function(gui_forms_defer_generated_header_dependents target header generator used_by)
    cmake_language(EVAL CODE
        "cmake_language(DEFER CALL gui_forms_generated_header_dependents [==[${target}]==] [==[${header}]==] [==[${generator}]==] [==[${used_by}]==])")
endfunction()

# The USED_BY sources in the list named by `sources_variable`, made absolute
# against the current source directory, into the list named by
# `paths_variable` (empty when USED_BY was not given).
function(gui_forms_used_by_paths sources_variable paths_variable)
    set(paths "")
    foreach(source IN LISTS ${sources_variable})
        get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND paths "${path}")
    endforeach()
    set(${paths_variable} "${paths}" PARENT_SCOPE)
endfunction()

# Orders `target` after the generation of its `header` by `generator`
# (cp33), and makes its own objects depend on it (cpfix-c7, above). The header is a source
# of the target, and a per-target generation target, added as a
# dependency, has it generated before any of the target's objects compile:
# an object that includes it only through another generated header, or
# whose dependency file predates it, still builds against the current
# bytes. The generation target's name joins the target to a digest of the
# header's path, so two headers with one stem stay apart.
function(gui_forms_order_generated_header target header generator used_by)
    string(MD5 digest "${header}")
    string(SUBSTRING "${digest}" 0 12 digest)
    string(MAKE_C_IDENTIFIER "gui_forms_generate_${target}_${digest}" generation)
    add_custom_target(${generation} DEPENDS "${header}")
    add_dependencies(${target} ${generation})
    target_sources(${target} PRIVATE "${header}")
    gui_forms_defer_generated_header_dependents(${target} "${header}" "${generator}" "${used_by}")
endfunction()

function(gui_forms_add_form target document)
    cmake_parse_arguments(PARSE_ARGV 2 FORM "DERIVED" "CLASS;NAMESPACE;OUTPUT;MODEL;MODEL_HEADER;COMPILER"
                          "TYPES;COMPOSITES;USED_BY")
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
    gui_forms_used_by_paths(FORM_USED_BY used_by)
    gui_forms_order_generated_header(${target} "${output}" "${compiler}" "${used_by}")
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
# COMPOSITES (composite controls the children may name), USED_BY (the
# sources that include the header, also through a generated form header
# naming the control) and COMPILER.

if(COMMAND gui_forms_add_control)
    return()
endif()

function(gui_forms_add_control target document)
    cmake_parse_arguments(PARSE_ARGV 2 CONTROL "" "CLASS;NAMESPACE;OUTPUT;COMPILER" "TYPES;COMPOSITES;USED_BY")
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
    gui_forms_used_by_paths(CONTROL_USED_BY used_by)
    gui_forms_order_generated_header(${target} "${output}" "${compiler}" "${used_by}")
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
