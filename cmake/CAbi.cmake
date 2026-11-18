# The GUI.Forms C ABI, major 1 (stage C): the library, its export allowlist,
# its installation as the CAbi component (GUIForms::CAbi) and its audit tests.
# It is the library's only C surface: the experimental 0.x ABI and its
# facade generator were retired before 1.0 (owner decision D5, 2026-11-10).
#
# The library is one module (owner decision, 2026-11-15: every independently
# delivered program holds exactly one copy of any routine): it links the
# Application archives whole, with libc++, STXlib's state and the GUI.Drawing
# C APIs (gui_drawing_c_api_objects and gui_drawing_raster_c_api_objects in
# CMakeLists.txt), and exports only the gf_ entry points of the allowlist and
# GUI.Drawing's gd_get_api_v0 (whose table carries raster_initialize).
# Everything else, libc++
# included, stays local (the version script on ELF, the exported-symbols list
# on Mach-O; on Windows the link .def's entry points). On Windows it also
# holds the Win32 compatibility shim (owner, 2026-11-15, "Fold it in") and
# exports its user32 and gdi32 names, so C, C# and Win32 consumers load one
# DLL. A C or C# program loads it alone. This replaces decision C-2's shared engine: a
# program that used both the C++ library and the C ABI would hold two
# engines, so a C++ program that wants the C entry points links
# gui_forms_abi_objects statically beside Application instead, as the trace
# gates below do. A build without a native host has no Application library
# and no C ABI.
#
# Installed:
#   include/gui_forms/c/gf_abi.h          the contract (with the other headers)
#   bin/gui_forms_abi1.dll (self-contained), lib/libgui_forms_abi1.dll.a and the MSVC-format
#   lib/gui_forms_abi1.lib (Windows); lib/libgui_forms_abi.so.1 (Linux);
#   lib/libgui_forms_abi.1.dylib (macOS)
#   lib/cmake/GUIForms/GUIFormsCAbiTargets.cmake, lib/pkgconfig/gui_forms_abi.pc
#   share/GUIForms/abi/gf_abi.json         the audited manifest
#   share/GUIForms/abi/gf_abi_exports.txt  the export allowlist

if(NOT GUI_FORMS_BUILD_C_ABI)
    message(STATUS "GUI.Forms C ABI: not built (GUI_FORMS_BUILD_C_ABI is OFF)")
    return()
endif()
if(NOT TARGET gui_forms_application)
    message(STATUS "GUI.Forms C ABI: not built (this build has no Application library)")
    return()
endif()
# gf_abi.h is a 64-bit contract; the browser target is wasm32.
if(EMSCRIPTEN)
    message(STATUS "GUI.Forms C ABI: not built (the browser target is 32-bit)")
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
# GUI.Drawing's one entry point, folded into the library: gd_get_api_v0,
# whose table's raster_initialize installs the raster (there is no gdr_
# export any more).
list(APPEND _gf_abi_exports gd_get_api_v0)

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
# The exact export set, one name per line, for the export check below.
string(JOIN "\n" _gf_abi_expected_text ${_gf_abi_exports})
file(CONFIGURE OUTPUT "${_gf_abi_generated}/gf_abi_exports.txt" CONTENT "${_gf_abi_expected_text}\n" @ONLY)
# Windows: the link .def adds the Win32 compatibility shim's exports, each
# `Name=gf_compat_Name` line of src/surface/windows/win32_compat_shim.def, to the
# entry points; GUI_FORMS_ABI_WIN32_SHIM_EXPORTS lists the names for the
# export check. The .def above, without them, makes both import libraries.
set(GUI_FORMS_ABI_WIN32_SHIM_EXPORTS "")
if(WIN32)
    set(_gf_abi_shim_def "${CMAKE_CURRENT_SOURCE_DIR}/src/surface/windows/win32_compat_shim.def")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_gf_abi_shim_def}")
    file(STRINGS "${_gf_abi_shim_def}" _gf_abi_shim_lines)
    set(_gf_abi_link_def_text "${_gf_abi_def_text}")
    foreach(_gf_abi_shim_line IN LISTS _gf_abi_shim_lines)
        if(_gf_abi_shim_line MATCHES "^[ \t]+([A-Za-z][A-Za-z0-9]*)=(gf_compat_[A-Za-z0-9]+)[ \t]*$")
            list(APPEND GUI_FORMS_ABI_WIN32_SHIM_EXPORTS "${CMAKE_MATCH_1}")
            string(APPEND _gf_abi_link_def_text "    ${CMAKE_MATCH_1}=${CMAKE_MATCH_2}\n")
        endif()
    endforeach()
    if(NOT GUI_FORMS_ABI_WIN32_SHIM_EXPORTS)
        message(FATAL_ERROR "${_gf_abi_shim_def} names no export")
    endif()
    file(CONFIGURE OUTPUT "${_gf_abi_generated}/${_gf_abi_output_name}-link.def"
         CONTENT "${_gf_abi_link_def_text}" @ONLY)
