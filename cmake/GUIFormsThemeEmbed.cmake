# Script mode (cmake -P) for gui_forms_add_theme (GUIFormsThemes.cmake):
# checks a theme package against the program's form documents and writes
# the header that embeds it. Inputs, as -D definitions:
#   PACKAGE    the package directory (theme.json and the files it names);
#   OUTPUT     the header to write (rewritten only when its bytes change);
#   FUNCTION   the generated function's name;
#   NAMESPACE  its namespace, or empty;
#   FORMS      the form documents, joined by '|'.
# Fails, naming the cause, when the manifest names a file that is missing or
# whose sha256 differs, when a stable-ID selector names no object of the
# forms, or when a file holds the raw string delimiter.

cmake_minimum_required(VERSION 3.25)

set(delimiter "gf_theme")
# The text is split and searched as CMake lists below; ';' is a list
# separator there, so it stands as this character while parsing.
string(ASCII 1 semicolon_stand_in)

# `text` without its /* ... */ comments, into `result`. CMake's regular
# expressions have no lazy repetition, so the comments are cut in a loop.
function(strip_comments text result)
    set(rest "${text}")
    set(kept "")
    while(TRUE)
        string(FIND "${rest}" "/*" opening)
        if(opening EQUAL -1)
            string(APPEND kept "${rest}")
            break()
        endif()
        string(SUBSTRING "${rest}" 0 ${opening} before)
        string(APPEND kept "${before} ")
        math(EXPR after_opening "${opening} + 2")
        string(SUBSTRING "${rest}" ${after_opening} -1 rest)
        string(FIND "${rest}" "*/" closing)
        if(closing EQUAL -1)
            break()
        endif()
        math(EXPR after_closing "${closing} + 2")
        string(SUBSTRING "${rest}" ${after_closing} -1 rest)
    endwhile()
    set(${result} "${kept}" PARENT_SCOPE)
endfunction()

# The C++ expression of `text` as concatenated raw string literals of at
# most 4000 bytes each, into `result`.
function(raw_literal text name result)
    string(FIND "${text}" ")${delimiter}\"" clash)
    if(NOT clash EQUAL -1)
        message(FATAL_ERROR "gui_forms_add_theme: ${name} holds the raw string delimiter )${delimiter}\"")
    endif()
    # A carriage return in a raw string literal may become a newline in
    # translation, and the bytes would no longer have the manifest's sha256.
    string(FIND "${text}" "" carriage_return)
    if(NOT carriage_return EQUAL -1)
        message(FATAL_ERROR "gui_forms_add_theme: ${name} must have LF line endings")
    endif()
    string(LENGTH "${text}" length)
    if(length EQUAL 0)
        set(${result} "std::string()" PARENT_SCOPE)
        return()
    endif()
    set(literal "std::string(")
    set(offset 0)
    while(offset LESS length)
        string(SUBSTRING "${text}" ${offset} 4000 piece)
        string(APPEND literal "R\"${delimiter}(${piece})${delimiter}\"\n        ")
        math(EXPR offset "${offset} + 4000")
    endwhile()
    string(APPEND literal ")")
    set(${result} "${literal}" PARENT_SCOPE)
endfunction()

# -- the manifest and the files it names ------------------------------------
file(READ "${PACKAGE}/theme.json" manifest)
string(REGEX MATCHALL "\"path\"[ \t\r\n]*:[ \t\r\n]*\"[^\"]+\"" path_fields "${manifest}")
string(REGEX MATCHALL "\"sha256\"[ \t\r\n]*:[ \t\r\n]*\"[0-9A-Fa-f]+\"" sha_fields "${manifest}")
list(LENGTH path_fields path_count)
list(LENGTH sha_fields sha_count)
if(path_count EQUAL 0 OR NOT path_count EQUAL sha_count)
    message(FATAL_ERROR "gui_forms_add_theme: ${PACKAGE}/theme.json must name each file with a path and a sha256")
