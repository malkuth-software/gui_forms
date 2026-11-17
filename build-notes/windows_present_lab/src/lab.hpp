#pragma once

// Windows presentation-surface lab (GUI.Forms handoff item 14). Standalone:
// nothing here is GUI.Forms code, and the GPU-related routes are probes, not
// candidates for the library. Every route prints "route.key=value" lines on
// standard output; the study quotes them.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace lab {

// Prints one result line: "<route>.<key>=<formatted value>".
void report(const char* route, const char* key, const char* format, ...);
// Prints an HRESULT as hex with "ok" or "fail".
void report_hr(const char* route, const char* key, HRESULT result);
// Prints a BOOL Win32 result with GetLastError on failure.
void report_bool(const char* route, const char* key, BOOL result);

// The test image: premultiplied BGRA, a function of position and phase so a
// readback can tell a frame, a partial update and a scroll apart. With
// `with_alpha`, the left quarter is half transparent and the next quarter
// clear (alpha 0); the rest is opaque.
std::uint32_t pattern_pixel(int x, int y, int phase, bool with_alpha);
// Fills `rows` x `columns` of `pixels` (row stride `stride_pixels`) whose
// first pixel stands at image position (`origin_x`, `origin_y`).
void fill_pattern(std::uint32_t* pixels, int columns, int rows, int stride_pixels,
                  int origin_x, int origin_y, int phase, bool with_alpha);

// A window procedure target: `paint` runs on WM_PAINT with `context`; null
// means the window validates and paints nothing.
struct PaintTarget final {
    void (*paint)(HWND window, void* context){};
    void* context{};
};

inline constexpr int window_width = 320;
inline constexpr int window_height = 240;

// Creates and shows a top-level popup of window_width x window_height at
// (40, 40) whose WM_PAINT calls `target` (borrowed for the window's life).
HWND create_window(const wchar_t* title, DWORD extended_style, PaintTarget* target, bool show);
// Runs the message loop for about `milliseconds` without sleeping.
void pump(DWORD milliseconds);

// What a readback found: whether a method produced an image at all, and how
// many of the sampled pixels (colour channels only) match the expectation.
struct Readback final {
    bool available{};
    int samples{};
    int matches{};
    int blank{};
};
// Reads the window's client area back with PrintWindow(PW_RENDERFULLCONTENT)
// and with BitBlt from its window DC, compares a grid of opaque samples with
// pattern_pixel(phase) shifted up by `scrolled_rows`, and reports both.
void report_readback(const char* route, const char* key, HWND window, int phase, int scrolled_rows);

// The session and desktop this run sees: remote session, DWM composition,
// desktop name, OS build.
void report_environment();

template <typename Interface>
void release(Interface*& pointer) noexcept {
    if (pointer != nullptr) {
        (*pointer).Release();
        pointer = nullptr;
    }
}

// Routes, one per source file.
void run_gdi_routes();
void run_dxgi_routes();
void run_dcomp_routes();
void run_presentation_manager_route();
void run_winrt_route();

} // namespace lab
