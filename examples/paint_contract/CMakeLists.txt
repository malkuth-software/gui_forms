cmake_minimum_required(VERSION 3.25)
project(GUIFormsPaintContract LANGUAGES CXX)
option(GUI_FORMS_EXAMPLE_NATIVE "Exercise the installed native application host" OFF)
if(GUI_FORMS_EXAMPLE_NATIVE)
    find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Application)
else()
    find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Controls Drawing)
endif()
# The bundled faces are compiled into the library (font pack stage 4): nothing
# is copied beside the program or into its bundle.
if(GUI_FORMS_EXAMPLE_NATIVE AND APPLE)
    add_executable(paint_contract MACOSX_BUNDLE main.cpp)
else()
    add_executable(paint_contract main.cpp)
endif()
target_compile_features(paint_contract PRIVATE cxx_std_20)
if(GUI_FORMS_EXAMPLE_NATIVE)
    target_compile_definitions(paint_contract PRIVATE GUI_FORMS_EXAMPLE_NATIVE=1)
    target_link_libraries(paint_contract PRIVATE GUIForms::Application)
else()
    target_link_libraries(paint_contract PRIVATE GUIForms::Controls GUIForms::Drawing)
endif()
# The program is one module: every GUI.Forms library and STXlib's state are
# static archives linked into it, so nothing is copied beside it.
enable_testing()
add_test(NAME paint_contract COMMAND paint_contract)
# The drawing ABI, where the SDK installed the C ABI library that holds it: a
# C program through the installed header and that one library alone.
if(TARGET GUIForms::CAbi)
    enable_language(C)
    add_executable(drawing_abi_consumer drawing_abi_consumer.c)
    set_target_properties(drawing_abi_consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_link_libraries(drawing_abi_consumer PRIVATE GUIForms::CAbi)
    if(WIN32)
        add_custom_command(TARGET drawing_abi_consumer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:drawing_abi_consumer> $<TARGET_FILE_DIR:drawing_abi_consumer>
            COMMAND_EXPAND_LISTS)
    endif()
    add_test(NAME drawing_abi_consumer COMMAND drawing_abi_consumer)
endif()
