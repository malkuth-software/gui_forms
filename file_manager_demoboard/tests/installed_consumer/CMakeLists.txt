cmake_minimum_required(VERSION 3.25)
project(GUIFormsInstalledConsumer LANGUAGES CXX)

find_package(GUIForms 0.2 REQUIRED CONFIG)

add_executable(gui_forms_installed_consumer main.cpp)
# CorrespondenceView and InstrumentRack are the lab library's (audit F9): the
# SDK must be installed with GUI_FORMS_INSTALL_LAB=ON.
target_link_libraries(gui_forms_installed_consumer PRIVATE GUIForms::Controls GUIForms::Lab)
target_compile_features(gui_forms_installed_consumer PRIVATE cxx_std_20)
