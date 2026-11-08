#include "gui_forms/canvas.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/window.hpp"
#if GUI_FORMS_EXAMPLE_NATIVE
#include "gui_forms/application.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Application-owned straight RGBA document. In particular, transparent RGB
// remains document data and is never round-tripped through the display bitmap.
class Document final {
public:
    std::array<std::uint8_t, 4U * 4U * 4U> pixels{};
    std::array<std::uint8_t, 4U * 4U * 4U> undo{};

    void publish(gui_drawing::Bitmap& display, gui_drawing::RectI rectangle) const {
        gui_drawing::BitmapEditView edit = display.begin_edit(rectangle);
        for (std::int32_t y = 0; y < rectangle.height; ++y) {
            for (std::int32_t x = 0; x < rectangle.width; ++x) {
                const std::size_t source =
                    (static_cast<std::size_t>(rectangle.y + y) * 4U +
                     static_cast<std::size_t>(rectangle.x + x)) * 4U;
                std::byte* target = edit.writable_data +
                    static_cast<std::size_t>(y) * edit.row_bytes +
                    static_cast<std::size_t>(x) * 4U;
                const unsigned alpha = pixels[source + 3U];
                // Rounded premultiplication: p = (c * alpha + 127) / 255.
                target[0] = static_cast<std::byte>((pixels[source + 2U] * alpha + 127U) / 255U);
                target[1] = static_cast<std::byte>((pixels[source + 1U] * alpha + 127U) / 255U);
                target[2] = static_cast<std::byte>((pixels[source] * alpha + 127U) / 255U);
                target[3] = static_cast<std::byte>(alpha);
            }
        }
        static_cast<void>(display.commit_edit(edit.token));
    }
};

class PointerObserver final : public gui_forms::Component {
public:
    explicit PointerObserver(gui_forms::RasterCanvas& canvas) : canvas_(canvas) {}
    void observe(const gui_forms::PointerEvent& event) {
        // Only presses and releases are counted; other actions pass unobserved.
        if (event.action == gui_forms::PointerAction::down) {
            canvas_.set_pointer_capture(true);
            ++presses;
        } else if (event.action == gui_forms::PointerAction::up) {
            canvas_.set_pointer_capture(false);
            ++releases;
        }
    }
    unsigned presses{};
    unsigned releases{};
private:
    gui_forms::RasterCanvas& canvas_;
};
#if GUI_FORMS_EXAMPLE_NATIVE
struct CloseWhenReady final {
    void operator()(gui_forms::Window& window, gui_forms::ApplicationWindowHandle handle) const {
        require(window.host_services() != nullptr && handle.request_close().accepted(),
                "installed native application did not attach services or accept close");
    }
};
#endif
} // namespace

int main() {
    // The program's application (APP_WINDOW_RULES_DESIGN A.2): windows made
    // from here join it, and it runs them.
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    if (!created) return 1;
    Application& application = *created.application;
    using namespace gui_forms;
    Document document;
    document.pixels[0] = 231U; // Hidden color in a completely transparent pixel.
    document.undo = document.pixels;
    const std::shared_ptr<gui_drawing::Bitmap> display =
        std::make_shared<gui_drawing::Bitmap>(4U, 4U);
    document.publish(*display, {0, 0, 4, 4});
    const std::shared_ptr<Control> root = make_control<Control>(StableId("paint.contract.root"));
    const std::shared_ptr<RasterCanvas> canvas = make_control<RasterCanvas>(StableId("paint.contract.canvas"));
    (*root).set_requested_bounds({0, 0, 100, 100});
    (*canvas).set_requested_bounds({10, 20, 40, 40});
    (*canvas).set_view(2.0, {0, 0});
    (*canvas).set_bitmap(display);
    (*root).add_child(canvas);
    std::unique_ptr<Window> window = std::make_unique<Window>(root, Size{100, 100});
    (*window).perform_layout();
    static_cast<void>((*window).take_damage());

    const std::size_t edited = (1U * 4U + 1U) * 4U;
    document.pixels[edited] = 200U;
    document.pixels[edited + 3U] = 128U;
    document.publish(*display, {1, 1, 1, 1});
    require((*canvas).synchronize_bitmap(), "display synchronization failed");
    require((*window).take_damage().bounds() == (Rect{12, 22, 2, 2}),
            "single-pixel edit did not produce bounded viewport damage");
    const std::vector<ImageId> images = (*window).image_resources().image_ids();
    require(images.size() == 1U, "unexpected display resource ownership");
    const ImageResourceView resource = *(*window).image_resources().find(images.front());
    require(resource.encoded[edited + 2U] == std::byte{100} &&
            resource.encoded[edited + 3U] == std::byte{128},
            "straight RGBA to premultiplied BGRA conversion failed");
    require(document.pixels[0] == 231U, "display conversion destroyed hidden document color");

    PointerObserver observer(*canvas);
    SubscriptionToken token = (*canvas).pointer_observed().subscribe(
        observer, Delegate<const PointerEvent&>::bind<PointerObserver, &PointerObserver::observe>(observer));
    static_cast<void>((*window).dispatch_pointer({PointerAction::down, PointerButton::primary, {12, 22}}));
    require((*canvas).has_pointer_capture(), "canvas did not capture the pointer");
    static_cast<void>((*window).dispatch_pointer({PointerAction::up, PointerButton::primary, {90, 90}}));
    require(observer.presses == 1U && observer.releases == 1U && !(*canvas).has_pointer_capture(),
            "pointer release outside the canvas lost the capture route");

    (*canvas).set_view(4.0, {1, 1});
    require((*canvas).client_to_bitmap({8, 4}) == (gui_drawing::PointF{3, 2}),
            "zoom and pan coordinate mapping failed");
    document.pixels = document.undo;
    document.publish(*display, {0, 0, 4, 4});
    require((*canvas).synchronize_bitmap() && document.pixels[0] == 231U &&
            document.pixels[edited] == 0U, "application-owned undo failed");
    observer.dispose();
    require(!token.connected(), "controller disposal left a live delegate");
    std::cout << "installed consumer: pixel conversion, damage, capture, zoom/pan, undo, lifetime passed\n";
#if GUI_FORMS_EXAMPLE_NATIVE
    ApplicationWindowOptions options;
    options.title = "GUI.Forms installed consumer";
    options.initial_size = {240, 180};
    options.ready = CloseWhenReady{};
    options.print_metrics_on_close = false;
    const ApplicationResult result = application.run(std::move(window), std::move(options));
    // Once, after the last run: ends the rendering library for the process.
    const ApplicationResult ended = Application::shutdown();
    return result.accepted() && ended.accepted() ? 0 : 1;
#else
    return 0;
#endif
}
