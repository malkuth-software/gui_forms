# The shared-source project of tests/cmake/form_shared_source_test.cmake:
# gui_forms_add_form on a target that compiles a source shared with the
# stand-in form compiler and with a third target. A per-source property
# would make the compiler's own object depend on the header it generates (a
# ninja dependency cycle, cp33); the per-target ordering must not, and the
# header must still rebuild form_reader.cpp's object (its own source) when
# the depfile spells the header another way (ccache, cpfix-c7).
cmake_minimum_required(VERSION 3.25)
project(gui_forms_form_shared_source LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT GUI_FORMS_FORMS_MODULE)
    message(FATAL_ERROR "form shared source: GUI_FORMS_FORMS_MODULE is not set")
endif()
include("${GUI_FORMS_FORMS_MODULE}")

add_executable(fake_form_compiler fake_form_compiler.cpp shared.cpp)
add_executable(form_reader form_reader.cpp shared.cpp)
gui_forms_add_form(form_reader sample.form CLASS SampleForm COMPILER fake_form_compiler)
add_executable(other_reader other_reader.cpp shared.cpp)
