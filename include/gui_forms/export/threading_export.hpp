#pragma once

// The export macro of the Threading library (libgui_forms_threading, CP29
// handoff section 2.2). It exports only the classes of
// gui_forms/threading.hpp, which carry GUI_FORMS_THREADING_API: on ELF and
// Mach-O through the export list generated from the marks
// (cmake/exports/gui_forms_threading.*, tools/export_annotations.py), on
// MinGW through dllexport, without --export-all-symbols. The library is
// always shared, so every other unit imports it: GUI_FORMS_THREADING_BUILD,
// defined only while compiling the library, selects dllexport on Windows.
// On ELF and Mach-O the macro is default visibility in every case.

#if defined(_WIN32)
#if defined(GUI_FORMS_THREADING_BUILD)
#define GUI_FORMS_THREADING_API __declspec(dllexport)
#else
#define GUI_FORMS_THREADING_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GUI_FORMS_THREADING_API __attribute__((visibility("default")))
#else
#define GUI_FORMS_THREADING_API
#endif
