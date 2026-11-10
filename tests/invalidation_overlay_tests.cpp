// The invalidation overlay (owner request 2026-11-09; gui_forms/diagnostics/
// invalidation_overlay): the outline store records, ages out at the named
// 300 ms lifetime and cuts long labels; the regions it draws on stay inside
// the client; composition strokes each cause's colour at the outline's edge
// and fades with age; a window makes no overlay while the switch is off,
// attributes painted damage to a control's invalidation (named by its
// stable id), to layout on a resize and to a host expose for a painted area
// nothing asked for; the process switch reaches windows without a setting
// of their own, and a window's own setting wins.
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "invalidation overlay failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

std::size_t cause_index(InvalidationCause cause) {
    const std::size_t index = static_cast<std::size_t>(cause);
    return index;
}

bool records_cause(const InvalidationOverlay& overlay, InvalidationCause cause, std::string_view source) {
    for (const InvalidationOutline& outline : overlay.outlines()) {
        if (outline.cause == cause && outline.source == source) return true;
    }
    return false;
}

// An opaque black frame of `width` by `height` premultiplied BGRA pixels.
struct Frame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::byte> pixels{};

    Frame(std::uint32_t frame_width, std::uint32_t frame_height)
        : width(frame_width), height(frame_height),
          pixels(static_cast<std::size_t>(frame_width) * frame_height * 4U, std::byte{0}) {
        for (std::size_t index = 3U; index < pixels.size(); index += 4U) pixels[index] = std::byte{255};
    }

    [[nodiscard]] InvalidationOverlayTarget target() {
        InvalidationOverlayTarget made{};
        made.pixels = std::span<std::byte>(pixels);
        made.width = width;
        made.height = height;
        made.row_bytes = static_cast<std::size_t>(width) * 4U;
        return made;
    }

    // The red channel at (x, y).
    [[nodiscard]] int red(std::uint32_t x, std::uint32_t y) const {
        const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U + 2U;
        const int value = static_cast<int>(pixels[offset]);
        return value;
    }
};

void outline_store() {
    InvalidationOverlay overlay;
    const InvalidationClock::time_point start = InvalidationClock::now();
    overlay.record({20.0, 20.0, 100.0, 60.0}, InvalidationCause::control, "store.button", start);
    overlay.record({0.0, 0.0, 0.0, 10.0}, InvalidationCause::layout, "", start);
    expect(overlay.outlines().size() == 1U, "an empty rectangle records no outline");
    expect(overlay.recorded()[cause_index(InvalidationCause::control)] == 1U &&
               overlay.recorded()[cause_index(InvalidationCause::layout)] == 0U,
           "recorded counts outlines by cause");
    const std::string long_source(100U, 'x');
    overlay.record({0.0, 0.0, 10.0, 10.0}, InvalidationCause::layout, long_source,
                   start + std::chrono::milliseconds(200));
    expect(overlay.outlines().back().source.size() == maximum_invalidation_label, "a long label is cut");
    overlay.expire(start + invalidation_outline_lifetime - std::chrono::milliseconds(1));
    expect(overlay.outlines().size() == 2U, "an outline younger than its lifetime stays");
    overlay.expire(start + invalidation_outline_lifetime + std::chrono::milliseconds(1));
    expect(overlay.outlines().size() == 1U && overlay.outlines().front().cause == InvalidationCause::layout,
           "an outline older than 300 ms is dropped, the younger one stays");
    overlay.clear();
    expect(overlay.empty(), "clear drops every outline");
    expect(invalidation_outline_lifetime == std::chrono::milliseconds(300), "the lifetime is 300 ms");
    expect(invalidation_cause_name(InvalidationCause::host_expose) == "expose" &&
               invalidation_cause_name(InvalidationCause::external_content) == "external",
           "causes have legend names");
}

