// Scroll copies on the Windows host (default raster profile): a scroll that
// only moves a viewport's children by whole device pixels is applied by
// moving the raster's pixels (SurfaceRoot::request_scroll_copy), and the
// host paints only the band it exposed. After each scroll the host's raster
// equals a fresh whole-window paint of the same scene, at scales 1 and 2, for
// one scroll per frame and for two scrolls taken before one frame (the
// damage the host still holds moves with the second copy).
//
// Host scroll (GUI_FORMS_PRESENT=scroll; docs/user/presentation.md): the
// window moves its own pixels with ScrollWindowEx and only the exposed strip
// is copied; the window then equals the raster. A child window over the
// viewport makes the move refused, and the whole area is copied instead.
// Exercises the Windows host in the same translation unit.
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/scrollable_control.hpp"
#include "gui_forms/window.hpp"
#include "../src/surface/windows/application/windows_host.cpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gui_forms::host {
namespace {

// A window of 200 x 120 with a 100 x 80 viewport whose one child is a column
// of twenty distinctly coloured 80 x 20 stripes (400 tall).
struct ScrollScene final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ScrollableControl> viewport;
};

ScrollScene make_scene() {
    ScrollScene scene{};
    scene.root = make_control<Panel>(StableId("scroll-copy.root"));
    // An opaque solid ground whose paint does not depend on its size: the
    // copy's precondition beneath the viewport.
    (*scene.root).set_background(Color::rgba(240, 240, 240));
    scene.viewport = make_control<ScrollableControl>(StableId("scroll-copy.viewport"));
    (*scene.viewport).auto_scroll = true;
    (*scene.viewport).set_requested_bounds({10.0, 10.0, 100.0, 80.0});
    const std::shared_ptr<Panel> column = make_control<Panel>(StableId("scroll-copy.column"));
    (*column).set_requested_bounds({0.0, 0.0, 80.0, 400.0});
    (*column).set_background(Color::rgba(250, 250, 250));
    constexpr std::size_t stripe_count = 20U;
    for (std::size_t index = 0U; index < stripe_count; ++index) {
        const std::shared_ptr<Panel> stripe =
            make_control<Panel>(StableId("scroll-copy.stripe." + std::to_string(index)));
        const double top = static_cast<double>(index) * 20.0;
        (*stripe).set_requested_bounds({4.0, top + 2.0, 72.0, 16.0});
        const std::uint8_t red = static_cast<std::uint8_t>(40U + index * 10U);
        const std::uint8_t green = static_cast<std::uint8_t>(200U - index * 8U);
        const std::uint8_t blue = static_cast<std::uint8_t>((index * 37U) % 256U);
        (*stripe).set_background(Color::rgba(red, green, blue));
        (*column).add_child(stripe);
    }
    (*scene.viewport).add_child(column);
    (*scene.root).add_child(scene.viewport);
    return scene;
}

constexpr Size scene_size{200.0, 120.0};

struct ScrollCopyHostFixture final {
    static void require(bool condition, const std::string& message) {
        if (!condition) throw std::runtime_error(message);
    }

    // The raster's pixels, row by row, without row padding.
    static std::vector<std::uint32_t> pixels_of(render::SkiaRaster& raster) {
        const std::uint32_t width = raster.pixel_width();
        const std::uint32_t height = raster.pixel_height();
        const std::size_t row_bytes = raster.row_bytes();
        const std::uint8_t* const source = static_cast<const std::uint8_t*>(raster.pixels());
        require(source != nullptr && width != 0U && height != 0U, "the raster has pixels");
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
        for (std::uint32_t row = 0U; row < height; ++row) {
            std::memcpy(&pixels[static_cast<std::size_t>(row) * width], source + row * row_bytes,
                        static_cast<std::size_t>(width) * 4U);
        }
        return pixels;
    }

    // A fresh whole-window paint of the scene scrolled to `position`.
    static std::vector<std::uint32_t> reference_pixels(const double scale, const Point position) {
        const ScrollScene scene = make_scene();
        Window window(scene.root, scene_size);
        window.set_scale(scale);
        window.perform_layout();
        static_cast<void>((*scene.viewport).scroll_to(position));
        window.perform_layout();
        WindowsRaster raster;
        require(raster.resize(scene_size, scale), "the reference raster is allocated");
        require(raster.synchronize_images(window.image_resources()), "reference images synchronize");
        require(raster.begin_frame(), "the reference frame begins");
        require(window.paint(raster.painter()).present(), "the reference paints");
        raster.end_frame();
        return pixels_of(raster.painter());
    }

    static void send_paint(WindowsHostState& state) {
        SendMessageW(state.hwnd_, WM_PAINT, 0, 0);
    }

    static void require_matches(WindowsHostState& state, const double scale, const Point position,
                                const char* stage) {
        const std::vector<std::uint32_t> host = pixels_of(state.raster_.painter());
        const std::vector<std::uint32_t> expected = reference_pixels(scale, position);
        require(host.size() == expected.size(), std::string(stage) + ": the rasters differ in extent");
        std::size_t differing = 0U;
        for (std::size_t index = 0U; index < host.size(); ++index) {
            if (host[index] != expected[index]) ++differing;
        }
        require(differing == 0U, std::string(stage) + ": " + std::to_string(differing) +
                                     " pixels differ from a whole-window paint");
    }

