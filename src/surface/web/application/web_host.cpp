// The browser host's program half (web_surface_host.hpp): the canvas as the
// window manager's surface, and the public entry points (web_host.hpp).
#include "../../../host/web/application/web_surface_host.hpp"
#include "../../../host/web/fonts/sealed_pages_web.hpp"
#include "../../../host/offscreen_font_lookup.hpp"
#include "gui_forms/platform/web_host.hpp"
#include "../../../platform/library_files.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <variant>

#if defined(__EMSCRIPTEN__)
#include <emscripten/threading.h>
#endif

namespace gui_forms::host::web_detail {

// The browser target is wasm32 (STXlib's web profile; no MEMORY64): the
// present record hands the page the surface's address as a 32-bit word
// (web_present.hpp), and the page reads it from HEAPU32.
static_assert(sizeof(void*) == 4U && sizeof(std::uintptr_t) == 4U, "the web host is built for wasm32");

namespace {

// The present record shared with the page: static storage, so it outlives
// the run and the page may read it until the document goes away.
WebPresentFrame present_record{};

} // namespace

surface_detail::SurfaceInputQueue& web_input_queue() noexcept {
    // Never destroyed (rule 1: process termination releases none of our state).
    static surface_detail::SurfaceInputQueue* const queue = new surface_detail::SurfaceInputQueue();
    return *queue;
}

std::filesystem::path web_fonts_directory() {
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    // A high-integrity build reads no environment variable (cmake/HighIntegrity.cmake).
    std::filesystem::path directory("/gui_forms/fonts");
#else
    const char* override_path = std::getenv("GUI_FORMS_FONT_DIR");
    std::filesystem::path directory =
        override_path != nullptr && override_path[0] != '\0' ? std::filesystem::path(override_path)
                                                             : std::filesystem::path("/gui_forms/fonts");
#endif
    if (!platform::names_directory(directory)) directory.clear();
    return directory;
}

HostCapabilities WebSurfaceHost::capabilities() const {
    const HostCapabilities offered = web_capabilities();
    return offered;
}

std::filesystem::path WebSurfaceHost::fonts_directory() const {
    const std::filesystem::path directory = web_fonts_directory();
    return directory;
}

gui_forms::detail::SealedPageFunctions WebSurfaceHost::sealed_pages() const noexcept {
    const gui_forms::detail::SealedPageFunctions pages = web_sealed_pages();
    return pages;
}

surface_detail::SurfaceHostStart WebSurfaceHost::start() {
    double width = 0.0;
    double height = 0.0;
    double ratio = 1.0;
    gf_web_js_attach(&present_record, &width, &height, &ratio);
    surface_detail::SurfaceHostStart start{};
    start.extent = Size{std::max(1.0, width), std::max(1.0, height)};
    start.scale = std::isfinite(ratio) && ratio > 0.0 ? ratio : 1.0;
    return start;
}

surface_detail::SurfaceView WebSurfaceHost::acquire_surface(std::uint32_t width, std::uint32_t height) {
    surface_detail::SurfaceView view{};
    if (width == 0U || height == 0U || width > 32768U || height > 32768U) return view;
    // The page reads the old buffer until it has presented; the manager
    // acquires only after that.
    if (present_record.pending.load(std::memory_order_acquire) != 0U) return view;
    // Counted in 64 bits: 32768 x 32768 x 4 is 4 GiB, past a 32-bit size_t.
    const std::uint64_t wanted = static_cast<std::uint64_t>(width) * height * 4U;
    if (wanted > static_cast<std::uint64_t>(SIZE_MAX)) return view;
    const std::size_t bytes = static_cast<std::size_t>(wanted);
    surface_.assign(bytes, 0U);
    width_ = width;
    height_ = height;
    view.pixels = surface_.data();
    view.row_bytes = static_cast<std::size_t>(width) * 4U;
    view.width = width;
    view.height = height;
    return view;
}

surface_detail::SurfacePresentResult WebSurfaceHost::present(std::span<const surface_detail::SurfaceDeviceRect> damage) {
    if (surface_.empty() || present_record.pending.load(std::memory_order_acquire) != 0U)
        return surface_detail::SurfacePresentResult::failed;
    present_record.width = width_;
    present_record.height = height_;
    present_record.pixels = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(surface_.data()));
    present_record.row_bytes = width_ * 4U;
    static_cast<void>(fill_web_present_rects(present_record, damage));
    present_record.pending.store(1U, std::memory_order_release);
    gf_web_js_request_present();
    return surface_detail::SurfacePresentResult::pending;
}

void WebSurfaceHost::want_frames(bool wanted) { gf_web_js_want_frames(wanted ? 1 : 0); }

void WebSurfaceHost::set_title(std::string_view title) {
    const std::string text(title);
    gf_web_js_set_title(text.c_str());
}

