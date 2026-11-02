# The GUI.Forms C ABI, major 1 (stage C): the library, its export allowlist,
# its installation as the CAbi component (GUIForms::CAbi) and its audit tests.
# The experimental 0.x ABI (gui_forms_c_api, include/gui_forms/c_api.h) is
# separate and stays uninstalled until its last consumer moves.
#
# The library links the Application shared library (owner decision C-2), so
# a process has one GUI.Forms engine whether it uses C++, the C ABI or both.
# A build without a native host has no Application library and no C ABI.
#
# Installed:
#   include/gui_forms/c/gf_abi.h          the contract (with the other headers)
#   bin/gui_forms_abi1.dll, lib/libgui_forms_abi1.dll.a and the MSVC-format
#   lib/gui_forms_abi1.lib (Windows); lib/libgui_forms_abi.so.1 (Linux);
#   lib/libgui_forms_abi.1.dylib (macOS)
#   lib/cmake/GUIForms/GUIFormsCAbiTargets.cmake, lib/pkgconfig/gui_forms_abi.pc
#   share/GUIForms/abi/gf_abi.json         the audited manifest
#   share/GUIForms/abi/gf_abi_exports.txt  the export allowlist

if(NOT TARGET gui_forms_application)
    message(STATUS "GUI.Forms C ABI: not built (this build has no Application library)")
    return()
endif()

set(_gf_abi_header "${CMAKE_CURRENT_SOURCE_DIR}/include/gui_forms/c/gf_abi.h")
set(_gf_abi_allowlist "${CMAKE_CURRENT_SOURCE_DIR}/manifests/abi/gf_abi_exports.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gf_abi_header}" "${_gf_abi_allowlist}")

# The library version follows the header's ABI version.
file(STRINGS "${_gf_abi_header}" _gf_abi_major_line REGEX "^#define GF_ABI_MAJOR [0-9]+u$")
file(STRINGS "${_gf_abi_header}" _gf_abi_minor_line REGEX "^#define GF_ABI_MINOR [0-9]+u$")
string(REGEX REPLACE "^#define GF_ABI_MAJOR ([0-9]+)u$" "\\1" GUI_FORMS_ABI_MAJOR "${_gf_abi_major_line}")
string(REGEX REPLACE "^#define GF_ABI_MINOR ([0-9]+)u$" "\\1" GUI_FORMS_ABI_MINOR "${_gf_abi_minor_line}")
if(NOT GUI_FORMS_ABI_MAJOR MATCHES "^[0-9]+$" OR NOT GUI_FORMS_ABI_MINOR MATCHES "^[0-9]+$")
    message(FATAL_ERROR "gf_abi.h must define GF_ABI_MAJOR and GF_ABI_MINOR as plain literals")
endif()

# The allowlist: one symbol per line, # starts a comment.
file(STRINGS "${_gf_abi_allowlist}" _gf_abi_allowlist_lines)
set(_gf_abi_exports)
foreach(_gf_abi_line IN LISTS _gf_abi_allowlist_lines)
    string(STRIP "${_gf_abi_line}" _gf_abi_line)
    if(_gf_abi_line AND NOT _gf_abi_line MATCHES "^#")
        list(APPEND _gf_abi_exports "${_gf_abi_line}")
    endif()
endforeach()
if(NOT _gf_abi_exports)
    message(FATAL_ERROR "${_gf_abi_allowlist} names no symbol")
endif()

if(WIN32)
    set(_gf_abi_output_name "gui_forms_abi${GUI_FORMS_ABI_MAJOR}")
else()
    set(_gf_abi_output_name "gui_forms_abi")
endif()

