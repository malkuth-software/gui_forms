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
if(WIN32)
    # Every installed DLL the program loads (Threading and STXlib's shared
    # state, stxlib_state.dll, always; Application when native) beside it, so
    # it runs without the SDK's bin/ on PATH.
    add_custom_command(TARGET paint_contract POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:paint_contract> $<TARGET_FILE_DIR:paint_contract>
        COMMAND_EXPAND_LISTS)
endif()
enable_testing()
add_test(NAME paint_contract COMMAND paint_contract)
# The drawing ABI, where the SDK installed it: a C program through the
# installed header and shared library alone.
if(GUIForms_DrawingCApi_FOUND)
    enable_language(C)
    add_executable(drawing_abi_consumer drawing_abi_consumer.c)
    set_target_properties(drawing_abi_consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_link_libraries(drawing_abi_consumer PRIVATE GUIForms::DrawingCApi)
    if(WIN32)
        add_custom_command(TARGET drawing_abi_consumer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:drawing_abi_consumer> $<TARGET_FILE_DIR:drawing_abi_consumer>
            COMMAND_EXPAND_LISTS)
    endif()
    add_test(NAME drawing_abi_consumer COMMAND drawing_abi_consumer)
endif()
