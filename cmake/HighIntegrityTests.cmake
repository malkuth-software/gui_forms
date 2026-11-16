# The high-integrity configuration's tests (cmake/HighIntegrity.cmake), all
# labelled high_integrity.
#
# Every tree with tests (an ordinary one with GUI_FORMS_BUILD_TESTS, a
# high-integrity one with GUI_FORMS_BUILD_HIGH_INTEGRITY_TESTS) runs:
#   - gui_forms_build_profile_tests: build_profile() reports the tree's
#     profile and allowances;
#   - gui_forms_high_integrity_refuses_*: a configure of this source with
#     GUI_FORMS_HIGH_INTEGRITY and a contradicting setting, or an allowance
#     without the option, stops before anything is built, naming the setting.
# A high-integrity tree also runs gui_forms_high_integrity_damaged_pack_fails_fast
# and gui_forms_high_integrity_surface (below).

if(NOT GUI_FORMS_BUILD_TESTS AND NOT GUI_FORMS_BUILD_HIGH_INTEGRITY_TESTS)
    return()
endif()
enable_testing()

add_executable(gui_forms_build_profile_tests tests/build_profile_tests.cpp)
target_link_libraries(gui_forms_build_profile_tests PRIVATE gui_forms_core gui_forms_host_headless)
add_test(NAME gui_forms_build_profile_tests COMMAND gui_forms_build_profile_tests)
set_tests_properties(gui_forms_build_profile_tests PROPERTIES LABELS high_integrity)

