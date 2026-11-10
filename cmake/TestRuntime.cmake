# One shared test runtime for the test programs (audit S9; coordinator,
# 2026-11-10: the test programs stop relinking the world on every change).
#
# Every test program used to link the toolkit's static libraries, so a
# change to any library source relinked each program that links it (about
# 210 for a core source). gui_forms_link_tests_to_runtime() builds one
# shared library, gui_forms_test_runtime, holding the whole of the
# toolkit's own libraries (the core, drawing core, plot primitives,
# controls and headless host, each whole), and links to it instead every
# test program whose libraries are all among those. The program keeps the
# libraries' compile usage ($<COMPILE_ONLY:...>: include directories,
# definitions, options) and links only the runtime, and LINK_DEPENDS_NO_SHARED
# keeps a rebuilt runtime from relinking it: a library change relinks the
# runtime once, and each program only when its own sources change.
#
# Each program is still its own process with its own main, so the tests
# stay independent. A program that links anything else (another library,
# a host, Skia, a system library) keeps its static libraries, so that no
# program holds two copies of a library's state. The runtime exports every
# symbol (on MinGW --export-all-symbols; ELF and Mach-O export by default),
# as the Application library does for its consumers, so the programs reach
# internal functions as before. On Windows the runtime lands beside the
# programs, which find it first.
#
# GUI_FORMS_TEST_RUNTIME (default ON) turns it off: every program then links
# its static libraries as before. It needs CMake 3.27 ($<COMPILE_ONLY>); an
# older CMake keeps the static links.
option(GUI_FORMS_TEST_RUNTIME "Link the test programs that use only the toolkit's own libraries to one shared test runtime" ON)

# Whether every entry of `links` (a target's LINK_LIBRARIES) is one of
# `runtime_libraries`; sets `result` to TRUE or FALSE.
function(_gui_forms_links_within_runtime links runtime_libraries result)
    if(NOT links)
        set(${result} FALSE PARENT_SCOPE)
        return()
    endif()
    foreach(library IN LISTS links)
        if(NOT library IN_LIST runtime_libraries)
            set(${result} FALSE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${result} TRUE PARENT_SCOPE)
endfunction()

# Builds gui_forms_test_runtime and relinks the calling directory's test
# programs (executables named *_tests) that link only the runtime's
# libraries. Call once, after the last test program is defined and before
# the directory's first-party options and the shared precompiled header are
# applied (they read the programs' link libraries).
function(gui_forms_link_tests_to_runtime)
    if(NOT GUI_FORMS_TEST_RUNTIME)
        message(STATUS "Test programs: static links (GUI_FORMS_TEST_RUNTIME=OFF)")
        return()
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.27)
        message(STATUS "Test programs: static links (the shared test runtime needs CMake 3.27)")
        return()
    endif()
    set(runtime_libraries gui_forms_core gui_drawing_core gui_forms_plot_primitives gui_forms_controls)
    if(TARGET gui_forms_host_headless)
        list(APPEND runtime_libraries gui_forms_host_headless)
    endif()
    get_property(directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(programs "")
    foreach(target IN LISTS directory_targets)
        get_target_property(target_type ${target} TYPE)
        if(NOT target_type STREQUAL "EXECUTABLE" OR NOT target MATCHES "_tests$")
            continue()
        endif()
        get_target_property(links ${target} LINK_LIBRARIES)
        _gui_forms_links_within_runtime("${links}" "${runtime_libraries}" within)
        if(within)
            list(APPEND programs ${target})
        endif()
    endforeach()
    list(LENGTH programs program_count)
    if(program_count EQUAL 0)
        return()
    endif()

    add_library(gui_forms_test_runtime SHARED ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/test_runtime.cpp)
    # Whole archives, spelled as the Application library spells them: the
    # libraries also reach the link through each other's usage, which
    # $<LINK_LIBRARY:WHOLE_ARCHIVE> would refuse as a conflict.
    if(APPLE)
        foreach(library IN LISTS runtime_libraries)
            target_link_libraries(gui_forms_test_runtime PRIVATE ${library} "-Wl,-force_load,$<TARGET_FILE:${library}>")
        endforeach()
    elseif(MSVC)
        foreach(library IN LISTS runtime_libraries)
            target_link_libraries(gui_forms_test_runtime PRIVATE ${library} "/WHOLEARCHIVE:$<TARGET_FILE:${library}>")
        endforeach()
        set_target_properties(gui_forms_test_runtime PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    else()
        target_link_libraries(gui_forms_test_runtime PRIVATE
            "-Wl,--whole-archive" ${runtime_libraries} "-Wl,--no-whole-archive")
    endif()
    if(MINGW)
        target_link_options(gui_forms_test_runtime PRIVATE "-Wl,--export-all-symbols")
    endif()
    foreach(program IN LISTS programs)
        get_target_property(links ${program} LINK_LIBRARIES)
        set(compile_only "")
        foreach(library IN LISTS links)
            list(APPEND compile_only "$<COMPILE_ONLY:${library}>")
        endforeach()
        set_property(TARGET ${program} PROPERTY LINK_LIBRARIES ${compile_only} gui_forms_test_runtime)
        set_property(TARGET ${program} PROPERTY LINK_DEPENDS_NO_SHARED ON)
    endforeach()
    message(STATUS "Test programs: ${program_count} link the shared test runtime (cmake/TestRuntime.cmake)")
endfunction()