endif()
set(paths "")
math(EXPR last "${path_count} - 1")
foreach(index RANGE ${last})
    list(GET path_fields ${index} path_field)
    list(GET sha_fields ${index} sha_field)
    string(REGEX REPLACE "^\"path\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"$" "\\1" path "${path_field}")
    string(REGEX REPLACE "^\"sha256\"[ \t\r\n]*:[ \t\r\n]*\"([0-9A-Fa-f]+)\"$" "\\1" expected "${sha_field}")
    if(NOT EXISTS "${PACKAGE}/${path}")
        message(FATAL_ERROR "gui_forms_add_theme: ${PACKAGE}/theme.json names ${path}, which is missing")
    endif()
    file(SHA256 "${PACKAGE}/${path}" actual)
    string(TOLOWER "${expected}" expected)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "gui_forms_add_theme: the sha256 of ${path} is ${actual}; theme.json says ${expected}")
    endif()
    list(APPEND paths "${path}")
endforeach()

# -- the forms' stable IDs ------------------------------------------------------
set(known_ids "")
string(REPLACE "|" ";" form_files "${FORMS}")
foreach(form IN LISTS form_files)
    file(READ "${form}" form_text)
    string(REPLACE ";" "${semicolon_stand_in}" form_text "${form_text}")
    strip_comments("${form_text}" form_text)
    # An object opens with `<type> <stable-id> {`.
    string(REGEX MATCHALL "[a-z][a-z0-9-]*[ \t]+[A-Za-z0-9_.-]+[ \t\r\n]*{" openings "${form_text}")
    foreach(opening IN LISTS openings)
        string(REGEX REPLACE "^[a-z][a-z0-9-]*[ \t]+([A-Za-z0-9_.-]+)[ \t\r\n]*{$" "\\1" id "${opening}")
        list(APPEND known_ids "${id}")
    endforeach()
endforeach()

# -- the sheets' stable-ID selectors ---------------------------------------------
# A selector list is the text before each '{'; the text before each '}' is
# declarations, where '#' starts a colour.
foreach(path IN LISTS paths)
    if(NOT path MATCHES "\\.css$")
        continue()
    endif()
    file(READ "${PACKAGE}/${path}" sheet)
    string(REPLACE ";" "${semicolon_stand_in}" sheet "${sheet}")
    strip_comments("${sheet}" sheet)
    string(REGEX MATCHALL "[^{}]*{" preludes "${sheet}")
    foreach(prelude IN LISTS preludes)
        string(REGEX MATCHALL "#([A-Za-z_]|\\\\.)([A-Za-z0-9_-]|\\\\.)*" selectors "${prelude}")
        foreach(selector IN LISTS selectors)
            string(SUBSTRING "${selector}" 1 -1 escaped)
            string(REGEX REPLACE "\\\\(.)" "\\1" id "${escaped}")
            list(FIND known_ids "${id}" found)
            if(found EQUAL -1)
                message(FATAL_ERROR "gui_forms_add_theme: ${path} selects #${escaped}, which no form document of "
                                    "the program names")
            endif()
        endforeach()
    endforeach()
endforeach()

# -- the header -------------------------------------------------------------------
raw_literal("${manifest}" "theme.json" manifest_literal)
set(header "#pragma once\n\n")
string(APPEND header "// Generated by gui_forms_add_theme from ${PACKAGE}; do not edit.\n\n")
string(APPEND header "#include \"gui_forms/theme/theme_compiler/theme_compiler.hpp\"\n\n#include <string>\n\n")
if(NAMESPACE)
    string(APPEND header "namespace ${NAMESPACE} {\n\n")
endif()
string(APPEND header "// The theme package's manifest and files, as bytes.\n")
string(APPEND header "[[nodiscard]] inline gui_forms::ThemePackage ${FUNCTION}() {\n")
string(APPEND header "    gui_forms::ThemePackage package{};\n")
string(APPEND header "    package.manifest = ${manifest_literal};\n")
foreach(path IN LISTS paths)
    file(READ "${PACKAGE}/${path}" bytes)
    raw_literal("${bytes}" "${path}" bytes_literal)
    string(APPEND header "    package.files.push_back(gui_forms::ThemePackageFile{\"${path}\",\n        ${bytes_literal}});\n")
endforeach()
string(APPEND header "    return package;\n}\n")
if(NAMESPACE)
    string(APPEND header "\n} // namespace ${NAMESPACE}\n")
endif()
file(WRITE "${OUTPUT}.next" "${header}")
file(COPY_FILE "${OUTPUT}.next" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.next")
