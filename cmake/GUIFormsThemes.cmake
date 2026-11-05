# A program's theme package in a CMake build (owner rulings of 2026-11-04): a
# program, never a form, invokes its project theme. The package directory
# (theme.json and its sheet) is embedded into a generated header at build time,
# and the sheet is checked against the program's form documents. Included by
# the GUI.Forms build and by an installed SDK's GUIFormsConfig.cmake:
#
#   gui_forms_add_theme(my_app themes/my_app FUNCTION my_app_theme NAMESPACE my_app
#                       FORMS forms/main.form forms/settings.form)
#
# and includes "<function>.hpp", which declares
#   [[nodiscard]] gui_forms::ThemePackage <function>();
# returning the package's manifest and files as bytes, for
# ApplicationResources::compile_theme_package. Arguments:
#   <target>     the target that includes the header (private include
#                directory and source, as gui_forms_add_form).
#   <directory>  the package directory, relative to the current source
#                directory: theme.json and the sheet it names.
#   FUNCTION     the generated function's name (one C++ identifier).
#   NAMESPACE    its namespace (`a::b`); the global namespace when omitted.
#   FORMS        the program's form documents. Every stable-ID selector of
#                the sheet (`#gallery\.typography\.heading`) must name an
#                object of one of them, or the build fails: a rule that names
#                no object styles nothing.
# The build also fails when the manifest's sha256 is not the sheet's.

if(COMMAND gui_forms_add_theme)
    return()
endif()

set(GUI_FORMS_THEME_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/GUIFormsThemeEmbed.cmake")

function(gui_forms_add_theme target directory)
    cmake_parse_arguments(PARSE_ARGV 2 THEME "" "FUNCTION;NAMESPACE" "FORMS")
    if(THEME_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "gui_forms_add_theme: unknown arguments ${THEME_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT THEME_FUNCTION MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "gui_forms_add_theme: FUNCTION must be one C++ identifier")
    endif()
    get_filename_component(package "${directory}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    file(GLOB sheets CONFIGURE_DEPENDS "${package}/*.css")
    set(forms "")
    foreach(form IN LISTS THEME_FORMS)
        get_filename_component(absolute "${form}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND forms "${absolute}")
    endforeach()
    set(output_directory "${CMAKE_CURRENT_BINARY_DIR}/gui_forms_themes")
    set(output "${output_directory}/${THEME_FUNCTION}.hpp")
    # Lists cross the command line joined by '|', which no path here holds.
    string(REPLACE ";" "|" forms_argument "${forms}")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DPACKAGE=${package}" "-DOUTPUT=${output}"
                "-DFUNCTION=${THEME_FUNCTION}" "-DNAMESPACE=${THEME_NAMESPACE}" "-DFORMS=${forms_argument}"
                -P "${GUI_FORMS_THEME_EMBED_SCRIPT}"
        DEPENDS "${package}/theme.json" ${sheets} ${forms} "${GUI_FORMS_THEME_EMBED_SCRIPT}"
        COMMENT "Embedding the theme package ${directory}"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${output_directory}")
endfunction()
