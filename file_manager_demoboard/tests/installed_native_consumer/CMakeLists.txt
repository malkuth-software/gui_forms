cmake_minimum_required(VERSION 3.25)
project(GUIFormsInstalledNativeConsumer LANGUAGES CXX)

find_package(GUIForms 0.6 REQUIRED CONFIG)

if(NOT TARGET GUIForms::Application)
    message(FATAL_ERROR "Installed GUI.Forms package has no native Application target")
endif()
# The faces are compiled into the library (font pack stage 4); the package
# carries their licences.
if(NOT EXISTS "${GUIForms_FONT_DIR}/LICENSE-PortsmouthRapids.txt")
    message(FATAL_ERROR "Installed GUI.Forms package has no font licences")
endif()

add_executable(gui_forms_installed_native_consumer main.cpp)
target_link_libraries(gui_forms_installed_native_consumer
    PRIVATE GUIForms::Application)
target_compile_features(gui_forms_installed_native_consumer PRIVATE cxx_std_20)
