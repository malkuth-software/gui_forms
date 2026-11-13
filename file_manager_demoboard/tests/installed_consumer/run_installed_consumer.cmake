# The demoboard's installed-consumer proof (audit F9): configures, builds and
# installs the GUI.Forms SDK with GUI_FORMS_INSTALL_LAB=ON into a clean
# build-local prefix, then configures, builds and runs this consumer against
# only find_package(GUIForms CONFIG), GUIForms::Controls and GUIForms::Lab.
#
#   cmake -DGUI_FORMS_SOURCE_DIR=<gui_forms checkout> -DWORK_DIR=<empty directory>
#         [-DSDK_ARGUMENTS=<extra SDK configure arguments, ;-separated>]
#         -P file_manager_demoboard/tests/installed_consumer/run_installed_consumer.cmake
#
# SDK_ARGUMENTS carries the build tree's own settings (compiler, LLVM runtime,
# generator), which this script does not choose; the consumer is configured
# with them too, since a consumer built by another compiler links against
# another C++ runtime than the SDK's. Each step fails the script with its
# exit status.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED GUI_FORMS_SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "run_installed_consumer: GUI_FORMS_SOURCE_DIR and WORK_DIR are required")
endif()

set(sdk_build "${WORK_DIR}/sdk-build")
set(sdk_prefix "${WORK_DIR}/sdk")
set(consumer_build "${WORK_DIR}/consumer-build")
get_filename_component(consumer_source "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

function(run_step description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "run_installed_consumer: ${description} failed (${status})")
    endif()
endfunction()

run_step("SDK configure"
    "${CMAKE_COMMAND}" -S "${GUI_FORMS_SOURCE_DIR}" -B "${sdk_build}"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_INSTALL_PREFIX=${sdk_prefix}"
    -DGUI_FORMS_INSTALL_LAB=ON
    ${SDK_ARGUMENTS})
run_step("SDK build" "${CMAKE_COMMAND}" --build "${sdk_build}")
run_step("SDK install" "${CMAKE_COMMAND}" --install "${sdk_build}")
run_step("consumer configure"
    "${CMAKE_COMMAND}" -S "${consumer_source}" -B "${consumer_build}"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_PREFIX_PATH=${sdk_prefix}"
    ${SDK_ARGUMENTS})
run_step("consumer build" "${CMAKE_COMMAND}" --build "${consumer_build}")
run_step("consumer run" "${consumer_build}/gui_forms_installed_consumer")
message(STATUS "run_installed_consumer: the installed SDK with GUIForms::Lab served the consumer")
