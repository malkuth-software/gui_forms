#pragma once

// The export macro of the Application library (libgui_forms_application)
// (CP29 handoff section 2.2). A class, function or variable of the public
// C++ API leaves the library only when its declaration carries
// GUI_FORMS_API: on ELF and Mach-O through the export list the build
// generates from the marks (tools/export_annotations.py places the macro,
// writes the lists under cmake/exports/ and checks both), on Windows
// through dllexport, without --export-all-symbols. The shared test
// runtime, which holds the same archives, exports everything instead,
// because the tests call private functions.
//
// Which form the macro takes is set by the build, per target:
// - GUI_FORMS_API_BUILD: the unit is compiled into one of those libraries
//   (the core, drawing core, controls, plot primitives, renderers, hosts and
//   the library's own sources): dllexport on Windows;
// - GUI_FORMS_API_IMPORT: the unit links one of those libraries (the
//   Application library's and the test runtime's usage requirement):
//   dllimport on Windows;
// - neither: the unit links the static archives into its own image, and the
//   macro is empty on Windows.
// On ELF and Mach-O the macro is default visibility in every case, so a
// consumer compiled with -fvisibility=hidden still references the library's
// symbols as default-visibility ones.

#if defined(_WIN32)
#if defined(GUI_FORMS_API_BUILD)
#define GUI_FORMS_API __declspec(dllexport)
#elif defined(GUI_FORMS_API_IMPORT)
#define GUI_FORMS_API __declspec(dllimport)
#else
#define GUI_FORMS_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GUI_FORMS_API __attribute__((visibility("default")))
#else
#define GUI_FORMS_API
#endif
