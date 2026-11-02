cmake_minimum_required(VERSION 3.25)
project(GUIFormsOwnedEventsReference LANGUAGES C CXX)

find_package(GUIForms 0.2 CONFIG REQUIRED COMPONENTS Controls)
add_executable(owned_events owned_events.cpp)
target_compile_features(owned_events PRIVATE cxx_std_20)
target_link_libraries(owned_events PRIVATE GUIForms::Controls)
# Keep the selected SDK ahead of an older GUI.Forms installation in the
# compiler's normal include search path.
set_target_properties(owned_events PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)

enable_testing()
add_test(NAME owned_events COMMAND owned_events)

foreach(example_name IN ITEMS bound_values shared_state collection_state cooperative_worker multiline_scrollbars)
    add_executable(${example_name} ${example_name}.cpp)
    target_compile_features(${example_name} PRIVATE cxx_std_20)
    if(example_name STREQUAL "cooperative_worker")
        target_link_libraries(${example_name} PRIVATE GUIForms::Threading)
    else()
        target_link_libraries(${example_name} PRIVATE GUIForms::Controls)
    endif()
    set_target_properties(${example_name} PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
    add_test(NAME ${example_name} COMMAND ${example_name})
    set_tests_properties(${example_name} PROPERTIES TIMEOUT 15)
endforeach()

# The C ABI from C11: the CAbi component alone, no C++ target.
find_package(GUIForms 0.2 CONFIG REQUIRED COMPONENTS CAbi)
add_executable(c_abi_consumer c_abi_consumer.c)
set_target_properties(c_abi_consumer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON NO_SYSTEM_FROM_IMPORTED TRUE)
target_link_libraries(c_abi_consumer PRIVATE GUIForms::CAbi)
add_test(NAME c_abi_consumer COMMAND c_abi_consumer)
set_tests_properties(c_abi_consumer PROPERTIES TIMEOUT 15 PASS_REGULAR_EXPRESSION "C ABI 1[.]")

if(TARGET GUIForms::Application)
    add_executable(collection_gallery collection_gallery.cpp)
    target_compile_features(collection_gallery PRIVATE cxx_std_20)
    target_link_libraries(collection_gallery PRIVATE GUIForms::Application)
    set_target_properties(collection_gallery PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
endif()

if(TARGET GUIForms::Audio)
    add_library(audio_adapter STATIC audio_worker.cpp)
    target_compile_features(audio_adapter PRIVATE cxx_std_20)
    target_link_libraries(audio_adapter PRIVATE GUIForms::Audio)
    set_target_properties(audio_adapter PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
    add_executable(ogg_worker ogg_worker.cpp)
    target_link_libraries(ogg_worker PRIVATE audio_adapter)
    add_test(NAME ogg_worker COMMAND ogg_worker "${CMAKE_CURRENT_SOURCE_DIR}/tone.ogg")
    set_tests_properties(ogg_worker PROPERTIES TIMEOUT 15)
    if(TARGET GUIForms::Application)
        add_executable(application_audio_worker ogg_worker.cpp)
        target_compile_definitions(application_audio_worker PRIVATE GUI_FORMS_REFERENCE_APPLICATION)
        target_compile_features(application_audio_worker PRIVATE cxx_std_20)
        target_link_libraries(application_audio_worker PRIVATE GUIForms::Application audio_adapter)
        set_target_properties(application_audio_worker PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
        add_test(NAME application_audio_worker
            COMMAND application_audio_worker "${CMAKE_CURRENT_SOURCE_DIR}/tone.ogg")
        set_tests_properties(application_audio_worker PROPERTIES TIMEOUT 15)
    endif()
endif()

if(TARGET GUIForms::Application)
    foreach(link_case IN ITEMS direct static_adapter interface_adapter)
        add_test(NAME reject_mixed_core_${link_case} COMMAND ${CMAKE_COMMAND}
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../linkage_rejection"
            "-DBINARY=${CMAKE_CURRENT_BINARY_DIR}/reject-${link_case}"
            "-DSDK=${CMAKE_PREFIX_PATH}"
            "-DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}"
            "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DLINK_CASE=${link_case}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/check_core_linkage.cmake")
    endforeach()
endif()

# Installed Windows SDKs keep DLLs in bin. Stage the actual transitive runtime
# targets alongside each executable; no Core archive is copied into consumers.
if(WIN32)
    get_property(reference_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(reference_target IN LISTS reference_targets)
        get_target_property(reference_type ${reference_target} TYPE)
        if(reference_type STREQUAL "EXECUTABLE")
            add_custom_command(TARGET ${reference_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${reference_target}>
                    $<TARGET_FILE_DIR:${reference_target}>
                COMMAND_EXPAND_LISTS)
        endif()
    endforeach()
endif()
