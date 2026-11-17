# An installed-package consumer of the components tools/build_sdk.py
# --features all adds: tools/verify_install.py --feature-components builds and
# runs it against a prefix. It compiles application_text_consumer's main.cpp
# (GUI_FORMS_CONSUMER_MAIN), which uses the text masks and the loop transport
# beside Application. (The exported FreeType and HarfBuzz, and the
# text_stack.cpp consumer of them, were retired on 2026-11-16 with the text
# engine's move to STXlib.)
cmake_minimum_required(VERSION 3.25)
project(GUIFormsInstalledFeatureConsumer LANGUAGES CXX)
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS
    Application TextMasks Audio AudioLoopTransport)
if(NOT EXISTS "${GUI_FORMS_CONSUMER_MAIN}")
    message(FATAL_ERROR "Set GUI_FORMS_CONSUMER_MAIN to tests/application_text_consumer/main.cpp")
endif()
add_executable(installed_feature_consumer "${GUI_FORMS_CONSUMER_MAIN}")
target_compile_features(installed_feature_consumer PRIVATE cxx_std_20)
target_link_libraries(installed_feature_consumer PRIVATE
    GUIForms::Application GUIForms::TextMasks GUIForms::AudioLoopTransport)
# One module: Application, Threading and STXlib's state are static archives
# linked into it, so nothing is copied beside it.
# Static libraries: nothing to copy beside the executable.
enable_testing()
add_test(NAME installed_feature_consumer COMMAND installed_feature_consumer)
