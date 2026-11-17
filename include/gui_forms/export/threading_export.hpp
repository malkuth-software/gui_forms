#pragma once

// The mark of the Threading library's public classes (gui_forms/threading.hpp).
// The library is a static archive linked into the one module of each program
// (the one-module decision, 2026-11-15), so on Windows the mark is empty: no
// unit imports it from a DLL, and the C ABI library, the one shared library,
// exports only its gf_* and gd_* entry points. GUI_FORMS_THREADING_DLL, which
// no first-party build defines, restores dllexport/dllimport for a consumer
// that builds the library as its own DLL (GUI_FORMS_THREADING_BUILD while
// compiling it). On ELF and Mach-O the mark is default visibility, so a
// program's own export policy decides.

#if defined(_WIN32)
#if defined(GUI_FORMS_THREADING_DLL) && defined(GUI_FORMS_THREADING_BUILD)
#define GUI_FORMS_THREADING_API __declspec(dllexport)
#elif defined(GUI_FORMS_THREADING_DLL)
#define GUI_FORMS_THREADING_API __declspec(dllimport)
#else
#define GUI_FORMS_THREADING_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GUI_FORMS_THREADING_API __attribute__((visibility("default")))
#else
#define GUI_FORMS_THREADING_API
#endif
