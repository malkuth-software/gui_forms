# An installed-package consumer of the components tools/build_sdk.py
# --features all adds: tools/verify_install.py --feature-components builds and
# runs it against a prefix. It compiles application_text_consumer's main.cpp
# (GUI_FORMS_CONSUMER_MAIN), which uses the text masks and the loop transport
# beside Application, and text_stack.cpp, which includes the exported FreeType
# and HarfBuzz headers and shapes text with their static libraries. The
# package installs no face file (font pack stage 4), so the text stack test
# shapes a face from the source tree, GUI_FORMS_CONSUMER_FACE (an outside
# library given a file path, as a third-party consumer would be).
cmake_minimum_required(VERSION 3.25)
project(GUIFormsInstalledFeatureConsumer LANGUAGES CXX)
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS
    Application TextMasks Audio AudioLoopTransport FreeType HarfBuzz)
if(NOT EXISTS "${GUI_FORMS_CONSUMER_MAIN}")
    message(FATAL_ERROR "Set GUI_FORMS_CONSUMER_MAIN to tests/application_text_consumer/main.cpp")
endif()
if(NOT EXISTS "${GUI_FORMS_CONSUMER_FACE}")
    message(FATAL_ERROR "Set GUI_FORMS_CONSUMER_FACE to assets/fonts/Arimo-Regular.ttf")
endif()
add_executable(installed_feature_consumer "${GUI_FORMS_CONSUMER_MAIN}")
target_compile_features(installed_feature_consumer PRIVATE cxx_std_20)
target_link_libraries(installed_feature_consumer PRIVATE
    GUIForms::Application GUIForms::TextMasks GUIForms::AudioLoopTransport)
# One module: Application, Threading and STXlib's state are static archives
# linked into it, so nothing is copied beside it.
# Static libraries: nothing to copy beside the executable.
add_executable(installed_text_stack_consumer text_stack.cpp)
target_compile_features(installed_text_stack_consumer PRIVATE cxx_std_20)
target_link_libraries(installed_text_stack_consumer PRIVATE GUIForms::HarfBuzz GUIForms::FreeType)
enable_testing()
add_test(NAME installed_feature_consumer COMMAND installed_feature_consumer)
add_test(NAME installed_text_stack_consumer
    COMMAND installed_text_stack_consumer "${GUI_FORMS_CONSUMER_FACE}")
