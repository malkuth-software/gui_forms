#pragma once

// The mark of the public C++ API (CP29 handoff section 2.2). The Application
// library is a static archive linked into the one module of each program
// (one module, owner decision 2026-11-15), so no shipped library exports
// the marked declarations now. tools/export_annotations.py still places the
// mark and keeps the lists under cmake/exports/ in step with it: the marks
// record the public surface, and the lists stay ready for a consumer that
// builds its own shared library from the archives. The shared test runtime,
// which holds the same archives and is never shipped, exports everything,
// because the tests call private functions.
//
// Which form the macro takes is set by the build, per target:
// - GUI_FORMS_API_BUILD: the unit is compiled into a shared library that
//   exports the marks (the test runtime's own source): dllexport on Windows;
// - GUI_FORMS_API_IMPORT: the unit links such a library (the test runtime's
//   usage requirement): dllimport on Windows;
// - neither: the unit is compiled into or links the static archives, which
//   every shipped program does, and the macro is empty on Windows.
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
