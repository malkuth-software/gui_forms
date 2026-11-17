# The SDK-consumer link test: a consumer of every public component, built
# against an installed SDK only. tools/sdk_consumer_link.py installs the build
# tree into a temporary prefix and builds this (the gui_forms_sdk_consumer_link
# test); tools/verify_install.py --sdk-consumer builds it against a given
# prefix. It exists because the one-module SDK once shipped an empty
# GUIForms::TextMasks interface and an Application archive without the text
# masks (b68c80ecc, 2d4bb797b), which every in-tree test missed: here a
# symbol the installed archives lack fails the link.
#
#   GUI_FORMS_SDK_CONSUMER_FACE        a TrueType face for the application
#                                      font family (assets/fonts/Arimo-Regular.ttf)
#   GUI_FORMS_SDK_CONSUMER_TEXT_MASKS  ON (the default): require TextMasks (a
#                                      build with GUI_FORMS_BUILD_TEXT_MASKS, as
#                                      build_sdk.py --features all and CI's tree)
#                                      and link a program of TextMasks alone
#   GUI_FORMS_SDK_CONSUMER_C_ABI       ON (the default): require CAbi and link a
#                                      C consumer of gf_abi_get_table and
#                                      gd_get_api_v0
# A component the SDK lacks fails the configure (find_package REQUIRED); it
# is never skipped quietly. Turning one off is a statement about the prefix,
# said in a warning, and tools/sdk_consumer_link.py then reports the test as
# skipped, not passed, since it proved less than it should.
cmake_minimum_required(VERSION 3.25)
project(GUIFormsSdkConsumer LANGUAGES C CXX)
option(GUI_FORMS_SDK_CONSUMER_TEXT_MASKS "Require the TextMasks component" ON)
option(GUI_FORMS_SDK_CONSUMER_C_ABI "Require the CAbi component" ON)
set(_sdk_components Core Controls Drawing Threading Application)
if(GUI_FORMS_SDK_CONSUMER_TEXT_MASKS)
    list(APPEND _sdk_components TextMasks)
endif()
if(GUI_FORMS_SDK_CONSUMER_C_ABI)
    list(APPEND _sdk_components CAbi)
endif()
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS ${_sdk_components})
if(NOT GUI_FORMS_SDK_CONSUMER_TEXT_MASKS)
    message(WARNING "GUI_FORMS_SDK_CONSUMER_TEXT_MASKS is OFF: the text masks are not checked")
endif()
if(NOT GUI_FORMS_SDK_CONSUMER_C_ABI)
    message(WARNING "GUI_FORMS_SDK_CONSUMER_C_ABI is OFF: the C ABI library and the drawing ABI are not checked")
endif()
if(NOT EXISTS "${GUI_FORMS_SDK_CONSUMER_FACE}")
    message(FATAL_ERROR "Set GUI_FORMS_SDK_CONSUMER_FACE to a TrueType face (assets/fonts/Arimo-Regular.ttf)")
endif()

# The C++ program: one module, every GUI.Forms archive linked statically.
add_executable(sdk_consumer main.cpp)
target_compile_features(sdk_consumer PRIVATE cxx_std_20)
target_link_libraries(sdk_consumer PRIVATE
    GUIForms::Core GUIForms::Controls GUIForms::Drawing GUIForms::Threading GUIForms::Application)

enable_testing()
add_test(NAME sdk_consumer COMMAND sdk_consumer "${GUI_FORMS_SDK_CONSUMER_FACE}")
set_tests_properties(sdk_consumer PROPERTIES PASS_REGULAR_EXPRESSION "sdk_consumer: every component linked and ran")

# The text masks through GUIForms::TextMasks alone: no Application named here,
# so an empty TextMasks interface fails the link as surely as an Application
# archive without the masks.
if(GUI_FORMS_SDK_CONSUMER_TEXT_MASKS)
    add_executable(sdk_text_masks_consumer text_masks_consumer.cpp)
    target_compile_features(sdk_text_masks_consumer PRIVATE cxx_std_20)
    target_link_libraries(sdk_text_masks_consumer PRIVATE GUIForms::TextMasks)
    add_test(NAME sdk_text_masks_consumer COMMAND sdk_text_masks_consumer)
    set_tests_properties(sdk_text_masks_consumer PROPERTIES
        PASS_REGULAR_EXPRESSION "sdk_text_masks_consumer: TextMasks alone links and runs")
endif()

# The C program: the C ABI library alone, with GUI.Drawing's raster seam.
if(GUI_FORMS_SDK_CONSUMER_C_ABI)
    add_executable(sdk_c_abi_consumer c_abi_consumer.c)
    set_target_properties(sdk_c_abi_consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_link_libraries(sdk_c_abi_consumer PRIVATE GUIForms::CAbi)
    add_test(NAME sdk_c_abi_consumer COMMAND sdk_c_abi_consumer)
    set_tests_properties(sdk_c_abi_consumer PROPERTIES PASS_REGULAR_EXPRESSION "sdk_c_abi_consumer: ok")
    # The C ABI library is shared: on Windows it is copied beside the program.
    if(WIN32)
        add_custom_command(TARGET sdk_c_abi_consumer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:GUIForms::CAbi> $<TARGET_FILE_DIR:sdk_c_abi_consumer>)
    endif()
endif()
