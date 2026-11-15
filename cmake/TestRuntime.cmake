# One test runtime for the test programs (audit S9; coordinator,
# 2026-11-10), a static archive since the one-module follow-up of
# 2026-11-15.
#
# gui_forms_link_tests_to_runtime() builds gui_forms_test_runtime, a static
# archive whose usage requirement links the toolkit's own libraries (the
# threading library, core, drawing core, plot primitives, controls and the
# headless host) ordinarily into the consumer, exactly as the test programs
# linked them before the runtime existed and as GUI_FORMS_TEST_RUNTIME=OFF
# still does. Every test program whose libraries are all among those links
# the runtime instead of them, keeping their compile usage
# ($<COMPILE_ONLY:...>), so the programs keep one link shape.
#
# It was a shared library, so that a library change relinked one runtime
# instead of every program. It is static now because no shared object of
# ours may carry the statically linked C++ runtime (one module, owner
# decision 2026-11-15): a shared runtime held its own copy of libc++ beside
# each program's, and Alpine's libc++abi.a is not position independent, so
# the shared runtime could not link there at all. The cost is the relink:
# a library change relinks each program that links the runtime again, as
# before audit S9. Each program is one image with one copy of every
# library's state, so nothing relies on a state shared across a test's
# modules, and no program needs excluding (the former exclusions compared
# addresses or read state across the program and the shared runtime, and
# replaced operator new in the program image only).
#
# GUI_FORMS_TEST_RUNTIME (default ON) turns it off: every program then links
# its static libraries ordinarily. It needs CMake 3.27 ($<COMPILE_ONLY>); an
# older CMake keeps the ordinary links.
option(GUI_FORMS_TEST_RUNTIME "Link the test programs that use only the toolkit's own libraries through one static test runtime that holds them whole" ON)

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
        message(STATUS "Test programs: static links (the test runtime needs CMake 3.27)")
        return()
    endif()
    set(runtime_libraries gui_forms_threading gui_forms_core gui_drawing_core gui_forms_plot_primitives gui_forms_controls)
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

    add_library(gui_forms_test_runtime STATIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/test_runtime.cpp)
    # Ordinary archive links: the linker takes the members each program
    # reaches, as it did before the runtime and does with the option off.
    target_link_libraries(gui_forms_test_runtime PUBLIC ${runtime_libraries})
    foreach(program IN LISTS programs)
        get_target_property(links ${program} LINK_LIBRARIES)
        set(compile_only "")
        foreach(library IN LISTS links)
            list(APPEND compile_only "$<COMPILE_ONLY:${library}>")
        endforeach()
        set_property(TARGET ${program} PROPERTY LINK_LIBRARIES ${compile_only} gui_forms_test_runtime)
    endforeach()
    message(STATUS "Test programs: ${program_count} link the static test runtime (cmake/TestRuntime.cmake)")
endfunction()
