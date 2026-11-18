# The full consumer-macro shield (tools/consumer_shield.py), for C++
# consumers who cannot `import gui_forms;`: gui_forms/gf_shield_begin.h and
# gf_shield_end.h save, remove and restore the consumer's macros for every
# open name of every header under include/gui_forms. It is generated at build
# time into <tree>/consumer_shield/gui_forms, never checked in, and installed
# from there beside the other headers. The install checks the installed pair
# against the headers it was built from, so installing a stale shield fails.
#
# The C shield (gui_forms/c/gf_c_shield_begin.h and gf_c_shield_end.h, the
# names of the C ABI headers and gf_abi.hpp) is small, checked in, and
# carried by those headers; cmake/HostileMacroConformance.cmake checks it.
#
# Included from CMakeLists.txt after the header install, with gui_forms_core
# defined. Generating needs Python 3; without it the full shield is neither
# built nor installed, and configuring says so.

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(WARNING "No Python 3 interpreter: the consumer-macro shield for C++ without modules "
        "(gui_forms/gf_shield_begin.h, gf_shield_end.h) is neither generated nor installed")
    return()
endif()

set(GUI_FORMS_CONSUMER_SHIELD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/consumer_shield")
set(_gf_shield_output "${GUI_FORMS_CONSUMER_SHIELD_DIRECTORY}/gui_forms")
set(_gf_shield_files "${_gf_shield_output}/gf_shield_begin.h" "${_gf_shield_output}/gf_shield_end.h")
# The include directories a consumer compiles with, transitively (STXlib's
# among them), one per line: the closure reaches through them.
set(_gf_shield_roots "${GUI_FORMS_CONSUMER_SHIELD_DIRECTORY}/include_directories.txt")
file(GENERATE OUTPUT "${_gf_shield_roots}"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:gui_forms_core,INTERFACE_INCLUDE_DIRECTORIES>,\n>\n")
file(GLOB_RECURSE _gf_shield_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gui_forms/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gui_forms/*.hpp")
set(_gf_shield_command ${Python3_EXECUTABLE} -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/consumer_shield.py" full
    --root "${CMAKE_CURRENT_SOURCE_DIR}" --include-directories-file "${_gf_shield_roots}")
add_custom_command(OUTPUT ${_gf_shield_files}
    COMMAND ${_gf_shield_command} --output-dir "${_gf_shield_output}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/consumer_shield.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/consumer_macros.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/manifests/standard_library_names.txt"
        "${_gf_shield_roots}" ${_gf_shield_headers}
    COMMENT "Generating the consumer-macro shield"
    VERBATIM)
add_custom_target(gui_forms_consumer_shield ALL DEPENDS ${_gf_shield_files})

install(FILES ${_gf_shield_files} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gui_forms)
if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    set(_gf_shield_installed "\$ENV{DESTDIR}${CMAKE_INSTALL_INCLUDEDIR}/gui_forms")
else()
    set(_gf_shield_installed "\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/gui_forms")
endif()
list(JOIN _gf_shield_command "\" \"" _gf_shield_arguments)
install(CODE "
execute_process(COMMAND \"${_gf_shield_arguments}\" --check --output-dir \"${_gf_shield_installed}\"
    RESULT_VARIABLE _gf_shield_result)
if(NOT _gf_shield_result EQUAL 0)
    message(FATAL_ERROR \"The installed consumer-macro shield does not match the headers: build the \"
        \"gui_forms_consumer_shield target, then install again\")
endif()")