endif()

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
    src/abi/stable/tables/abi_overlays.cpp
    src/abi/stable/tables/abi_property_grid.cpp
    src/abi/stable/tables/abi_custom_controls.cpp
    src/abi/stable/tables/abi_binding_provider.cpp
    src/abi/stable/tables/abi_bindings.cpp
    src/abi/stable/tables/abi_subsurfaces.cpp
    src/abi/stable/tables/abi_application.cpp
    src/abi/stable/tables/abi_rules.cpp
    src/abi/stable/tables/abi_data_grid.cpp
    src/abi/stable/tables/abi_list_view.cpp
    src/abi/stable/tables/abi_docking.cpp
    src/abi/stable/tables/abi_fonts.cpp
    src/abi/stable/tables/abi_help.cpp
    src/abi/stable/tables/abi_classes.cpp
    src/abi/stable/tables/abi_content.cpp
    src/abi/stable/classes/abi_class_bridge.cpp
    src/abi/stable/classes/generated/abi_class_registry.cpp
    src/abi/stable/tables/abi_splashscreen.cpp
    src/abi/stable/custom/abi_custom_control.cpp
    src/abi/stable/custom/abi_composite.cpp
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
    src/abi/stable/windows/abi_window_properties.cpp
)
# The slot gates (abi_registry.hpp, call_slot) are STXlib's; their state is
# stxlib_state's, which gui_forms_application brings.
target_link_libraries(gui_forms_abi_objects PRIVATE gui_forms_application stxlib::boundary)
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
# The Win32 compatibility shim, part of the library on Windows (one module,
# owner decision 2026-11-15, "Fold it in"): its gf_compat_ functions call the
# paint endpoint table directly, and the link .def below exports them under
# user32's and gdi32's names (src/surface/windows/win32_compat_shim.def). The
# Windows definitions are the shim's alone, so the other objects compile as
# before.
if(WIN32)
    set(_gf_abi_shim_source "${CMAKE_CURRENT_SOURCE_DIR}/src/surface/windows/win32_compat_shim.cpp")
    target_sources(gui_forms_abi_objects PRIVATE "${_gf_abi_shim_source}")
    set_source_files_properties("${_gf_abi_shim_source}" PROPERTIES
        COMPILE_DEFINITIONS "${GUI_FORMS_WINDOWS_DEFINITIONS}")
endif()
set_target_properties(gui_forms_abi_objects PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    POSITION_INDEPENDENT_CODE ON
)

add_library(gui_forms_abi SHARED
    $<TARGET_OBJECTS:gui_forms_abi_objects>
    $<TARGET_OBJECTS:gui_drawing_c_api_objects>)
# gui_forms_application links the engine's archives whole into the library
# (gui_forms_application_library); the raster module's objects add the
# drawing executor, whose archive is linked ordinarily.
target_link_libraries(gui_forms_abi PRIVATE gui_forms_application stxlib::boundary)
if(TARGET gui_drawing_raster_c_api_objects)
    target_sources(gui_forms_abi PRIVATE $<TARGET_OBJECTS:gui_drawing_raster_c_api_objects>)
    target_link_libraries(gui_forms_abi PRIVATE gui_drawing_skia)
else()
    # Without the renderer the table's raster_initialize fails
    # GD_ERROR_NOT_AVAILABLE (src/abi/drawing_raster_service.h).
    target_sources(gui_forms_abi PRIVATE src/abi/drawing_raster_absent.cpp)
endif()
if(WIN32)
    target_link_libraries(gui_forms_abi PRIVATE advapi32 user32 gdi32)
    # The exports: the allowlist's entry points and the shim's Win32 names.
    target_sources(gui_forms_abi PRIVATE "${_gf_abi_generated}/${_gf_abi_output_name}-link.def")
endif()
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
    # Both import libraries come from the entry points' .def, without the
    # shim's Win32 names: the MinGW one the linker wrote (from the link .def)
    # is written again, so a program linking the library still binds GetDC
    # and the rest to user32 and gdi32. A consumer that wants the shim's
    # names resolves them from the DLL at run time.
    add_custom_command(TARGET gui_forms_abi POST_BUILD
        COMMAND "${GUI_FORMS_LLVM_DLLTOOL}" -m ${_gf_abi_machine}
            -d "${_gf_abi_generated}/${_gf_abi_output_name}.def"
            -D "${_gf_abi_output_name}.dll"
            -l "${_gf_abi_msvc_import}"
        COMMAND "${GUI_FORMS_LLVM_DLLTOOL}" -m ${_gf_abi_machine}
            -d "${_gf_abi_generated}/${_gf_abi_output_name}.def"
            -D "${_gf_abi_output_name}.dll"
            -l "$<TARGET_LINKER_FILE:gui_forms_abi>"
        BYPRODUCTS "${_gf_abi_msvc_import}"
        VERBATIM)