# The refusals configure this source in a scratch tree with this tree's
# toolchain. They stop in cmake/HighIntegrity.cmake, before the stx package
# or anything else is built, so each costs a project() call.
set(_gf_high_integrity_refusal_arguments
    "-G${CMAKE_GENERATOR}"
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
# Each record: the test's name suffix, the settings it adds, and the text the
# configure must print.
set(_gf_high_integrity_refusals
    "hardening_off|-DGUI_FORMS_HIGH_INTEGRITY=ON,-DGUI_FORMS_HARDENING=OFF|GUI_FORMS_HARDENING=OFF: needs ON"
    "automation_on|-DGUI_FORMS_HIGH_INTEGRITY=ON,-DGUI_FORMS_WINDOWS_AUTOMATION_ENVIRONMENT=ON|GUI_FORMS_WINDOWS_AUTOMATION_ENVIRONMENT=ON: needs OFF"
    "math_dispatch_on|-DGUI_FORMS_HIGH_INTEGRITY=ON,-DSTXLIB_MATH_DISPATCH=ON|STXLIB_MATH_DISPATCH=ON: needs OFF"
    "gallery_on|-DGUI_FORMS_HIGH_INTEGRITY=ON,-DGUI_FORMS_BUILD_GALLERY=ON|GUI_FORMS_BUILD_GALLERY=ON: needs OFF"
    "allowance_alone|-DGUI_FORMS_HIGH_INTEGRITY=OFF,-DGUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT=ON|needs GUI_FORMS_HIGH_INTEGRITY=ON")
foreach(_gf_high_integrity_refusal IN LISTS _gf_high_integrity_refusals)
    string(REPLACE "|" ";" _gf_high_integrity_fields "${_gf_high_integrity_refusal}")
    list(GET _gf_high_integrity_fields 0 _gf_high_integrity_suffix)
    list(GET _gf_high_integrity_fields 1 _gf_high_integrity_settings)
    list(GET _gf_high_integrity_fields 2 _gf_high_integrity_expected)
    string(REPLACE "," ";" _gf_high_integrity_settings "${_gf_high_integrity_settings}")
    # A fresh scratch tree each run: the refused settings stay in its cache.
    set(_gf_high_integrity_scratch "${CMAKE_CURRENT_BINARY_DIR}/high-integrity-refusals/${_gf_high_integrity_suffix}")
    add_test(NAME gui_forms_high_integrity_refuses_${_gf_high_integrity_suffix}
        COMMAND "${CMAKE_COMMAND}" --fresh -S "${CMAKE_CURRENT_SOURCE_DIR}" -B "${_gf_high_integrity_scratch}"
            ${_gf_high_integrity_refusal_arguments} ${_gf_high_integrity_settings})
    # PASS_REGULAR_EXPRESSION ignores the exit code; the text appears only on
    # the refusal (a FATAL_ERROR), so it proves the configure stopped there.
    set_tests_properties(gui_forms_high_integrity_refuses_${_gf_high_integrity_suffix} PROPERTIES
        LABELS high_integrity
        PASS_REGULAR_EXPRESSION "${_gf_high_integrity_expected}"
        TIMEOUT 300)
endforeach()

if(NOT GUI_FORMS_HIGH_INTEGRITY)
    return()
endif()

# A font pack that does not open fails fast (record_pack_refusal in
# src/core/resources/fonts/bundled_font_set.cpp); the test reaches the set's
# private header. The abort is a signal on POSIX, so the mode runs through
# cmake/expect_fail_fast.cmake.
target_include_directories(gui_forms_build_profile_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/resources/fonts)
add_test(NAME gui_forms_high_integrity_damaged_pack_fails_fast
    COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:gui_forms_build_profile_tests> -DMODE=damaged_pack
            "-DEXPECTED=fonts\\.bundled_pack"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/expect_fail_fast.cmake)
set_tests_properties(gui_forms_high_integrity_damaged_pack_fails_fast PROPERTIES LABELS high_integrity)

# The object surface (gui_forms_high_integrity_surface), checked on a linked
# program (owner, 2026-11-16: a high-integrity build holds exactly what the
# program uses): examples/high_integrity, an application with its embedded
# theme and two stock controls on the native host, linked as a shipped
# program is (Release, static, dead stripping; cmake/HighIntegrity.cmake).
# What it reaches is in it; what it does not reach is stripped. The test
# fails when the program defines or imports a symbol of
# manifests/high_integrity/absent_symbols.txt (cmake/CheckSystemImports.cmake
# with FORBIDDEN): an environment read, a user-locale read, a theme request
# from the arguments, or an entry the configuration compiles out. The text
# stack and the C++ runtime are inside the program too, so a getenv they make
# (HarfBuzz's HB_OPTIONS, FreeType's FREETYPE_PROPERTIES) fails it as well.
# The same proof, as a component file for template_harness's
# check_object_surface.py, is manifests/high_integrity/object_surface/gui_forms.toml.
set(_gf_high_integrity_binaries "")
if(TARGET gui_forms_application)
    add_executable(gui_forms_high_integrity_example examples/high_integrity/high_integrity_example.cpp)
    target_compile_features(gui_forms_high_integrity_example PRIVATE cxx_std_20)
    target_link_libraries(gui_forms_high_integrity_example PRIVATE gui_forms_application)
    gui_forms_add_theme(gui_forms_high_integrity_example examples/high_integrity/theme
                        FUNCTION high_integrity_example_theme NAMESPACE high_integrity_example)
    set(_gf_high_integrity_binaries "$<TARGET_FILE:gui_forms_high_integrity_example>")
endif()
# The list the test reads: absent_symbols.txt, and the clipboard text
# entries unless the build keeps that allowance.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/manifests/high_integrity/absent_symbols.txt" _gf_high_integrity_absent)
if(NOT GUI_FORMS_HIGH_INTEGRITY_ALLOW_CLIPBOARD_TEXT)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/manifests/high_integrity/absent_clipboard_text.txt"
         _gf_high_integrity_clipboard_text)
    string(APPEND _gf_high_integrity_absent "${_gf_high_integrity_clipboard_text}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/high_integrity_absent_symbols.txt" "${_gf_high_integrity_absent}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/high_integrity/absent_symbols.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifests/high_integrity/absent_clipboard_text.txt")
if(NOT _gf_high_integrity_binaries STREQUAL "")
    add_test(NAME gui_forms_high_integrity_surface
        COMMAND "${CMAKE_COMMAND}" "-DTOOL_NM=${CMAKE_NM}" "-DTOOL_OBJDUMP=${CMAKE_OBJDUMP}"
            "-DBINARIES=${_gf_high_integrity_binaries}"
            "-DFORBIDDEN=${CMAKE_CURRENT_BINARY_DIR}/high_integrity_absent_symbols.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckSystemImports.cmake")
    set_tests_properties(gui_forms_high_integrity_surface PROPERTIES LABELS high_integrity TIMEOUT 600)
endif()