void composition() {
    InvalidationOverlay overlay;
    const InvalidationClock::time_point start = InvalidationClock::now();
    overlay.record({20.0, 20.0, 100.0, 60.0}, InvalidationCause::control, "button", start);
    std::vector<DevicePixelRect> regions;
    overlay.fill_regions(1.0, 320U, 200U, regions);
    expect(!regions.empty(), "an outline draws on some regions");
    for (const DevicePixelRect& region : regions) {
        expect(region.left >= 0 && region.top >= 0 && region.right <= 320 && region.bottom <= 200 &&
                   region.left <= region.right && region.top <= region.bottom,
               "the overlay's regions stay inside the client");
    }

    Frame fresh(320U, 200U);
    const DevicePixelRect whole{0, 0, 320, 200};
    overlay.compose(fresh.target(), 1.0, start, whole);
    expect(fresh.red(20U, 60U) >= 250, "a control invalidation strokes red at the outline's left edge");
    expect(fresh.red(60U, 60U) == 0, "the outline's inside is left as it was");

    Frame half(320U, 200U);
    overlay.compose(half.target(), 1.0, start + invalidation_outline_lifetime / 2, whole);
    expect(half.red(20U, 60U) > 64 && half.red(20U, 60U) < 200, "half way through its lifetime it is half faded");

    Frame clipped(320U, 200U);
    overlay.compose(clipped.target(), 1.0, start, DevicePixelRect{200, 0, 320, 10});
    expect(clipped.red(20U, 60U) == 0, "composition writes inside its clip only");

    Frame gone(320U, 200U);
    overlay.compose(gone.target(), 1.0, start + invalidation_outline_lifetime, whole);
    expect(gone.red(20U, 60U) == 0, "at its lifetime an outline is gone");
}

void window_attribution() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("attr.root"));
    std::shared_ptr<Label> label = make_control<Label>(StableId("attr.label"), "Ready");
    (*label).set_bounds({100.0, 60.0, 60.0, 16.0});
    (*root).add_child(label);
    Window window(root, Size{200.0, 120.0});
    window.perform_layout();
    NullPainter painter;
    window.set_invalidation_overlay(false);
    window.paint(painter, {0.0, 0.0, 200.0, 120.0});
    expect(window.invalidation_overlay() == nullptr, "off, a window keeps no overlay");

    window.set_invalidation_overlay(true);
    InvalidationOverlay* const overlay = window.invalidation_overlay();
    expect(overlay != nullptr && window.invalidation_overlay_enabled(), "on, the window makes its overlay");
    (*label).text = "Changed";
    window.paint(painter);
    expect(records_cause(*overlay, InvalidationCause::control, "attr.label"),
           "a control's invalidation is outlined as such, named by its stable id");

    (*overlay).clear();
    window.paint(painter, {0.0, 0.0, 40.0, 30.0});
    expect(records_cause(*overlay, InvalidationCause::host_expose, ""),
           "a painted area nothing asked for is a host expose");

    (*overlay).clear();
    window.resize(Size{240.0, 140.0});
    window.perform_layout();
    window.paint(painter);
    expect(!(*overlay).empty() && (*overlay).recorded()[cause_index(InvalidationCause::layout)] > 0U,
           "a resize is outlined as layout");

    window.record_presented_area({10.0, 10.0, 50.0, 40.0}, InvalidationCause::external_content, "shim");
    expect(records_cause(*overlay, InvalidationCause::external_content, "shim"),
           "a host's presented external content is outlined");

    window.set_invalidation_overlay(false);
    expect(window.invalidation_overlay() == nullptr, "turned off, the overlay is released");
}

void process_switch() {
    const bool before = invalidation_overlay_default();
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("switch.root"));
    Window following(root, Size{100.0, 80.0});
    std::shared_ptr<Panel> other_root = make_control<Panel>(StableId("switch.other"));
    Window opted_out(other_root, Size{100.0, 80.0});
    opted_out.set_invalidation_overlay(false);

    const std::uint64_t generation = invalidation_overlay_default_generation();
    set_invalidation_overlay_default(!before);
    expect(invalidation_overlay_default_generation() != generation, "a change of the switch counts");
    set_invalidation_overlay_default(true);
    expect(invalidation_overlay_default(), "the process switch reads on");
    expect(following.invalidation_overlay_enabled(), "a window without a setting follows the process switch");
    expect(!opted_out.invalidation_overlay_enabled(), "a window's own setting wins over the process switch");
    std::shared_ptr<Panel> late_root = make_control<Panel>(StableId("switch.late"));
    Window late(late_root, Size{100.0, 80.0});
    expect(late.invalidation_overlay_enabled(), "a window made later follows the switch too");
    opted_out.set_invalidation_overlay(std::nullopt);
    expect(opted_out.invalidation_overlay_enabled(), "a cleared setting follows the switch again");
    set_invalidation_overlay_default(false);
    expect(!following.invalidation_overlay_enabled() && !late.invalidation_overlay_enabled(),
           "turned off, every following window drops its overlay");
    set_invalidation_overlay_default(before);
}

} // namespace

int main() {
    outline_store();
    composition();
    window_attribution();
    process_switch();
    std::cout << "invalidation overlay tests passed\n";
    return EXIT_SUCCESS;
}
