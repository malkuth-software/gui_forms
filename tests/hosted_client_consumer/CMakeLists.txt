# An installed-package consumer of hosted content (docs/user/content-surfaces.md):
# a parent that starts a hosted program on a ContentSurface through
# GUIForms::Controls, and the program itself, which links GUIForms::HostedClient
# alone. tools/verify_install.py --hosted-client builds and runs it against a
# prefix. macOS and Linux: Windows hosted content is not written yet, and the
# HostedClient component is not installed there.
cmake_minimum_required(VERSION 3.25)
project(GUIFormsHostedClientConsumer LANGUAGES CXX)
find_package(GUIForms 0.6 CONFIG REQUIRED COMPONENTS Controls HostedClient)
add_executable(hosted_client_child child.cpp)
target_compile_features(hosted_client_child PRIVATE cxx_std_20)
target_link_libraries(hosted_client_child PRIVATE GUIForms::HostedClient)
add_executable(hosted_client_parent parent.cpp)
target_compile_features(hosted_client_parent PRIVATE cxx_std_20)
target_link_libraries(hosted_client_parent PRIVATE GUIForms::Controls)
target_compile_definitions(hosted_client_parent PRIVATE
    "GUI_FORMS_CONSUMER_HOSTED_CHILD=\"$<TARGET_FILE:hosted_client_child>\"")
add_dependencies(hosted_client_parent hosted_client_child)
enable_testing()
add_test(NAME hosted_client_consumer COMMAND hosted_client_parent)
set_tests_properties(hosted_client_consumer PROPERTIES TIMEOUT 60)