    static void exercise(const ATOM native_class, const double scale) {
        const ScrollScene scene = make_scene();
        std::unique_ptr<Window> model = std::make_unique<Window>(scene.root, scene_size);
        WindowsHostOptions options{};
        options.quit_thread_on_close = false;
        options.print_metrics_on_close = false;
        options.scale_override = scale;
        WindowsHostState state(std::move(model), options);
        const int width = static_cast<int>(scene_size.width * scale);
        const int height = static_cast<int>(scene_size.height * scale);
        HWND window = CreateWindowExW(WS_EX_NOACTIVATE, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(native_class)),
                                      L"Scroll copy fixture", WS_POPUP, 0, 0, width, height, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), &state);
        require(window != nullptr, "fixture native creation");
        require(state.initialize(window), "fixture host initialization");
        require((*state.model_).scroll_copies_supported(), "the default raster profile turns scroll copies on");

        InvalidateRect(state.hwnd_, nullptr, FALSE);
        send_paint(state);
        require(state.pending_damage_.empty(), "the first frame is presented");
        require_matches(state, scale, {0.0, 0.0}, "first frame");

        // One scroll, one frame: one copy applied, and the damage painted is
        // the exposed band and the bar, not the viewport.
        require((*scene.viewport).scroll_by({0.0, 10.0}), "the viewport scrolls");
        state.collect_damage();
        require(state.pending_copies_.size() == 1U, "the scroll records one copy");
        // The exposed band, the bar, and the bar's ink over the viewport.
        const double band_and_bar = 84.0 * 10.0 + 2.0 * 16.0 * 80.0;
        require(state.pending_damage_.area() <= band_and_bar + 1.0e-6,
                "a copied scroll damages only the exposed band, the bar and its ink");
        send_paint(state);
        require(state.scroll_copies_applied_ == 1U && state.scroll_copies_refused_ == 0U,
                "the frame applies the copy");
        require(state.pending_damage_.empty() && state.pending_copies_.empty(), "the copied frame is presented");
        require_matches(state, scale, {0.0, 10.0}, "one scroll");

        // Two scrolls before one frame: the band the first exposed moves with
        // the second copy.
        require((*scene.viewport).scroll_by({0.0, 14.0}), "the viewport scrolls again");
        state.collect_damage();
        require((*scene.viewport).scroll_by({0.0, -6.0}), "the viewport scrolls back");
        state.collect_damage();
        require(state.pending_copies_.size() == 2U, "two scrolls record two copies");
        send_paint(state);
        require(state.scroll_copies_applied_ == 3U, "the frame applies both copies");
        require(state.pending_damage_.empty(), "the doubly copied frame is presented");
        require_matches(state, scale, {0.0, 18.0}, "two scrolls");

    }

    // The window's client pixels as GDI reads them back, row by row, with
    // the alpha byte cleared (the window shows no alpha in this profile).
    static std::vector<std::uint32_t> window_pixels(HWND window, const int width, const int height) {
        HDC source = GetDC(window);
        require(source != nullptr, "the window's device context");
        HDC memory = CreateCompatibleDC(source);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = memory != nullptr ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0)
                                           : nullptr;
        HGDIOBJ previous = bitmap != nullptr ? SelectObject(memory, bitmap) : nullptr;
        const BOOL copied = bitmap != nullptr && BitBlt(memory, 0, 0, width, height, source, 0, 0, SRCCOPY);
        GdiFlush();
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        if (copied != FALSE && bits != nullptr) std::memcpy(pixels.data(), bits, pixels.size() * 4U);
        if (previous != nullptr) SelectObject(memory, previous);
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (memory != nullptr) DeleteDC(memory);
        ReleaseDC(window, source);
        require(copied != FALSE && bits != nullptr, "the window's pixels are read back");
        for (std::uint32_t& pixel : pixels) pixel &= 0x00FFFFFFU;
        return pixels;
    }

    // Requires the window to show the raster, outside `excluded` (device
    // pixels; empty for none).
    static void require_window_shows_raster(WindowsHostState& state, const RECT excluded, const char* stage) {
        std::vector<std::uint32_t> raster = pixels_of(state.raster_.painter());
        const int width = static_cast<int>(state.raster_.painter().pixel_width());
        const int height = static_cast<int>(state.raster_.painter().pixel_height());
        const std::vector<std::uint32_t> shown = window_pixels(state.hwnd_, width, height);
        std::size_t differing = 0U;
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                const bool skipped = column >= excluded.left && column < excluded.right && row >= excluded.top &&
                                     row < excluded.bottom;
                if (skipped) continue;
                const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                                          static_cast<std::size_t>(column);
                if ((raster[index] & 0x00FFFFFFU) != shown[index]) ++differing;
            }
        }
        require(differing == 0U, std::string(stage) + ": " + std::to_string(differing) +
                                     " window pixels differ from the raster");
    }

    static void exercise_host_scroll(const ATOM native_class, const double scale) {
        require(_putenv_s("GUI_FORMS_PRESENT", "scroll") == 0, "the presentation policy is set");
        const ScrollScene scene = make_scene();
        std::unique_ptr<Window> model = std::make_unique<Window>(scene.root, scene_size);
        WindowsHostOptions options{};
        options.quit_thread_on_close = false;
        options.print_metrics_on_close = false;
        options.scale_override = scale;
        WindowsHostState state(std::move(model), options);
        const int width = static_cast<int>(scene_size.width * scale);
        const int height = static_cast<int>(scene_size.height * scale);
        // Shown, topmost and unactivated: ScrollWindowEx moves only what a
        // window shows.
        HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                      reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(native_class)),
                                      L"Host scroll fixture", WS_POPUP, 0, 0, width, height, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), &state);
        require(window != nullptr, "host scroll fixture native creation");
        require(state.initialize(window), "host scroll fixture host initialization");
        ShowWindow(window, SW_SHOWNOACTIVATE);
        const RECT none{0, 0, 0, 0};

        InvalidateRect(state.hwnd_, nullptr, FALSE);
        send_paint(state);
        require(state.presenter_ != nullptr, "the policy makes the window's presentation shim");
        require(state.pending_damage_.empty(), "the first frame is presented through the shim");
        require_window_shows_raster(state, none, "host scroll first frame");

        // One scroll: the window moves its pixels, and only the exposed strip
        // (and the bar) is copied, far less than the viewport.
        const presentation::PresentShimSnapshot before = (*state.presenter_).snapshot();
        require((*scene.viewport).scroll_by({0.0, 10.0}), "the viewport scrolls");
        state.collect_damage();
        send_paint(state);
        const presentation::PresentShimSnapshot after = (*state.presenter_).snapshot();
        require(after.scrolls_moved == before.scrolls_moved + 1U && after.scrolls_refused == before.scrolls_refused,
                "ScrollWindowEx moves the window's pixels");
        const double viewport_pixels = 100.0 * 80.0 * scale * scale;
        const double written = static_cast<double>(after.pixels_written - before.pixels_written);
        // Without host scroll the viewport's whole clip is copied.
        require(written < viewport_pixels, "only the exposed strip and the bar are copied, not the viewport");
        require(state.pending_damage_.empty(), "the scrolled frame is presented");
        require_matches(state, scale, {0.0, 10.0}, "host scroll raster");
        require_window_shows_raster(state, none, "host scroll window");

        // A child window over the viewport: the move is refused and the
        // whole area copied; the window still shows the raster around it.
        const int child_left = static_cast<int>(40.0 * scale);
        const int child_top = static_cast<int>(30.0 * scale);
        const int child_side = static_cast<int>(20.0 * scale);
        HWND child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, child_left, child_top, child_side,
                                     child_side, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(child != nullptr, "the covering child window");
        const presentation::PresentShimSnapshot covered_before = (*state.presenter_).snapshot();
        require((*scene.viewport).scroll_by({0.0, 12.0}), "the viewport scrolls under the child");
        state.collect_damage();
        send_paint(state);
        const presentation::PresentShimSnapshot covered_after = (*state.presenter_).snapshot();
        require(covered_after.scrolls_refused == covered_before.scrolls_refused + 1U &&
                    covered_after.scrolls_moved == covered_before.scrolls_moved,
                "a child window refuses the move");
        require_matches(state, scale, {0.0, 22.0}, "refused host scroll raster");
        const RECT child_area{child_left, child_top, child_left + child_side, child_top + child_side};
        require_window_shows_raster(state, child_area, "refused host scroll window");
        DestroyWindow(child);
        require(_putenv_s("GUI_FORMS_PRESENT", "") == 0, "the presentation policy is cleared");
    }

    static void run() {
        WNDCLASSEXW native_class{};
        native_class.cbSize = sizeof(native_class);
        native_class.lpfnWndProc = window_procedure;
        native_class.hInstance = GetModuleHandleW(nullptr);
        native_class.lpszClassName = L"GUIForms.ScrollCopyFixture.v1";
        const ATOM registered_class = RegisterClassExW(&native_class);
        require(registered_class != 0, "fixture class registration");
        constexpr std::array<double, 2> scales{1.0, 2.0};
        for (const double scale : scales) exercise(registered_class, scale);
        for (const double scale : scales) exercise_host_scroll(registered_class, scale);
        require(UnregisterClassW(native_class.lpszClassName, native_class.hInstance) != FALSE,
                "fixture class release");
    }
};

} // namespace
} // namespace gui_forms::host

int main() {
#if defined(GUI_FORMS_WINDOWS_TRANSACTIONAL_DIB)
    std::cout << "gui_forms_windows_scroll_copy_tests: skipped for the transactional DIB profile\n";
    return EXIT_SUCCESS;
#else
    try {
        gui_forms::host::ScrollCopyHostFixture::run();
    } catch (const std::exception& error) {
        std::cerr << "windows scroll copy failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "windows scroll copy tests passed\n";
    return EXIT_SUCCESS;
#endif
}
