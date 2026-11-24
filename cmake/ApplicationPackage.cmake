# The installed Application (one module, owner decision 2026-11-15): the
# engine a C++ program links statically, as two archives and one targets file.
#
# In the build tree GUIForms::Application is a static archive whose usage
# requirement links the engine's archives whole (gui_forms_application_library
# in CMakeLists.txt). Those archives, the renderer and the text stack are
# build-tree targets the SDK does not export one by one, so the install
# merges them:
#   lib/libgui_forms_application.a        the runtime's own archive and every
#                                         archive in its
#                                         GUI_FORMS_APPLICATION_ARCHIVES (the
#                                         core, drawing core, controls, host,
#                                         Skia adapter and text engine),
#                                         linked whole into the program;
#   lib/libgui_forms_application_deps.a   stx_skia, linked
#                                         ordinarily, so
#                                         the program takes only the members
#                                         it reaches;
#   lib/cmake/GUIForms/GUIFormsApplicationTargets.cmake
#                                         GUIForms::Application, an interface
#                                         target over the two
#                                         (GUIFormsApplicationTargets.cmake.in).
# The STXlib and stx packages, GUIForms::Threading, GUIForms::Memory and
# GUIForms::PlotPrimitives
# are installed archives of their own, which the interface names; the
# program's link therefore holds one copy of each routine.
#
# The merge runs after the archives are built (a POST_BUILD step of the
# Application library) with Apple's libtool on macOS and llvm-ar's MRI mode
# elsewhere (cmake/MergeArchives.cmake).
if(NOT TARGET gui_forms_application OR EMSCRIPTEN)
    return()
endif()

get_target_property(_gf_package_archives gui_forms_application GUI_FORMS_APPLICATION_ARCHIVES)
set(_gf_package_engine_inputs "$<TARGET_FILE:gui_forms_application>")
foreach(_gf_package_archive IN LISTS _gf_package_archives)
    list(APPEND _gf_package_engine_inputs "$<TARGET_FILE:${_gf_package_archive}>")
endforeach()
set(_gf_package_dependency_inputs "")
foreach(_gf_package_dependency IN ITEMS stx_skia::stx_skia)
    if(TARGET ${_gf_package_dependency})
        list(APPEND _gf_package_dependency_inputs "$<TARGET_FILE:${_gf_package_dependency}>")
    endif()
endforeach()

if(APPLE)
    find_program(GUI_FORMS_ARCHIVE_MERGER NAMES libtool PATHS /usr/bin NO_DEFAULT_PATH REQUIRED)
    set(_gf_package_merge_kind libtool)