void WebSurfaceHost::set_cursor(CursorKind cursor) { gf_web_js_set_cursor(static_cast<int>(cursor), 0); }

void WebSurfaceHost::pointer_capture(bool captured) { gf_web_js_pointer_capture(captured ? 1 : 0); }

void WebSurfaceHost::write_clipboard(std::string_view text) {
    const std::string copied(text);
    gf_web_js_write_clipboard(copied.c_str());
}

void WebSurfaceHost::place_text_input(Rect caret, bool editing) {
    gf_web_js_place_input(caret.x, caret.y, caret.width, caret.height, editing ? 1 : 0);
}

void WebSurfaceHost::toggle_full_screen() { gf_web_js_full_screen(); }

HostServiceStatus WebSurfaceHost::start_dialog(const HostDialogRequest& request, std::uint64_t ticket) {
    // The page carries the ticket as a JavaScript number, exact below 2^53.
    constexpr std::uint64_t largest_exact_ticket = std::uint64_t{1} << 53U;
    if (ticket > largest_exact_ticket) return {HostServiceError::backend_failure};
    const double number = static_cast<double>(ticket);
    if (const HostOpenFileDialogRequest* open = std::get_if<HostOpenFileDialogRequest>(&request.payload)) {
        const std::string accept = web_accept_list((*open).filters);
        gf_web_js_open_files(number, accept.c_str(), (*open).allow_multiple ? 1 : 0);
        return {};
    }
    if (const HostSaveFileDialogRequest* save = std::get_if<HostSaveFileDialogRequest>(&request.payload)) {
        const std::string name = web_save_name(*save);
        const std::filesystem::path directory = std::filesystem::path("/web/save") / std::to_string(ticket);
        if (!platform::create_directories(directory)) return {HostServiceError::backend_failure};
        const std::string path = (directory / name).string();
        const std::string accept = web_accept_list((*save).filters);
        gf_web_js_save_file(number, path.c_str(), name.c_str(), accept.c_str());
        return {};
    }
    if (const HostColorDialogRequest* color = std::get_if<HostColorDialogRequest>(&request.payload)) {
        // The browser's colour input has no alpha.
        const unsigned int rgb = static_cast<unsigned int>((*color).initial_rgba >> 8U);
        gf_web_js_choose_color(number, rgb);
        return {};
    }
    return {HostServiceError::unsupported};
}

} // namespace gui_forms::host::web_detail

namespace gui_forms::host {

std::filesystem::path offscreen_bundled_font_directory() {
    const std::filesystem::path directory = web_detail::web_fonts_directory();
    return directory;
}

// What a page offers, and what it does not: no accessibility publication
// yet, no frame capture, no sound cues, no clipboard images, no tray, no
// window backdrops beyond the layer's alpha, and no window state reports.
// The key map is the PC convention's in every browser (an open decision for
// Apple keyboards, which a page cannot tell apart reliably).
HostCapabilities web_capabilities() {
    HostCapabilities capabilities{HostCapabilities::current_protocol_version, "web-emscripten",
            HostCapability::lifecycle | HostCapability::scale_notifications | HostCapability::monitor_geometry |
                HostCapability::occlusion | HostCapability::scheduled_wake | HostCapability::pointer_input |
                HostCapability::keyboard_input | HostCapability::text_composition |
                HostCapability::pointer_capture | HostCapability::cursor | HostCapability::clipboard |
                HostCapability::dialogs | HostCapability::typed_drag_destination |
                HostCapability::runtime_windows | HostCapability::window_frames |
                HostCapability::screen_pointer | HostCapability::window_at_point |
                HostCapability::window_ordering | HostCapability::transparent_windows |
                HostCapability::compositor_notifications | HostCapability::transient_surfaces |
                // The canvas outside the browser's visual viewport (stage VA).
                HostCapability::safe_insets,
            pc_key_map()};
#if defined(GUI_FORMS_HIGH_INTEGRITY)
    // Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).
    capabilities.available = without_capability(capabilities.available,
                                                high_integrity_compiled_out_capabilities);
#endif
    return capabilities;
}

int run_web_application(std::vector<WebApplicationWindow> windows, WebApplicationOptions application) {
#if defined(__EMSCRIPTEN__)
    if (emscripten_is_main_browser_thread()) return 3;
#endif
    web_detail::WebSurfaceHost host;
    const int code = surface_detail::run_surface_application(host, std::move(windows), std::move(application));
    return code;
}

int run_web(std::unique_ptr<Window> window, WebHostOptions options) {
    std::vector<WebApplicationWindow> windows;
    WebApplicationWindow entry;
    entry.stable_id = "main";
    entry.model = std::move(window);
    entry.options = std::move(options);
    windows.push_back(std::move(entry));
    const int code = run_web_application(std::move(windows));
    return code;
}

} // namespace gui_forms::host