# The linker inputs generated from the allowlist. file(CONFIGURE) rewrites a
# file only when its content changes, so configuring again does not relink.
set(_gf_abi_generated "${CMAKE_CURRENT_BINARY_DIR}/gf_abi")
set(_gf_abi_map_text "/* Generated from manifests/abi/gf_abi_exports.txt by cmake/CAbi.cmake. */\n{\n    global:\n")
set(_gf_abi_macos_text "")
set(_gf_abi_def_text "LIBRARY ${_gf_abi_output_name}.dll\nEXPORTS\n")
foreach(_gf_abi_symbol IN LISTS _gf_abi_exports)
    string(APPEND _gf_abi_map_text "        ${_gf_abi_symbol};\n")
    string(APPEND _gf_abi_macos_text "_${_gf_abi_symbol}\n")
    string(APPEND _gf_abi_def_text "    ${_gf_abi_symbol}\n")
endforeach()
string(APPEND _gf_abi_map_text "    local: *;\n};\n")
file(CONFIGURE OUTPUT "${_gf_abi_generated}/gf_abi_exports.map" CONTENT "${_gf_abi_map_text}" @ONLY)
file(CONFIGURE OUTPUT "${_gf_abi_generated}/gf_abi_exports_macos.txt" CONTENT "${_gf_abi_macos_text}" @ONLY)
file(CONFIGURE OUTPUT "${_gf_abi_generated}/${_gf_abi_output_name}.def" CONTENT "${_gf_abi_def_text}" @ONLY)

