#pragma once
// The browser as a single-surface host (owner decision, 2026-11-15): the
// canvas is the window manager's one surface (surface_host.hpp), presented
// with WebGL2 by the page half (page/web_host.js) from the present record
// (page/web_present.hpp); DOM events arrive as the manager's records
// through the page's exports (page/web_page_exports.cpp).
#include "../page/web_present.hpp"
#include "../../surface/manager/surface_host.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// web_host.js, the page side (Emscripten --js-library). Each is proxied to
// the page's main thread: "sync" ones block the program's thread until they
// return (they take strings, read before they return), "async" ones are
// queued and return at once.
extern "C" {
// sync: binds the canvas, installs the page's listeners, gives the record's
// address to the page, and writes the canvas's CSS size and
// devicePixelRatio.
void gf_web_js_attach(gui_forms::host::web_detail::WebPresentFrame* frame, double* width, double* height,
                      double* scale);
// async: asks for an animation frame that presents the record.
void gf_web_js_request_present();
// async: whether each animation frame posts a frame record.
void gf_web_js_want_frames(int wanted);
// sync: the document's title (UTF-8).
void gf_web_js_set_title(const char* title);
// async: the canvas's CSS cursor (a CursorKind), or none while hidden.
void gf_web_js_set_cursor(int kind, int hidden);
// async: releases the canvas's pointer capture (captured 0); the canvas
// captures every press itself.
void gf_web_js_pointer_capture(int captured);
// sync: navigator.clipboard.writeText, and the page's copy of the text.
void gf_web_js_write_clipboard(const char* text);
// async: places the hidden input element at the caret (CSS pixels of the
// canvas) and says whether a text control has the keyboard there.
void gf_web_js_place_input(double x, double y, double width, double height, int editing);
// async: the canvas's parent enters or leaves full screen.
void gf_web_js_full_screen();
// sync: opens the browser's file chooser for request `request`; `accept` is
// an input element accept list. The choice arrives as a dialog record.
void gf_web_js_open_files(double request, const char* accept, int multiple);
// sync: asks where `name` is saved (the browser's save chooser where it has
// one, else a download), for request `request`; the file the program then
// closes at `path` is written there. The answer arrives as a dialog record.
void gf_web_js_save_file(double request, const char* path, const char* name, const char* accept);
// sync: the browser's colour chooser for request `request`, starting at
// 0xRRGGBB. The answer arrives as a dialog record ("#rrggbb").
void gf_web_js_choose_color(double request, unsigned int rgb);
}

namespace gui_forms::host::web_detail {

// The process's one input queue: the page's exports post to it from the
// browser's main thread before and during the run.
[[nodiscard]] surface_detail::SurfaceInputQueue& web_input_queue() noexcept;

// The fonts the run's rasters read: /gui_forms/fonts in the program's file
// system (the page's font packs mount there), or GUI_FORMS_FONT_DIR; empty
// when neither is a directory.
[[nodiscard]] std::filesystem::path web_fonts_directory();

// An input element's accept list from a dialog's filters: ".png,.jpg";
// empty (every file) when a filter holds `*` or `*.*`, or there is none.
[[nodiscard]] std::string web_accept_list(const std::vector<HostFileDialogFilter>& filters);
// A file name the program's file system and a download both take: the
// request's base name without separators, "untitled" when empty, with the
// default extension where it has none.
[[nodiscard]] std::string web_save_name(const HostSaveFileDialogRequest& save);

class WebSurfaceHost final : public surface_detail::SurfaceHost {
public:
    WebSurfaceHost() = default;

    [[nodiscard]] HostCapabilities capabilities() const override;
    // The canvas's CSS cursor is the browser's.
    [[nodiscard]] bool draws_cursor() const noexcept override { return true; }
    [[nodiscard]] std::string_view monitor_id() const noexcept override { return "web-canvas"; }
    [[nodiscard]] std::filesystem::path fonts_directory() const override;
    [[nodiscard]] gui_forms::detail::SealedPageFunctions sealed_pages() const noexcept override;
    surface_detail::SurfaceHostStart start() override;
    [[nodiscard]] surface_detail::SurfaceInputQueue& input() noexcept override { return web_input_queue(); }
    surface_detail::SurfaceView acquire_surface(std::uint32_t width, std::uint32_t height) override;
    surface_detail::SurfacePresentResult present(std::span<const surface_detail::SurfaceDeviceRect> damage) override;
    void want_frames(bool wanted) override;
    void set_title(std::string_view title) override;
    void set_cursor(CursorKind cursor) override;
    void pointer_capture(bool captured) override;
    void write_clipboard(std::string_view text) override;
    void place_text_input(Rect caret, bool editing) override;
    void toggle_full_screen() override;
    // The browser's file chooser, save chooser (or download) and colour
    // input; the folder dialog is unsupported (a page cannot choose a
    // folder of the program's file system).
    HostServiceStatus start_dialog(const HostDialogRequest& request, std::uint64_t ticket) override;

private:
    // The surface the page uploads from; reallocated only on a new size,
    // never while the page holds a present (the manager does not acquire
    // then).
    std::vector<std::uint8_t> surface_;
    std::uint32_t width_{};
    std::uint32_t height_{};
};

} // namespace gui_forms::host::web_detail
