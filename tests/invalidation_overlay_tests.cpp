// The invalidation overlay (owner request 2026-11-09; gui_forms/diagnostics/
// invalidation_overlay): the outline store records, ages out at the named
// 300 ms lifetime and cuts long labels; the regions it draws on stay inside
// the client; composition strokes each cause's colour at the outline's edge
// and fades with age; a window makes no overlay while the switch is off,
// attributes painted damage to a control's invalidation (named by its
// stable id), to layout on a resize and to a host expose for a painted area
// nothing asked for; the process switch reaches windows without a setting
// of their own, and a window's own setting wins; the counters card (the
// diagnostic overlay) keeps bounded lines, draws at the top left with no
// outline and no legend, and a window fills it from its metrics.
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stxlib/optional.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

// D15 Q3: set_invalidation_overlay and set_tool_tips_shown keep their
// deprecated std::optional forms for one stage; a plain bool or {} must not
// reach them silently. Each plain call below is refused (a deleted overload
// or an ambiguity), and the stxlib form is accepted.
template <typename Root>
concept takes_plain_overlay = requires(Root& root) { root.set_invalidation_overlay(true); };
template <typename Root>
concept takes_empty_overlay = requires(Root& root) { root.set_invalidation_overlay({}); };
template <typename Root>
concept takes_plain_tool_tips = requires(Root& root) { root.set_tool_tips_shown(false); };
template <typename Root>
concept takes_empty_tool_tips = requires(Root& root) { root.set_tool_tips_shown({}); };
template <typename Root>
concept takes_optional_switches = requires(Root& root) {
    root.set_invalidation_overlay(stxlib::Optional<bool>::of(true));
    root.set_tool_tips_shown(stxlib::Optional<bool>::none());
};
static_assert(!takes_plain_overlay<Window> && !takes_empty_overlay<Window>);
static_assert(!takes_plain_tool_tips<Window> && !takes_empty_tool_tips<Window>);
static_assert(takes_optional_switches<Window>);

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
    window.set_invalidation_overlay(stxlib::Optional<bool>::of(false));
    window.paint(painter, {0.0, 0.0, 200.0, 120.0});
    expect(window.invalidation_overlay() == nullptr, "off, a window keeps no overlay");

    window.set_invalidation_overlay(stxlib::Optional<bool>::of(true));
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

    window.set_invalidation_overlay(stxlib::Optional<bool>::of(false));
    expect(window.invalidation_overlay() == nullptr, "turned off, the overlay is released");
}

void process_switch() {
    const bool before = invalidation_overlay_default();
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("switch.root"));
    Window following(root, Size{100.0, 80.0});
    std::shared_ptr<Panel> other_root = make_control<Panel>(StableId("switch.other"));
    Window opted_out(other_root, Size{100.0, 80.0});
    opted_out.set_invalidation_overlay(stxlib::Optional<bool>::of(false));

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
    opted_out.set_invalidation_overlay(stxlib::Optional<bool>::none());
    expect(opted_out.invalidation_overlay_enabled(), "a cleared setting follows the switch again");
    set_invalidation_overlay_default(false);
    expect(!following.invalidation_overlay_enabled() && !late.invalidation_overlay_enabled(),
           "turned off, every following window drops its overlay");
    set_invalidation_overlay_default(before);
}

} // namespace

void counters_card() {
    InvalidationOverlay overlay;
    std::vector<DevicePixelRect> regions;
    overlay.fill_regions(1.0, 320U, 200U, regions);
    expect(regions.empty(), "with no outline and no counters nothing is drawn");

    const std::string long_line(100U, '7');
    std::vector<std::string_view> lines(maximum_overlay_counter_lines + 3U, std::string_view{"paint 1"});
    lines[0] = long_line;
    overlay.set_counters(std::span<const std::string_view>(lines.data(), lines.size()));
    expect(overlay.counters().size() == maximum_overlay_counter_lines, "the card keeps at most its line count");
    expect(overlay.counters()[0].size() == maximum_overlay_counter_characters, "a long line is cut");
    expect(overlay.empty(), "counters are not outlines, so nothing fades");

    const std::array<std::string_view, 2> two{std::string_view{"renderer test"}, std::string_view{"paint 3"}};
    overlay.set_counters(std::span<const std::string_view>(two.data(), two.size()));
    expect(overlay.counters().size() == 2U && overlay.counters()[1] == "paint 3", "the lines are replaced");
    overlay.fill_regions(1.0, 320U, 200U, regions);
    expect(regions.size() == 1U && regions[0].left < 40 && regions[0].top < 40,
           "the counters card alone draws one region, at the top left (no legend)");

    Frame card(320U, 200U);
    overlay.compose(card.target(), 1.0, InvalidationClock::now(), DevicePixelRect{0, 0, 320, 200});
    bool lit = false;
    for (std::uint32_t y = static_cast<std::uint32_t>(regions[0].top);
         y < static_cast<std::uint32_t>(regions[0].bottom); ++y) {
        for (std::uint32_t x = static_cast<std::uint32_t>(regions[0].left);
             x < static_cast<std::uint32_t>(regions[0].right); ++x) {
            if (card.red(x, y) > 128) lit = true;
        }
    }
    expect(lit, "the counters card draws its text");
    expect(card.red(300U, 190U) == 0, "with no outline the legend is not drawn");

    overlay.set_counters(std::span<const std::string_view>{});
    overlay.fill_regions(1.0, 320U, 200U, regions);
    expect(regions.empty(), "no lines, no card");

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("counters.root"));
    Window window(root, Size{200.0, 120.0});
    window.perform_layout();
    window.set_invalidation_overlay(stxlib::Optional<bool>::of(true));
    NullPainter painter;
    window.paint(painter, {0.0, 0.0, 200.0, 120.0});
    InvalidationOverlay* const filled = window.invalidation_overlay();
    expect(filled != nullptr && (*filled).counters().size() >= 6U, "a window fills the counters card after a paint");
    expect((*filled).counters()[0].substr(0U, 8U) == "renderer", "the first line names the renderer");
    bool paints = false;
    for (const std::string& line : (*filled).counters()) {
        if (line.substr(0U, 6U) == "paint ") paints = true;
    }
    expect(paints, "a line counts paints and damage");
    window.set_invalidation_overlay(stxlib::Optional<bool>::none());
}

int main() {
    outline_store();
    composition();
    window_attribution();
    process_switch();
    counters_card();
    std::cout << "invalidation overlay tests passed\n";
    return EXIT_SUCCESS;
}
