cmake_minimum_required(VERSION 3.25)
project(GUIFormsInstalledNativeConsumer LANGUAGES CXX)

find_package(GUIForms 0.6 REQUIRED CONFIG)

if(NOT TARGET GUIForms::Application)
    message(FATAL_ERROR "Installed GUI.Forms package has no native Application target")
endif()
if(NOT EXISTS "${GUIForms_FONT_DIR}/PortsmouthRapids.ttf")
    message(FATAL_ERROR "Installed GUI.Forms package has no runtime font payload")
endif()

add_executable(gui_forms_installed_native_consumer main.cpp)
target_link_libraries(gui_forms_installed_native_consumer
    PRIVATE GUIForms::Application)
target_compile_features(gui_forms_installed_native_consumer PRIVATE cxx_std_20)
