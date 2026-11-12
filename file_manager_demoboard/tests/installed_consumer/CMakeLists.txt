cmake_minimum_required(VERSION 3.25)
project(GUIFormsInstalledConsumer LANGUAGES CXX)

find_package(GUIForms 0.2 REQUIRED CONFIG)
# CorrespondenceView and InstrumentRack are the lab library's (audit F9).
# run_installed_consumer.cmake installs the SDK with GUI_FORMS_INSTALL_LAB=ON.
if(NOT TARGET GUIForms::Lab)
    message(FATAL_ERROR "The installed GUI.Forms SDK has no GUIForms::Lab: install it with "
                        "GUI_FORMS_INSTALL_LAB=ON (run_installed_consumer.cmake does)")
endif()

add_executable(gui_forms_installed_consumer main.cpp)
target_link_libraries(gui_forms_installed_consumer PRIVATE GUIForms::Controls GUIForms::Lab)
target_compile_features(gui_forms_installed_consumer PRIVATE cxx_std_20)
