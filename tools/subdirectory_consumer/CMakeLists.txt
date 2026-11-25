# A consumer that adds GUI.Forms with add_subdirectory, as Plan Paint's
# development build does, and opts its program into the library allocator.
# tools/ci_build.py builds it on every desktop host and runs the program,
# the allocator probe (tools/allocator_probe/allocator_probe.cpp), which
# fails unless the replacement reports itself live. It proves that the
# consumer functions work from a directory that is not GUI.Forms' own: a
# function reading one of GUI.Forms' directory variables sees nothing there
# (gui_forms_use_library_allocator once compiled no source and left the
# program on the toolchain's operator new).
cmake_minimum_required(VERSION 3.25)
project(gui_forms_subdirectory_consumer LANGUAGES C CXX)

set(GUI_FORMS_SOURCE_DIR "" CACHE PATH "The GUI.Forms checkout to add")
if(NOT EXISTS "${GUI_FORMS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "GUI_FORMS_SOURCE_DIR must name a GUI.Forms checkout")
endif()
add_subdirectory("${GUI_FORMS_SOURCE_DIR}" gui_forms EXCLUDE_FROM_ALL)

add_executable(subdirectory_consumer_probe "${GUI_FORMS_SOURCE_DIR}/tools/allocator_probe/allocator_probe.cpp")
target_compile_definitions(subdirectory_consumer_probe PRIVATE GUI_FORMS_PROBE_OPTED_IN=1)
target_link_libraries(subdirectory_consumer_probe PRIVATE GUIForms::Memory stxlib::boundary stxlib::state)
gui_forms_use_library_allocator(subdirectory_consumer_probe)