# The library's objects, compiled once: the shared library is made from
# them, and the stage C4 trace gate links them statically beside the C++
# library so both paths share one process.
add_library(gui_forms_abi_objects OBJECT
    src/abi/stable/engine/abi_engine.cpp
    src/abi/stable/entry/abi_entry.cpp
    src/abi/stable/events/abi_event_fields.cpp
    src/abi/stable/failure/abi_failure.cpp
    src/abi/stable/registry/abi_registry.cpp
    src/abi/stable/schema/abi_create_with.cpp
    src/abi/stable/schema/abi_schema.cpp
    src/abi/stable/schema/abi_schema_recipes.cpp
    src/abi/stable/tables/abi_diagnostics.cpp
    src/abi/stable/tables/abi_controls.cpp
    src/abi/stable/tables/abi_text_editing.cpp
    src/abi/stable/tables/abi_popups.cpp
    src/abi/stable/tables/abi_property_grid.cpp
    src/abi/stable/tables/abi_custom_controls.cpp
    src/abi/stable/tables/abi_bindings.cpp
    src/abi/stable/custom/abi_custom_control.cpp
    src/abi/stable/windows/abi_windows_paint_endpoint.cpp
    src/abi/stable/tables/abi_dispatch.cpp
    src/abi/stable/tables/abi_events.cpp
    src/abi/stable/tables/abi_host.cpp
    src/abi/stable/tables/abi_objects.cpp
    src/abi/stable/tables/abi_properties.cpp
    src/abi/stable/tables/abi_schema_table.cpp
    src/abi/stable/tables/abi_themes.cpp
    src/abi/stable/tables/abi_windows.cpp
    src/abi/stable/values/abi_values.cpp
    src/abi/stable/windows/abi_window_handle.cpp
)
target_link_libraries(gui_forms_abi_objects PRIVATE gui_forms_application)
target_include_directories(gui_forms_abi_objects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gui_forms_abi_objects PRIVATE cxx_std_20)
# GF_ABI_BUILD is public: a program linking the objects declares the entry
# as the objects define it.
target_compile_definitions(gui_forms_abi_objects
    PUBLIC GF_ABI_BUILD
    PRIVATE
    GF_ABI_LIBRARY_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    GF_ABI_LIBRARY_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    GF_ABI_LIBRARY_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
# The Windows build carries the paint endpoint table
# (GF_TABLE_WINDOWS_PAINT_ENDPOINT) over the Windows host the application
# library contains.
if(TARGET gui_forms_host_windows)
    target_compile_definitions(gui_forms_abi_objects PRIVATE GF_ABI_HAS_WINDOWS_PAINT_ENDPOINT=1)
endif()
set_target_properties(gui_forms_abi_objects PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    POSITION_INDEPENDENT_CODE ON
)

add_library(gui_forms_abi SHARED $<TARGET_OBJECTS:gui_forms_abi_objects>)
target_link_libraries(gui_forms_abi PRIVATE gui_forms_application)
add_library(GUIForms::CAbi ALIAS gui_forms_abi)
target_include_directories(gui_forms_abi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(gui_forms_abi PROPERTIES
    LINKER_LANGUAGE CXX
    EXPORT_NAME CAbi
    OUTPUT_NAME "${_gf_abi_output_name}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    POSITION_INDEPENDENT_CODE ON
    VERSION "${GUI_FORMS_ABI_MAJOR}.${GUI_FORMS_ABI_MINOR}.0"
    SOVERSION "${GUI_FORMS_ABI_MAJOR}"
)
# The library links libgui_forms_application privately, so it finds it, and
# what it needs, beside itself once installed (GUI_FORMS_INSTALLED_LIBRARY_RPATH).
set_target_properties(gui_forms_abi PROPERTIES INSTALL_RPATH "${GUI_FORMS_INSTALLED_LIBRARY_RPATH}")
if(APPLE)
    target_link_options(gui_forms_abi PRIVATE
        "LINKER:-exported_symbols_list,${_gf_abi_generated}/gf_abi_exports_macos.txt")
    set_property(TARGET gui_forms_abi APPEND PROPERTY LINK_DEPENDS "${_gf_abi_generated}/gf_abi_exports_macos.txt")
elseif(UNIX)
    target_link_options(gui_forms_abi PRIVATE "LINKER:--version-script=${_gf_abi_generated}/gf_abi_exports.map")
    set_property(TARGET gui_forms_abi APPEND PROPERTY LINK_DEPENDS "${_gf_abi_generated}/gf_abi_exports.map")
elseif(WIN32)
    set_target_properties(gui_forms_abi PROPERTIES PREFIX "")
endif()

# Windows: an import library in the MSVC format beside the MinGW one, so a
# Visual Studio program links the DLL. llvm-dlltool writes it from the .def
# the allowlist generated.
set(_gf_abi_msvc_import "")
if(WIN32 AND MINGW)
    get_filename_component(_gf_abi_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(GUI_FORMS_LLVM_DLLTOOL NAMES llvm-dlltool HINTS "${_gf_abi_compiler_dir}" REQUIRED)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
        set(_gf_abi_machine arm64)
    else()
        set(_gf_abi_machine i386:x86-64)
    endif()
    set(_gf_abi_msvc_import "${CMAKE_CURRENT_BINARY_DIR}/${_gf_abi_output_name}.lib")
    add_custom_command(TARGET gui_forms_abi POST_BUILD
        COMMAND "${GUI_FORMS_LLVM_DLLTOOL}" -m ${_gf_abi_machine}
            -d "${_gf_abi_generated}/${_gf_abi_output_name}.def"
            -D "${_gf_abi_output_name}.dll"
            -l "${_gf_abi_msvc_import}"
        BYPRODUCTS "${_gf_abi_msvc_import}"
        VERBATIM)
endif()

install(TARGETS gui_forms_abi
        EXPORT GUIFormsCAbiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(EXPORT GUIFormsCAbiTargets
        FILE GUIFormsCAbiTargets.cmake
        NAMESPACE GUIForms::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
if(_gf_abi_msvc_import)
    install(FILES "${_gf_abi_msvc_import}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/manifests/abi/gf_abi.json"
        "${_gf_abi_allowlist}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/GUIForms/abi)
set(GUI_FORMS_ABI_PKGCONFIG_LIBRARY "${_gf_abi_output_name}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/gui_forms_abi.pc.in"
               "${_gf_abi_generated}/gui_forms_abi.pc" @ONLY)
install(FILES "${_gf_abi_generated}/gui_forms_abi.pc" DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(GUI_FORMS_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    get_filename_component(_gf_abi_tool_dir "${CMAKE_C_COMPILER}" DIRECTORY)

    # Version negotiation and the error model, from C11.
    add_executable(gui_forms_abi_runtime_c11_tests tests/abi/gf_abi_runtime_c11_tests.c)
    target_link_libraries(gui_forms_abi_runtime_c11_tests PRIVATE gui_forms_abi)
    # The fixture starts a second thread with pthread_create; CLANG64's libc++
    # uses native Windows threads and brings no pthread dependency of its own.
    if(MINGW)
        target_link_libraries(gui_forms_abi_runtime_c11_tests PRIVATE winpthread)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(gui_forms_abi_runtime_c11_tests PRIVATE Threads::Threads)
    endif()
    add_test(NAME gui_forms_abi_runtime_c11_tests COMMAND gui_forms_abi_runtime_c11_tests)

    # The conformance kit, part 1 (ownership, threads, callbacks, the window
    # run), as consumers will run it, and the check that it calls every slot.
    add_library(gui_forms_abi_conformance_kit STATIC
        tests/abi/conformance/gf_conformance_bindings.c
        tests/abi/conformance/gf_conformance_callbacks.c
        tests/abi/conformance/gf_conformance_create.c
        tests/abi/conformance/gf_conformance_custom.c
        tests/abi/conformance/gf_conformance_diagnostics.c
        tests/abi/conformance/gf_conformance_events.c
        tests/abi/conformance/gf_conformance_families.c
        tests/abi/conformance/gf_conformance_kit.c
        tests/abi/conformance/gf_conformance_ownership.c
        tests/abi/conformance/gf_conformance_proxies.c
        tests/abi/conformance/gf_conformance_schema.c
        tests/abi/conformance/gf_conformance_threads.c
        tests/abi/conformance/gf_conformance_values.c
        tests/abi/conformance/gf_conformance_window.c
        tests/abi/conformance/gf_conformance_themes.c
        tests/abi/conformance/gf_conformance_host.c)
    set_target_properties(gui_forms_abi_conformance_kit PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_link_libraries(gui_forms_abi_conformance_kit PUBLIC gui_forms_abi)
    if(MINGW)
        target_link_libraries(gui_forms_abi_conformance_kit PUBLIC winpthread)
    else()
        target_link_libraries(gui_forms_abi_conformance_kit PUBLIC Threads::Threads)
    endif()
    add_executable(gui_forms_abi_conformance tests/abi/conformance/gf_conformance_main.c)
    target_link_libraries(gui_forms_abi_conformance PRIVATE gui_forms_abi_conformance_kit)
    add_test(NAME gui_forms_abi_conformance COMMAND gui_forms_abi_conformance)
    set_tests_properties(gui_forms_abi_conformance PROPERTIES TIMEOUT 60)
    add_test(NAME gui_forms_abi_slot_coverage
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_audit.py coverage
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/abi/conformance ${CMAKE_CURRENT_SOURCE_DIR}/tests/abi/schema
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/abi/host)

    # The stage C3 gate: the control schema read through the ABI alone equals
    # manifests/control-schema.json. The schema as the ABI gave it is left in
    # the build tree for comparison.
    add_executable(gui_forms_abi_schema_export tests/abi/schema/gf_abi_schema_export.cpp)
    target_link_libraries(gui_forms_abi_schema_export PRIVATE gui_forms_abi)
    target_compile_features(gui_forms_abi_schema_export PRIVATE cxx_std_20)
    add_test(NAME gui_forms_abi_schema_export
        COMMAND gui_forms_abi_schema_export ${CMAKE_CURRENT_SOURCE_DIR}/manifests/control-schema.json
            ${CMAKE_CURRENT_BINARY_DIR}/gf_abi/control-schema.abi.json)
    # The stage C4 gate: a theme loaded through the ABI paints a fixture as
    # the C++ path does.
    add_executable(gui_forms_abi_theme_trace tests/abi/themes/gf_abi_theme_trace.cpp)
    target_link_libraries(gui_forms_abi_theme_trace PRIVATE gui_forms_abi_objects gui_forms_application)
    target_compile_features(gui_forms_abi_theme_trace PRIVATE cxx_std_20)
    add_test(NAME gui_forms_abi_theme_trace COMMAND gui_forms_abi_theme_trace ${CMAKE_CURRENT_BINARY_DIR}/gf_abi)
    # The host's capability report through the ABI, against the per-host
    # matrix every host must match.
    add_executable(gui_forms_abi_capability_matrix tests/abi/host/gf_abi_capability_matrix.c)
    target_link_libraries(gui_forms_abi_capability_matrix PRIVATE gui_forms_abi)
    add_test(NAME gui_forms_abi_capability_matrix
        COMMAND gui_forms_abi_capability_matrix ${CMAKE_CURRENT_SOURCE_DIR}/manifests/abi/host_capabilities.json)
    # The registry and the ABI's generated type catalog agree.
    add_test(NAME gui_forms_abi_schema_catalog_current
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/control_registry.py emit-abi --check)
    # The stage C5 fields: C++ events raised directly reach an ABI callback
    # as named fields, and what the callback writes reaches the C++ payload.
    add_executable(gui_forms_abi_event_fields tests/abi/events/gf_abi_event_fields.cpp)
    target_link_libraries(gui_forms_abi_event_fields PRIVATE gui_forms_abi_objects gui_forms_application)
    target_compile_features(gui_forms_abi_event_fields PRIVATE cxx_std_20)
    add_test(NAME gui_forms_abi_event_fields COMMAND gui_forms_abi_event_fields)
    # A custom control's paint callback fills the pixels a window presents.
    add_executable(gui_forms_abi_custom_paint tests/abi/custom/gf_abi_custom_paint.cpp)
    target_link_libraries(gui_forms_abi_custom_paint PRIVATE gui_forms_abi_objects gui_forms_application)
    target_compile_features(gui_forms_abi_custom_paint PRIVATE cxx_std_20)
    add_test(NAME gui_forms_abi_custom_paint COMMAND gui_forms_abi_custom_paint)
    add_test(NAME gui_forms_abi_event_catalog_current
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_event_catalog.py --check)

    # The C5 gate: the 0.x surface the Windows Forms facade uses, mapped onto
    # stable slots and schema names (pending families are listed, not failed).
    add_test(NAME gui_forms_abi_compat_map
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_compat_map.py)

    # The manifest's sizes and offsets, asserted by this host's compiler in C11 and C++.
    add_executable(gui_forms_abi_layout_probe_c11 tests/abi/gf_abi_layout_probe.c tests/abi/gf_abi_coexistence.c)
    target_include_directories(gui_forms_abi_layout_probe_c11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME gui_forms_abi_layout_probe_c11 COMMAND gui_forms_abi_layout_probe_c11)
    add_executable(gui_forms_abi_layout_probe_cpp tests/abi/gf_abi_layout_probe_cpp.cpp)
    target_include_directories(gui_forms_abi_layout_probe_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME gui_forms_abi_layout_probe_cpp COMMAND gui_forms_abi_layout_probe_cpp)

    # The rules, the current manifest and the current probe.
    add_test(NAME gui_forms_abi_audit
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_audit.py check
            --clang ${CMAKE_C_COMPILER})
    add_test(NAME gui_forms_abi_layout_probe_current
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_audit.py probe --check)
    # Every seeded break is reported, and no legal change is.
    add_test(NAME gui_forms_abi_audit_fixtures
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/abi/abi_audit_tests.py
            --clang ${CMAKE_C_COMPILER})
    # The built library exports exactly the allowlist.
    set(_gf_abi_export_arguments --library $<TARGET_FILE:gui_forms_abi> --allowlist ${_gf_abi_allowlist}
        --tool-dir ${_gf_abi_tool_dir})
    if(_gf_abi_msvc_import)
        list(APPEND _gf_abi_export_arguments --import-library ${_gf_abi_msvc_import})
    endif()
    add_test(NAME gui_forms_abi_exports
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_audit.py exports
            ${_gf_abi_export_arguments})
    set_tests_properties(gui_forms_abi_audit gui_forms_abi_audit_fixtures PROPERTIES TIMEOUT 300)
endif()