elseif(GUI_FORMS_WINDOWS_MSVC_TARGET)
    # The MSVC target: lld-link writes the one import library, in the MSVC
    # format, from the link .def; it is written again from the entry points'
    # .def by llvm-lib, without the shim's Win32 names, as above.
    if(NOT GUI_FORMS_LLVM_LIB)
        get_filename_component(_gf_abi_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        find_program(GUI_FORMS_LLVM_LIB NAMES llvm-lib HINTS "${_gf_abi_compiler_dir}" REQUIRED)
    endif()
    add_custom_command(TARGET gui_forms_abi POST_BUILD
        COMMAND "${GUI_FORMS_LLVM_LIB}" /nologo /machine:x64
            "/def:${_gf_abi_generated}/${_gf_abi_output_name}.def"
            "/name:${_gf_abi_output_name}.dll"
            "/out:$<TARGET_LINKER_FILE:gui_forms_abi>"
        VERBATIM)
endif()

# The conformance kit (stage C8): the C11 checks of ownership, threads,
# callbacks, events, values, the schema, themes, host services, the control
# families and the window run, as a static library a consumer links and runs
# in its own tests (owner decision C-3). Installed with the CAbi component as
# GUIForms::CAbiConformance and include/gui_forms/c/gf_conformance.h;
# docs/c-abi/conformance-kit.md says how a product runs it.
find_package(Threads REQUIRED)
add_library(gui_forms_abi_conformance_kit STATIC
    src/abi/conformance/gf_conformance_bindings.c
    src/abi/conformance/gf_conformance_callbacks.c
    src/abi/conformance/gf_conformance_classes.c
    src/abi/conformance/gf_conformance_band.c
    src/abi/conformance/gf_conformance_content.c
    src/abi/conformance/gf_conformance_create.c
    src/abi/conformance/gf_conformance_custom.c
    src/abi/conformance/gf_conformance_diagnostics.c
    src/abi/conformance/gf_conformance_events.c
    src/abi/conformance/gf_conformance_families.c
    src/abi/conformance/gf_conformance_focus.c
    src/abi/conformance/gf_conformance_kit.c
    src/abi/conformance/gf_conformance_modal.c
    src/abi/conformance/gf_conformance_window_properties.c
    src/abi/conformance/gf_conformance_ownership.c
    src/abi/conformance/gf_conformance_proxies.c
    src/abi/conformance/gf_conformance_schema.c
    src/abi/conformance/gf_conformance_subsurfaces.c
    src/abi/conformance/gf_conformance_application.c
    src/abi/conformance/gf_conformance_rules.c
    src/abi/conformance/gf_conformance_data_grid.c
    src/abi/conformance/gf_conformance_list_view.c
    src/abi/conformance/gf_conformance_docking.c
    src/abi/conformance/gf_conformance_help.c
    src/abi/conformance/gf_conformance_splashscreen.c
    src/abi/conformance/gf_conformance_threads.c
    src/abi/conformance/gf_conformance_values.c
    src/abi/conformance/gf_conformance_window.c
    src/abi/conformance/gf_conformance_window_events.c
    src/abi/conformance/gf_conformance_themes.c
    src/abi/conformance/gf_conformance_host.c)
set_target_properties(gui_forms_abi_conformance_kit PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
# A product's runner (gui_forms_tests' gf_conformance_main.c among them)
# includes gf_conformance.h: beside the kit in this tree, under
# include/gui_forms/c once installed.
target_include_directories(gui_forms_abi_conformance_kit PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/abi/conformance>)
target_link_libraries(gui_forms_abi_conformance_kit PUBLIC gui_forms_abi)
if(MINGW)
    target_link_libraries(gui_forms_abi_conformance_kit PUBLIC winpthread)
else()
    target_link_libraries(gui_forms_abi_conformance_kit PUBLIC Threads::Threads)
endif()
set_target_properties(gui_forms_abi_conformance_kit PROPERTIES EXPORT_NAME CAbiConformance
                      OUTPUT_NAME gui_forms_abi_conformance_kit)
install(TARGETS gui_forms_abi_conformance_kit
        EXPORT GUIFormsCAbiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/abi/conformance/gf_conformance.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gui_forms/c)

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
# The substitution faces' licences and attribution, in the fonts directory
# beside the library, where raster_initialize looks (CMakeLists.txt,
# gui_drawing_raster_fonts), and beside it in the build.
if(TARGET gui_drawing_raster_c_api_objects)
    add_custom_command(TARGET gui_forms_abi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:gui_forms_abi>/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${gui_drawing_raster_fonts} "$<TARGET_FILE_DIR:gui_forms_abi>/fonts"
        VERBATIM)
    if(WIN32)
        install(FILES ${gui_drawing_raster_fonts} DESTINATION ${CMAKE_INSTALL_BINDIR}/fonts)
    else()
        install(FILES ${gui_drawing_raster_fonts} DESTINATION ${CMAKE_INSTALL_LIBDIR}/fonts)
    endif()
endif()
set(GUI_FORMS_ABI_PKGCONFIG_LIBRARY "${_gf_abi_output_name}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/gui_forms_abi.pc.in"
               "${_gf_abi_generated}/gui_forms_abi.pc" @ONLY)
install(FILES "${_gf_abi_generated}/gui_forms_abi.pc" DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)