else()
    get_filename_component(_gf_package_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(GUI_FORMS_ARCHIVE_MERGER NAMES llvm-ar llvm-ar-22 HINTS "${_gf_package_compiler_dir}")
    # Without llvm-ar (Alpine's clang package carries none), the toolchain's
    # own archiver: GNU ar reads the same MRI script (-M).
    if(NOT GUI_FORMS_ARCHIVE_MERGER AND CMAKE_AR)
        set(GUI_FORMS_ARCHIVE_MERGER "${CMAKE_AR}" CACHE FILEPATH "Archive merger (llvm-ar or ar, MRI scripts)" FORCE)
    endif()
    if(NOT GUI_FORMS_ARCHIVE_MERGER)
        message(FATAL_ERROR "the application package needs llvm-ar or an ar that reads MRI scripts (CMAKE_AR)")
    endif()
    set(_gf_package_merge_kind mri)
endif()

set(_gf_package_directory "${CMAKE_CURRENT_BINARY_DIR}/application-package")
set(_gf_package_engine "${_gf_package_directory}/libgui_forms_application.a")
set(_gf_package_dependencies "${_gf_package_directory}/libgui_forms_application_deps.a")
# Every archive in the merge is a dependency of the Application library's
# link (its PUBLIC archives), so a POST_BUILD step of a target that depends
# on all of them sees them built. A custom target keeps the merge out of the
# Application library itself, which the tests link without needing it.
add_custom_target(gui_forms_application_package ALL
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${_gf_package_engine}" "-DINPUTS=${_gf_package_engine_inputs}"
            "-DTOOL=${GUI_FORMS_ARCHIVE_MERGER}" "-DKIND=${_gf_package_merge_kind}"
            -P "${CMAKE_CURRENT_LIST_DIR}/MergeArchives.cmake"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${_gf_package_dependencies}" "-DINPUTS=${_gf_package_dependency_inputs}"
            "-DTOOL=${GUI_FORMS_ARCHIVE_MERGER}" "-DKIND=${_gf_package_merge_kind}"
            -P "${CMAKE_CURRENT_LIST_DIR}/MergeArchives.cmake"
    BYPRODUCTS "${_gf_package_engine}" "${_gf_package_dependencies}"
    COMMENT "GUI.Forms: the installed Application's merged archives"
    VERBATIM)
add_dependencies(gui_forms_application_package gui_forms_application ${_gf_package_archives})
install(FILES "${_gf_package_engine}" "${_gf_package_dependencies}" DESTINATION ${CMAKE_INSTALL_LIBDIR})

# What the interface target names beside the two archives: the host's system
# libraries as a consumer spells them (frameworks by name, Windows import
# libraries by name, X11 and ATK found again by the targets file), and the
# whole-archive option for the engine.
set(GUI_FORMS_PACKAGE_FIND_DEPENDENCIES "")
if(APPLE)
    set(GUI_FORMS_PACKAGE_SYSTEM_LIBRARIES
        "-framework AppKit;-framework QuartzCore;-framework IOSurface;-framework CoreGraphics;-framework CoreText;-framework UniformTypeIdentifiers;-framework CoreFoundation")
    # Desktop notifications; not linked in a high-integrity build.
    if(NOT GUI_FORMS_HIGH_INTEGRITY)
        string(APPEND GUI_FORMS_PACKAGE_SYSTEM_LIBRARIES ";-framework UserNotifications")
    endif()
    set(GUI_FORMS_PACKAGE_WHOLE_ARCHIVE
        "LINKER:-force_load,\${_gui_forms_application_prefix}/${CMAKE_INSTALL_LIBDIR}/libgui_forms_application.a")
elseif(WIN32 AND MINGW)
    set(GUI_FORMS_PACKAGE_SYSTEM_LIBRARIES
        "user32;gdi32;imm32;ole32;oleaut32;oleacc;uiautomationcore;uuid;comdlg32;shell32;advapi32;winpthread")
    set(GUI_FORMS_PACKAGE_WHOLE_ARCHIVE
        "SHELL:-Wl,--whole-archive \${_gui_forms_application_prefix}/${CMAKE_INSTALL_LIBDIR}/libgui_forms_application.a -Wl,--no-whole-archive")
elseif(WIN32)
    # The MSVC target (x86_64-pc-windows-msvc, own runtime included): no
    # winpthread, which is MinGW's, and lld-link's own whole-archive option,
    # since it ignores the GNU spelling.
    set(GUI_FORMS_PACKAGE_SYSTEM_LIBRARIES
        "user32;gdi32;imm32;ole32;oleaut32;oleacc;uiautomationcore;uuid;comdlg32;shell32;advapi32")
    set(GUI_FORMS_PACKAGE_WHOLE_ARCHIVE
        "LINKER:/WHOLEARCHIVE:\${_gui_forms_application_prefix}/${CMAKE_INSTALL_LIBDIR}/libgui_forms_application.a")
else()
    set(GUI_FORMS_PACKAGE_FIND_DEPENDENCIES [=[
find_dependency(X11)
find_dependency(PkgConfig)
if(NOT TARGET PkgConfig::GUI_FORMS_ATK)
    pkg_check_modules(GUI_FORMS_ATK REQUIRED IMPORTED_TARGET atk atk-bridge-2.0 gio-2.0)
endif()
]=])
    set(GUI_FORMS_PACKAGE_SYSTEM_LIBRARIES "X11::X11;X11::Xfixes;X11::Xrender;PkgConfig::GUI_FORMS_ATK")
    foreach(_gf_package_x11 IN ITEMS Xcursor Xrandr Xext Xi)
        if(TARGET X11::${_gf_package_x11})
            string(APPEND GUI_FORMS_PACKAGE_SYSTEM_LIBRARIES ";X11::${_gf_package_x11}")
        endif()
    endforeach()
    # The X11 direct presentation path's libXpresent, linked by name.
    if(GUI_FORMS_XPRESENT_AVAILABLE)
        string(APPEND GUI_FORMS_PACKAGE_SYSTEM_LIBRARIES ";Xpresent")
    endif()
    if(CMAKE_DL_LIBS)
        string(APPEND GUI_FORMS_PACKAGE_SYSTEM_LIBRARIES ";${CMAKE_DL_LIBS}")
    endif()
    set(GUI_FORMS_PACKAGE_WHOLE_ARCHIVE
        "SHELL:-Wl,--whole-archive \${_gui_forms_application_prefix}/${CMAKE_INSTALL_LIBDIR}/libgui_forms_application.a -Wl,--no-whole-archive")
endif()
# The definitions the host's archives make public in the build tree.
set(GUI_FORMS_PACKAGE_DEFINITIONS "")
if(WIN32)
    set(GUI_FORMS_PACKAGE_DEFINITIONS "_WIN32_WINNT=0x0A00;WINVER=0x0A00")
endif()
# The MCP transport (CMakeLists.txt: gui_forms_application links stxlib::mcp
# privately when GUI_FORMS_MCP is on): the engine archive calls into it, so the
# installed interface names the STXlib package's stxlib::mcp before the
# libraries it links.
set(GUI_FORMS_PACKAGE_MCP_LIBRARIES "")
if(GUI_FORMS_MCP AND NOT GUI_FORMS_HIGH_INTEGRITY AND TARGET stxlib::mcp)
    set(GUI_FORMS_PACKAGE_MCP_LIBRARIES "stxlib::mcp;")
endif()
# The prefix, from lib/cmake/GUIForms where the targets file is installed.
file(RELATIVE_PATH GUI_FORMS_PACKAGE_PREFIX_FROM_CONFIG
     "/prefix/${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms" "/prefix")
configure_file("${CMAKE_CURRENT_LIST_DIR}/GUIFormsApplicationTargets.cmake.in"
               "${_gf_package_directory}/GUIFormsApplicationTargets.cmake" @ONLY)
install(FILES "${_gf_package_directory}/GUIFormsApplicationTargets.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GUIForms)
