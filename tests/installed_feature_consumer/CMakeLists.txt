# An installed-package consumer of the components tools/build_sdk.py
# --features all adds: tools/verify_install.py --feature-components builds and
# runs it against a prefix. It compiles application_text_consumer's main.cpp
# (GUI_FORMS_CONSUMER_MAIN), which uses the text masks and the loop transport
# beside Application, and text_stack.cpp, which includes the exported FreeType
# and HarfBuzz headers and shapes text with their static libraries.
cmake_minimum_required(VERSION 3.25)
project(GUIFormsInstalledFeatureConsumer LANGUAGES CXX)
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS
    Application TextMasks Audio AudioLoopTransport FreeType HarfBuzz)
get_target_property(text_core GUIForms::TextMasks INTERFACE_GUI_FORMS_CORE_LINKAGE)
if(NOT text_core STREQUAL "application")
    message(FATAL_ERROR "The installed TextMasks must advertise Application's Core, not '${text_core}'")
endif()
if(NOT EXISTS "${GUI_FORMS_CONSUMER_MAIN}")
    message(FATAL_ERROR "Set GUI_FORMS_CONSUMER_MAIN to tests/application_text_consumer/main.cpp")
endif()
add_executable(installed_feature_consumer "${GUI_FORMS_CONSUMER_MAIN}")
target_compile_features(installed_feature_consumer PRIVATE cxx_std_20)
target_link_libraries(installed_feature_consumer PRIVATE
    GUIForms::Application GUIForms::TextMasks GUIForms::AudioLoopTransport)
if(WIN32)
    add_custom_command(TARGET installed_feature_consumer POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_FILE:GUIForms::Threading>"
            "$<TARGET_FILE_DIR:installed_feature_consumer>"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${GUIForms_FONT_DIR}" "$<TARGET_FILE_DIR:installed_feature_consumer>/fonts")
endif()
# Static libraries: nothing to copy beside the executable.
add_executable(installed_text_stack_consumer text_stack.cpp)
target_compile_features(installed_text_stack_consumer PRIVATE cxx_std_20)
target_link_libraries(installed_text_stack_consumer PRIVATE GUIForms::HarfBuzz GUIForms::FreeType)
enable_testing()
add_test(NAME installed_feature_consumer COMMAND installed_feature_consumer)
add_test(NAME installed_text_stack_consumer
    COMMAND installed_text_stack_consumer "${GUIForms_FONT_DIR}/Arimo-Regular.ttf")
