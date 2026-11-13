# An installed-package consumer that links its own, upstream HarfBuzz and
# FreeType beside GUIForms::Application (owner decisions 2026-11-13, "What
# this asks of GUI.Forms"): tools/verify_install.py --upstream-text-stack
# builds and runs it against a prefix. main.cpp shapes text with the upstream
# libraries and renders a label through GUI.Forms' own, privately linked
# copies, and requires the upstream calls to reach the upstream libraries.
#
# Needs pkg-config and the harfbuzz and freetype2 modules (Homebrew's
# harfbuzz and freetype on macOS; libharfbuzz-dev and libfreetype-dev on
# Linux). The versions may be any; the check is conclusive when they differ
# from the pinned ones, and main.cpp prints both.
cmake_minimum_required(VERSION 3.25)
project(GUIFormsUpstreamTextStackConsumer LANGUAGES CXX)
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Application)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UPSTREAM_TEXT REQUIRED IMPORTED_TARGET harfbuzz freetype2)
add_executable(upstream_text_stack_consumer main.cpp)
target_compile_features(upstream_text_stack_consumer PRIVATE cxx_std_20)
# The upstream libraries come first on the link line: were a GUI.Forms
# library to export one of their symbols, a flat lookup could still bind the
# consumer to it, which the version check below then reports.
target_link_libraries(upstream_text_stack_consumer PRIVATE PkgConfig::UPSTREAM_TEXT GUIForms::Application)
target_compile_definitions(upstream_text_stack_consumer PRIVATE
    "GUI_FORMS_CONSUMER_FONT_DIR=\"${GUIForms_FONT_DIR}\"")
if(WIN32)
    add_custom_command(TARGET upstream_text_stack_consumer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>"
            "$<TARGET_FILE_DIR:upstream_text_stack_consumer>")
endif()
enable_testing()
add_test(NAME upstream_text_stack_consumer COMMAND upstream_text_stack_consumer)
