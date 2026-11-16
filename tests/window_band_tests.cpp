// The window outer band (owner, 2026-11-08; the active border study,
// section 2.2, and the owner's answers): the root is measured against the
// client size less the band and arranged inside it, coordinates follow
// the inset, the band hits nothing (overlays included), lighting and
// flashing repaint the strips only and never lay out, the width snaps to
// whole device pixels, the band is drawn after every plane, the theme's
// `window` rule styles it, high contrast and reduced motion still it,
// full screen hides it on request, and its corners follow the host's
// report, including after the host reports a change of window state
// (HostWindowStateEvent: full screen, maximize, snap, restore). Phase 3
// (cp33): per-edge reservations, cycling and the limiter, colour roles,
// textures, and the host's shaping report (docs/user/window-band.md).

#include "../src/core/theme_language/compiler/theme_compiler.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/host/types/host_capability/host_capability.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/band/window_band.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
using theme_language::CompileOutcome;
using theme_language::DiagnosticCode;
using theme_language::SelectorVocabulary;
using theme_language::ThemeCompileInput;
using theme_language::ThemeCompiler;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeSource;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) noexcept {
    const bool close = std::fabs(left - right) < 1.0e-9;
    return close;
}

bool near(Rect left, Rect right) noexcept {
    const bool close =
        near(left.x, right.x) && near(left.y, right.y) && near(left.width, right.width) && near(left.height, right.height);
    return close;
}

// A panel that records the size the layout pass hands it.
class SlotProbe final : public Panel {
public:
    explicit SlotProbe(StableId stable_id) : Panel(std::move(stable_id)) {}

    Size measure(Size available) override {
        handed = available;
        const Size measured = Panel::measure(available);
        return measured;
    }

    Size handed{};
};

// The motion clock's time, set by the test. Declared before the window
// whose clock borrows it, so it outlives the installation.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// One drawing operation the window replayed, in order.
struct PaintedShape final {
    Rect rect{};
    Color colour{};
    bool rounded_stroke{};
    double radius{};
    double width{};
};

// Records the window's drawing operations, in replay order.
class ShapePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect rect, Color colour) override {
        PaintedShape shape{};
        shape.rect = rect;
        shape.colour = colour;
        shapes.push_back(shape);
    }
    void stroke_rect(Rect rect, Color colour, double width) override {
        PaintedShape shape{};
        shape.rect = rect;
        shape.colour = colour;
        shape.width = width;
        shapes.push_back(shape);
    }
    void stroke_rounded_rect(Rect rect, double radius, Color colour, double width) override {
        PaintedShape shape{};
        shape.rect = rect;
        shape.colour = colour;
        shape.rounded_stroke = true;
        shape.radius = radius;
        shape.width = width;
        shapes.push_back(shape);
    }
    void draw_line(Point from, Point to, Color colour, double width) override {
        PaintedShape shape{};
        shape.rect = Rect{from.x, from.y, to.x - from.x, to.y - from.y};
        shape.colour = colour;
        shape.width = width;
        shapes.push_back(shape);
    }
    // An image or text draws no colour the tests look for, but holds its
    // place in the order.
    void draw_image(ImageId, Rect destination, double) override {
        PaintedShape shape{};
        shape.rect = destination;
        shapes.push_back(shape);
    }
    void draw_text_utf8(Point origin, std::string_view, FontSpec, Color colour) override {
        PaintedShape shape{};
        shape.rect = Rect{origin.x, origin.y, 0.0, 0.0};
        shape.colour = colour;
        shapes.push_back(shape);
    }

    std::vector<PaintedShape> shapes;
};

// The node with `stable_id` among `nodes` and their descendants, or null.
const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* found = find_node(node.children, stable_id);
        if (found != nullptr) return found;
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

// Whether every damaged rectangle lies in the band `width` wide along the
// edges of `client`: none reaches into the content rectangle.
bool damage_only_in_band(const DamageRegion& damage, Size client, double width) {
    if (damage.empty()) return false;
    const Rect content{width, width, client.width - 2.0 * width, client.height - 2.0 * width};
    for (const Rect& rectangle : damage.rectangles()) {
        const Rect inside = Rect::intersection(rectangle, content);
        if (!inside.empty()) return false;
    }
    return true;
}

// Paints the whole client area and reports it presented, so that no
// earlier damage or paint mark is left pending.
void paint_all(Window& window) {
    ShapePainter painter{};
    const Size client = window.client_size();
    const stxlib::Optional<PaintReceipt> receipt = window.paint(painter, Rect{0.0, 0.0, client.width, client.height});
    if (receipt.present()) static_cast<void>(window.notify_presented(*receipt.value()));
    static_cast<void>(window.take_damage());
}

bool refused(Window& window, WindowBandOptions options) {
    const WindowBandSnapshot before = window.band_snapshot();
    bool thrown = false;
    try {
        window.declare_band(options);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    const WindowBandSnapshot after = window.band_snapshot();
    return thrown && after.declared == before.declared && after.options == before.options;
}

std::shared_ptr<const Theme> theme_of(std::string_view text, std::vector<ThemeDiagnostic>& diagnostics) {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeSource source{};
    source.file = "band.theme.css";
    source.text = text;
    const ThemeCompileInput input{.source = source,
                                  .registry = registry,
                                  .vocabulary = SelectorVocabulary{},
                                  .published_values = {},
                                  .resources = {},
                                  .images = {},
                                  .requires_engine = current_engine_version};
    ThemeCompiler compiler{};
    ThemeDiagnosticList list{};
    ThemeRuleSheet sheet{};
    const CompileOutcome outcome = compiler.compile(input, sheet, list);
    require(outcome == CompileOutcome::compiled, "the band sheet compiles");
    for (const ThemeDiagnostic& diagnostic : list.records()) diagnostics.push_back(diagnostic);
    const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).id = "band-theme";
    (*definition).rules = sheet;
    std::shared_ptr<const Theme> theme = Theme::create(*definition);
    return theme;
}

void test_layout_inset() {
    std::shared_ptr<SlotProbe> root = make_control<SlotProbe>(StableId("band.root"));
    std::shared_ptr<Button> child = make_control<Button>(StableId("band.child"));
    (*child).set_requested_bounds({0.0, 0.0, 20.0, 20.0});
    (*root).add_child(child);
    Window window(root, Size{200.0, 100.0});
    window.perform_layout();
    require(!window.band_snapshot().declared, "a window starts with no band");
    require(window.content_rect() == Rect{0.0, 0.0, 200.0, 100.0}, "no band: the content is the client area");

    window.declare_band(WindowBandOptions{4.0, WindowBandFullScreen::stays});
    window.perform_layout();
    const WindowBandSnapshot snapshot = window.band_snapshot();
    require(snapshot.declared && snapshot.reserved && near(snapshot.width, 4.0), "the band reserves its width");
    require(window.content_rect() == Rect{4.0, 4.0, 192.0, 92.0}, "the content rectangle is the client less the band");
    require(window.root_slot() == Rect{4.0, 4.0, 192.0, 92.0}, "the root slot is inset by the band");
    require((*root).committed_arranged_bounds() == Rect{4.0, 4.0, 192.0, 92.0}, "the root is arranged inside the band");
    require((*root).handed == Size{192.0, 92.0}, "the root is measured against the client less the band");

    // Coordinates derived from the root's arranged bounds follow the inset.
    require((*child).absolute_bounds() == Rect{4.0, 4.0, 20.0, 20.0}, "a child's absolute bounds follow the inset");
    require((*child).point_to_window(Point{1.0, 2.0}) == Point{5.0, 6.0}, "point_to_window follows the inset");
    require((*child).point_from_window(Point{5.0, 6.0}) == Point{1.0, 2.0}, "point_from_window follows the inset");
    const SemanticSnapshot semantics = window.semantic_snapshot();
    const SemanticNode* child_node = find_node(semantics.roots, "band.child");
    require(child_node != nullptr && (*child_node).bounds == Rect{4.0, 4.0, 20.0, 20.0},
            "the accessibility frame follows the inset");

    // The chrome insets are measured inside the band.
    window.set_chrome_insets(Insets{10.0, 0.0, 0.0, 0.0});
    window.perform_layout();
    require(window.root_slot() == Rect{14.0, 4.0, 182.0, 92.0}, "chrome insets lie inside the band");
    window.set_chrome_insets(Insets{});

    // A resize keeps the reservation, and its damage reaches the old band.
    static_cast<void>(window.take_damage());
    window.resize(Size{240.0, 100.0});
    const DamageRegion resized = window.take_damage();
    require(resized.bounds().contains(Rect{196.0, 0.0, 4.0, 100.0}), "a resize damages the old right band");
    require(resized.bounds().contains(Rect{236.0, 0.0, 4.0, 100.0}), "a resize damages the new right band");
    window.perform_layout();
    require((*root).committed_arranged_bounds() == Rect{4.0, 4.0, 232.0, 92.0}, "a resize keeps the band");

    window.undeclare_band();
    window.perform_layout();
    require(!window.band_snapshot().declared && !window.band_snapshot().reserved, "undeclared, nothing is reserved");
    require((*root).committed_arranged_bounds() == Rect{0.0, 0.0, 240.0, 100.0},
            "undeclaring lays the root out at the origin again");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    require(refused(window, WindowBandOptions{-1.0, WindowBandFullScreen::stays}), "a negative width is refused");
    require(refused(window, WindowBandOptions{nan, WindowBandFullScreen::stays}), "a NaN width is refused");
    require(refused(window, WindowBandOptions{33.0, WindowBandFullScreen::stays}), "a width past 32 is refused");
    require(refused(window, WindowBandOptions{1.0, static_cast<WindowBandFullScreen>(7)}),
            "an unknown full-screen choice is refused");
}

void test_band_hits_nothing() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("hit.root"));
    std::shared_ptr<Button> child = make_control<Button>(StableId("hit.child"));
    (*child).set_requested_bounds({0.0, 0.0, 20.0, 20.0});
    (*root).add_child(child);
    Window window(root, Size{200.0, 100.0});
    // An overlay covering the whole client area.
    std::shared_ptr<Button> cover = make_control<Button>(StableId("hit.cover"));
    (*cover).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    OverlayToken token = window.open_overlay(child, cover);
    window.perform_layout();
    require(window.hit_test(Point{2.0, 50.0}) == cover, "without a band the overlay is hit at the edge");

    window.declare_band(WindowBandOptions{4.0, WindowBandFullScreen::stays});
    require(window.hit_test(Point{2.0, 50.0}) == nullptr, "the left band hits nothing, overlays included");
    require(window.hit_test(Point{100.0, 98.0}) == nullptr, "the bottom band hits nothing");
    require(window.hit_test(Point{199.0, 0.0}) == nullptr, "a corner of the band hits nothing");
    require(window.hit_test(Point{100.0, 50.0}) == cover, "inside the content the overlay is hit");
    token.disconnect();
    window.perform_layout();
    require(window.hit_test(Point{5.0, 5.0}) == child, "inside the content the inset child is hit");
    require(window.hit_test(Point{3.0, 5.0}) == nullptr, "just outside the content, nothing");
}

void test_appearance_touches_paint_only() {
    ManualTime time{};
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("flash.root"));
    Window window(root, Size{200.0, 100.0});
    window.motion_clock().set_time_source(&time);
    window.declare_band(WindowBandOptions{2.0, WindowBandFullScreen::stays});
    window.perform_layout();
    paint_all(window);
    const Rect arranged = (*root).committed_arranged_bounds();
    const std::uint64_t arranges = window.metrics_snapshot().arrange_passes;
    const std::uint64_t measures = window.metrics_snapshot().measure_passes;

    // Blue: a saturated red never flashes (test_limiter).
    const Color blue = Color::rgba(0, 90, 200);
    const Color grey = Color::rgba(90, 90, 90);
    WindowBandAppearance lit{};
    lit.state = WindowBandState::lit;
    lit.lit_colour.set(blue);
    lit.rest_colour.set(grey);
    window.set_band_appearance(lit);
    require(window.band_snapshot().drawn == blue, "lit, the band draws the lit colour");
    const DamageRegion lit_damage = window.take_damage();
    require(damage_only_in_band(lit_damage, Size{200.0, 100.0}, 2.0), "lighting damages the band strips only");
    window.perform_layout();
    require(window.metrics_snapshot().arrange_passes == arranges && window.metrics_snapshot().measure_passes == measures,
            "lighting lays nothing out");
    require((*root).committed_arranged_bounds() == arranged, "lighting moves no content");

    // Flashing: the colour changes every half second, the strips only.
    WindowBandAppearance flashing = lit;
    flashing.state = WindowBandState::flashing;
    window.set_band_appearance(flashing);
    require(window.band_snapshot().flashing, "a flash runs");
    static_cast<void>(window.take_damage());
    time.time += std::chrono::milliseconds(400);
    static_cast<void>(window.poll_frame_schedule(time.time));
    require(window.band_snapshot().flash_changes == 0U, "no change before half a second");
    time.time += std::chrono::milliseconds(200);
    static_cast<void>(window.poll_frame_schedule(time.time));
    require(window.band_snapshot().flash_changes == 1U && window.band_snapshot().drawn == grey,
            "half a second on, the band shows its rest colour");
    require(damage_only_in_band(window.take_damage(), Size{200.0, 100.0}, 2.0), "a flash damages the strips only");
    time.time += std::chrono::milliseconds(500);
    static_cast<void>(window.poll_frame_schedule(time.time));
    require(window.band_snapshot().flash_changes == 2U && window.band_snapshot().drawn == blue,
            "a second half second on, lit again");
    window.perform_layout();
    require(window.metrics_snapshot().arrange_passes == arranges, "flashing lays nothing out");

    // Reduced motion stills the flash at its lit pose.
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    require(!window.band_snapshot().flashing && window.band_snapshot().drawn == blue,
            "reduced motion stills the flash, lit");
    time.time += std::chrono::milliseconds(2000);
    static_cast<void>(window.poll_frame_schedule(time.time));
    require(window.band_snapshot().flash_changes == 2U, "a stilled flash changes nothing");
    settings.reduced_motion = false;
    window.set_presentation_settings(settings);
    require(window.band_snapshot().flashing, "the flash runs again after");

    // Resting shows the rest colour; an appearance set while undeclared is
    // kept and shown once declared.
    WindowBandAppearance rest = lit;
    rest.state = WindowBandState::rest;
    window.set_band_appearance(rest);
    require(!window.band_snapshot().flashing && window.band_snapshot().drawn == grey, "at rest, the rest colour");
    window.undeclare_band();
    window.set_band_appearance(lit);
    window.declare_band(WindowBandOptions{2.0, WindowBandFullScreen::stays});
    require(window.band_snapshot().drawn == blue, "the appearance set while undeclared shows");
    window.motion_clock().set_time_source(nullptr);
}

void test_device_pixel_snapping() {
    require(near(snapped_band_width(1.0, 1.0), 1.0), "1 at scale 1 is one pixel");
    require(near(snapped_band_width(1.0, 1.25), 0.8), "1 at 1.25 rounds to one device pixel");
    require(near(snapped_band_width(1.0, 1.5), 2.0 / 1.5), "1 at 1.5 rounds to two device pixels");
    require(near(snapped_band_width(0.1, 1.0), 1.0), "a thin band is at least one device pixel");
    require(near(snapped_band_width(3.0, 2.0), 3.0), "3 at scale 2 is six device pixels");
    require(snapped_band_width(0.0, 1.0) == 0.0, "no width snaps to none");
    require(snapped_band_width(1.0, 0.0) == 0.0, "no scale snaps to none");

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("dpi.root"));
    Window window(root, Size{200.0, 100.0});
    window.declare_band(WindowBandOptions{1.0, WindowBandFullScreen::stays});
    window.set_scale(1.5);
    window.perform_layout();
    const double width = 2.0 / 1.5;
    require(near(window.band_snapshot().width, width), "the band snaps at the new scale");
    require(near((*root).committed_arranged_bounds(), Rect{width, width, 200.0 - 2.0 * width, 100.0 - 2.0 * width}),
            "the root lands on the device grid inside the snapped band");
    window.set_scale(1.25);
    window.perform_layout();
    require(near(window.band_snapshot().width, 0.8), "and again at 1.25");
}

void test_drawn_above_every_plane() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("paint.root"));
    Window window(root, Size{120.0, 80.0});
    std::shared_ptr<Button> cover = make_control<Button>(StableId("paint.cover"));
    (*cover).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    OverlayToken token = window.open_overlay(root, cover, OverlayOptions{});
    const Color band_colour = Color::rgba(1, 2, 3);
    window.declare_band(WindowBandOptions{3.0, WindowBandFullScreen::stays});
    WindowBandAppearance lit{};
    lit.state = WindowBandState::lit;
    lit.lit_colour.set(band_colour);
    window.set_band_appearance(lit);
    ShapePainter painter{};
    const stxlib::Optional<PaintReceipt> receipt = window.paint(painter, Rect{0.0, 0.0, 120.0, 80.0});
    require(receipt.present(), "the window paints");
    static_cast<void>(window.notify_presented(*receipt.value()));
    std::size_t first_band = painter.shapes.size();
    std::size_t last_other = 0U;
    std::size_t band_fills = 0U;
    for (std::size_t index = 0U; index < painter.shapes.size(); ++index) {
        const PaintedShape& shape = painter.shapes[index];
        if (shape.colour == band_colour) {
            if (first_band == painter.shapes.size()) first_band = index;
            ++band_fills;
        } else {
            last_other = index;
        }
    }
    require(band_fills == 4U, "the band draws its four strips");
    // Nothing else is drawn once the band starts.
    const bool only_band = band_fills == painter.shapes.size();
    const bool others_first = last_other < first_band || only_band;
    require(first_band < painter.shapes.size() && others_first,
            "the band is drawn after everything else, overlays included");

    // At rest with no rest colour the band draws nothing: the window's base
    // shows in the reserved space.
    WindowBandAppearance rest{};
    window.set_band_appearance(rest);
    ShapePainter resting{};
    static_cast<void>(window.paint(resting, Rect{0.0, 0.0, 120.0, 80.0}));
    for (const PaintedShape& shape : resting.shapes) {
        require(!(shape.colour == band_colour), "a resting band with no rest colour draws nothing");
    }
    token.disconnect();
}

void test_theme_band() {
    std::vector<ThemeDiagnostic> diagnostics{};
    const std::shared_ptr<const Theme> theme =
        theme_of("window { band-width: 3px; band-color: #102030; band-lit-color: #405060; band-effect: flash }\n"
                 "#pad { band-width: 2px }\n",
                 diagnostics);
    bool warned = false;
    for (const ThemeDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == DiagnosticCode::band_not_on_window) warned = true;
    }
    require(warned, "a band property off a window rule warns (band_not_on_window)");

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("theme.root"));
    Window window(root, Size{200.0, 100.0});
    window.set_theme(theme);
    window.declare_band(WindowBandOptions{});
    require(near(window.band_snapshot().width, 3.0), "the theme's band-width when the window asks for none");
    require(window.band_snapshot().drawn == Color::rgba(0x10, 0x20, 0x30), "at rest, the theme's band-color");
    WindowBandAppearance lit{};
    lit.state = WindowBandState::lit;
    window.set_band_appearance(lit);
    require(window.band_snapshot().flashing, "band-effect: flash makes a lit band flash");
    require(window.band_snapshot().drawn == Color::rgba(0x40, 0x50, 0x60), "lit, the theme's band-lit-color");

    // The window's own width wins over the theme's.
    window.declare_band(WindowBandOptions{5.0, WindowBandFullScreen::stays});
    require(near(window.band_snapshot().width, 5.0), "the window's width wins");

    // High contrast: the palette's focus colour, solid, no flash.
    PresentationSettings settings = window.presentation_settings();
    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    ControlVisualContext context{};
    context.high_contrast = true;
    const ControlVisualRecipe& contrast = window.theme().resolve(ControlVisualRole::window, context);
    require(!window.band_snapshot().flashing, "high contrast stills the band");
    require(window.band_snapshot().drawn == contrast.focus_ring, "high contrast draws the palette's focus colour");
    require(window.band_snapshot().width >= contrast.focus_width, "at least the palette's focus width");
}

void test_full_screen() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("full.root"));
    Window window(root, Size{200.0, 100.0});
    window.declare_band(WindowBandOptions{2.0, WindowBandFullScreen::hides});
    window.perform_layout();
    window.report_full_screen(true);
    window.perform_layout();
    require(window.band_snapshot().declared && !window.band_snapshot().reserved, "hides: no reservation in full screen");
    require((*root).committed_arranged_bounds() == Rect{0.0, 0.0, 200.0, 100.0}, "the root fills the screen");
    window.report_full_screen(false);
    window.perform_layout();
    require((*root).committed_arranged_bounds() == Rect{2.0, 2.0, 196.0, 96.0}, "the band returns after");

    window.declare_band(WindowBandOptions{2.0, WindowBandFullScreen::stays});
    window.report_full_screen(true);
    window.perform_layout();
    require(window.band_snapshot().reserved, "stays: the band keeps its reservation in full screen");
}

void test_host_corners() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("corner.root"));
    Window window(root, Size{120.0, 90.0});
    window.set_corner_preference(WindowCornerPreference::round);
    require(window.band_snapshot().corners.shape == WindowCornerShape::unknown,
            "with no host, the corners are unknown and the band draws square");

    host::HeadlessHost host(window);
    require(host.dispatch(HostAttachEvent{{120.0, 90.0}, 1.0}, 1).accepted(), "attach refused");
    host::HeadlessHostServices& services = host.headless_services();
    require(services.native_corner_preference() == WindowCornerPreference::round,
            "attaching sends the window's preference to the host");
    WindowCornerReport rounded{};
    rounded.shape = WindowCornerShape::rounded;
    rounded.radius = 8.0;
    rounded.complied = true;
    services.set_corner_report(WindowCornerPreference::system, rounded);
    window.set_corner_preference(WindowCornerPreference::system);
    require(window.band_snapshot().corners == rounded, "the band follows the host's reported corners");

    const Color band_colour = Color::rgba(4, 5, 6);
    window.declare_band(WindowBandOptions{2.0, WindowBandFullScreen::stays});
    WindowBandAppearance lit{};
    lit.state = WindowBandState::lit;
    lit.lit_colour.set(band_colour);
    window.set_band_appearance(lit);
    ShapePainter painter{};
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 120.0, 90.0}));
    bool stroked = false;
    for (const PaintedShape& shape : painter.shapes) {
        if (shape.colour == band_colour && shape.rounded_stroke && near(shape.width, 2.0) && near(shape.radius, 7.0)) {
            stroked = true;
        }
    }
    require(stroked, "a rounded band is one stroke along the window's outline");

    // The opt-out: square corners where the host complies.
    WindowCornerReport square{};
    square.shape = WindowCornerShape::square;
    square.complied = true;
    services.set_corner_report(WindowCornerPreference::square, square);
    window.set_corner_preference(WindowCornerPreference::square);
    require(window.band_snapshot().corners == square, "square corners on request");
    require(window.corner_preference() == WindowCornerPreference::square, "the preference is kept");
}

// The host's window-state report: full screen reaches the band, and the
// corners are asked for again, so a host that squares a maximized or
// snapped window's corners squares the band's with them.
void test_window_state_reports() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("state.root"));
    Window window(root, Size{120.0, 90.0});
    window.declare_band(WindowBandOptions{2.0, WindowBandFullScreen::hides});
    host::HeadlessHost host(window);
    require(host.dispatch(HostAttachEvent{{120.0, 90.0}, 1.0}, 1).accepted(), "attach refused");
    require(host::headless_capabilities().supports(HostCapability::window_state_reports),
            "the reference host reports window state");
    host::HeadlessHostServices& services = host.headless_services();
    WindowCornerReport rounded{};
    rounded.shape = WindowCornerShape::rounded;
    rounded.radius = 8.0;
    rounded.complied = true;
    services.set_corner_report(WindowCornerPreference::system, rounded);
    require(host.dispatch(HostWindowStateEvent{false}, 2).accepted(), "a window state report is accepted");
    require(window.band_snapshot().corners == rounded, "the report asks the host for the corners again");
    window.perform_layout();
    require((*root).committed_arranged_bounds() == Rect{2.0, 2.0, 116.0, 86.0}, "the band is reserved");

    // Maximized: the simulated platform squares the corners.
    WindowCornerReport squared{};
    squared.shape = WindowCornerShape::square;
    squared.complied = true;
    services.set_corner_report(WindowCornerPreference::system, squared);
    require(host.dispatch(HostWindowStateEvent{false}, 3).accepted(), "a second report is accepted");
    require(window.band_snapshot().corners == squared, "a squared window squares the band's corners");
    require(window.band_snapshot().reserved, "not full screen: the band stays");

    // Full screen: a band that hides gives up its reservation.
    require(host.dispatch(HostWindowStateEvent{true}, 4).accepted(), "a full-screen report is accepted");
    window.perform_layout();
    require(!window.band_snapshot().reserved, "full screen hides the band");
    require((*root).committed_arranged_bounds() == Rect{0.0, 0.0, 120.0, 90.0}, "the root fills the screen");

    // Restored.
    services.set_corner_report(WindowCornerPreference::system, rounded);
    require(host.dispatch(HostWindowStateEvent{false}, 5).accepted(), "a restore report is accepted");
    window.perform_layout();
    require(window.band_snapshot().reserved && window.band_snapshot().corners == rounded,
            "restored: the band and its rounded corners return");
    require((*root).committed_arranged_bounds() == Rect{2.0, 2.0, 116.0, 86.0}, "the root is inset again");
    require(host_event_name(HostEventPayload{HostWindowStateEvent{}}) == std::string("window_state"),
            "the event is named window_state");
    require(window.content_rect() == Rect{2.0, 2.0, 116.0, 86.0},
            "the content rectangle, which accessibility roots cover, leaves the band out");
}

// Per-edge reservations: each edge keeps its own snapped width, the
// content and the root follow it, an edge of 0 reserves nothing, and only
// declaring or undeclaring lays out; appearance changes never do.
void test_per_edge_reservation() {
    std::shared_ptr<SlotProbe> root = make_control<SlotProbe>(StableId("edge.root"));
    std::shared_ptr<Button> child = make_control<Button>(StableId("edge.child"));
    (*child).set_requested_bounds({0.0, 0.0, 20.0, 20.0});
    (*root).add_child(child);
    Window window(root, Size{200.0, 100.0});
    WindowBandOptions options{};
    options.edges = WindowBandEdges{0.0, 6.0, 3.0, 2.0};
    window.declare_band(options);
    window.perform_layout();
    const WindowBandSnapshot declared = window.band_snapshot();
    require(declared.reserved && declared.edges == Insets{0.0, 6.0, 3.0, 2.0}, "each edge reserves its own width");
    require(near(declared.width, 6.0), "the snapshot's width is the widest edge");
    require(window.content_rect() == Rect{0.0, 6.0, 197.0, 92.0}, "the content leaves each edge's width out");
    require((*root).committed_arranged_bounds() == Rect{0.0, 6.0, 197.0, 92.0}, "the root is arranged inside");
    require((*root).handed == Size{197.0, 92.0}, "the root is measured against the client less the edges");
    paint_all(window);

    // Toggling the appearance, through every state and a texture, keeps
    // the reservation: no measure, no arrange, no move.
    const std::uint64_t arranges = window.metrics_snapshot().arrange_passes;
    const std::uint64_t measures = window.metrics_snapshot().measure_passes;
    WindowBandAppearance appearance{};
    appearance.lit_colour.set(Color::rgba(0, 120, 60));
    const WindowBandState states[] = {WindowBandState::lit, WindowBandState::flashing, WindowBandState::cycling,
                                      WindowBandState::rest, WindowBandState::lit};
    for (const WindowBandState state : states) {
        appearance.state = state;
        window.set_band_appearance(appearance);
        window.perform_layout();
    }
    WindowBandTexture texture{};
    texture.procedural.pattern = ProceduralPattern::pinstripe;
    appearance.texture = texture;
    window.set_band_appearance(appearance);
    window.perform_layout();
    appearance.texture.reset();
    window.set_band_appearance(appearance);
    window.perform_layout();
    require(window.metrics_snapshot().arrange_passes == arranges && window.metrics_snapshot().measure_passes == measures,
            "appearance toggles lay nothing out");
    require(window.band_snapshot().edges == Insets{0.0, 6.0, 3.0, 2.0}, "the reservation is fixed across toggles");
    require((*root).committed_arranged_bounds() == Rect{0.0, 6.0, 197.0, 92.0}, "no content moves");

    // Re-declaring the same options changes nothing; undeclaring lays out.
    window.declare_band(options);
    window.perform_layout();
    require(window.metrics_snapshot().arrange_passes == arranges, "the same declaration lays nothing out");
    window.undeclare_band();
    window.perform_layout();
    require(window.metrics_snapshot().arrange_passes > arranges, "undeclaring lays the window out");
    require((*root).committed_arranged_bounds() == Rect{0.0, 0.0, 200.0, 100.0}, "and the root fills the client");

    // A per-edge band hits nothing on its reserved edges only.
    window.declare_band(options);
    window.perform_layout();
    require(window.hit_test(Point{100.0, 3.0}) == nullptr, "the top edge hits nothing");
    require(window.hit_test(Point{198.5, 50.0}) == nullptr, "the right edge hits nothing");
    require(window.hit_test(Point{100.0, 99.0}) == nullptr, "the bottom edge hits nothing");
    require(window.hit_test(Point{0.5, 10.0}) == child, "the left edge, of width 0, is content");

    // Refusals leave the declaration as it was.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    WindowBandOptions bad = options;
    bad.edges = WindowBandEdges{0.0, -1.0, 0.0, 0.0};
    require(refused(window, bad), "a negative edge is refused");
    bad.edges = WindowBandEdges{nan, 1.0, 0.0, 0.0};
    require(refused(window, bad), "a NaN edge is refused");
    bad.edges = WindowBandEdges{0.0, 0.0, 33.0, 0.0};
    require(refused(window, bad), "an edge past 32 is refused");

    // Full screen: hides gives up every edge, stays keeps them.
    WindowBandOptions hiding = options;
    hiding.full_screen = WindowBandFullScreen::hides;
    window.declare_band(hiding);
    window.report_full_screen(true);
    window.perform_layout();
    require(!window.band_snapshot().reserved && window.content_rect() == Rect{0.0, 0.0, 200.0, 100.0},
            "hides: no edge is reserved in full screen");
    window.declare_band(options);
    window.perform_layout();
    require(window.band_snapshot().edges == Insets{0.0, 6.0, 3.0, 2.0}, "stays: every edge is kept in full screen");
    require(window.shaping_report().full_screen, "the shaping report says full screen");
}

// Each edge snaps to whole device pixels on its own.
void test_per_edge_snapping() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("edge.dpi.root"));
    Window window(root, Size{200.0, 100.0});
    WindowBandOptions options{};
    options.edges = WindowBandEdges{1.0, 0.0, 2.0, 0.3};
    window.declare_band(options);
    window.set_scale(1.25);
    window.perform_layout();
    // At 1.25: 1 is one device pixel (0.8), 2 is 2.5 pixels, rounded to
    // three (2.4), and 0.3 is at least one pixel (0.8).
    const Insets at_125 = window.band_snapshot().edges;
    require(near(at_125.left, 0.8) && at_125.top == 0.0 && near(at_125.right, 2.4) && near(at_125.bottom, 0.8),
            "each edge snaps at 1.25");
    window.set_scale(1.5);
    window.perform_layout();
    // At 1.5: 1.5 pixels rounds to two (4/3), 3 pixels (2), 0.45 to one.
    const Insets at_15 = window.band_snapshot().edges;
    require(near(at_15.left, 2.0 / 1.5) && at_15.top == 0.0 && near(at_15.right, 2.0) && near(at_15.bottom, 1.0 / 1.5),
            "each edge snaps at 1.5");
    const Rect arranged = (*root).committed_arranged_bounds();
    require(near(arranged, Rect{2.0 / 1.5, 0.0, 200.0 - 2.0 / 1.5 - 2.0, 100.0 - 1.0 / 1.5}),
            "the root lands on the device grid inside the snapped edges");
}

// The engine limiter: a saturated red never flashes, a cycle blends at
// most once a second, and reduced motion stills a cycle lit.
void test_limiter_and_cycling() {
    ManualTime time{};
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("cycle.root"));
    Window window(root, Size{200.0, 100.0});
    window.motion_clock().set_time_source(&time);
    window.declare_band(WindowBandOptions{2.0, WindowBandFullScreen::stays});
    window.perform_layout();
    paint_all(window);

    WindowBandAppearance red{};
    red.state = WindowBandState::flashing;
    red.lit_colour.set(Color::rgba(220, 10, 10));
    red.rest_colour.set(Color::rgba(90, 90, 90));
    window.set_band_appearance(red);
    require(!window.band_snapshot().flashing && window.band_snapshot().drawn == Color::rgba(220, 10, 10),
            "a saturated red never flashes: it holds lit");

    const Color lit = Color::rgba(0, 90, 200);
    const Color rest = Color::rgba(200, 200, 200);
    WindowBandAppearance cycling{};
    cycling.state = WindowBandState::cycling;
    cycling.lit_colour.set(lit);
    cycling.rest_colour.set(rest);
    cycling.frequency = 4.0;
    window.set_band_appearance(cycling);
    const WindowBandSnapshot started = window.band_snapshot();
    require(started.cycling && !started.flashing, "a cycle runs");
    require(near(started.frequency, maximum_band_frequency), "the limiter lowers the frequency to one a second");
    require(started.drawn == lit, "a cycle starts lit");
    static_cast<void>(window.take_damage());
    for (int frame = 0; frame < 15; ++frame) {
        time.time += std::chrono::milliseconds(34);
        static_cast<void>(window.poll_frame_schedule(time.time));
    }
    const WindowBandSnapshot halfway = window.band_snapshot();
    require(halfway.cycle_frames > 0U, "cycle frames are drawn");
    require(!(halfway.drawn == lit), "half a second on, the cycle has moved off its lit colour");
    require(damage_only_in_band(window.take_damage(), Size{200.0, 100.0}, 2.0), "a cycle damages the strips only");

    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    require(!window.band_snapshot().cycling && window.band_snapshot().drawn == lit, "reduced motion stills it, lit");
    const std::uint64_t frames = window.band_snapshot().cycle_frames;
    time.time += std::chrono::milliseconds(1000);
    static_cast<void>(window.poll_frame_schedule(time.time));
    require(window.band_snapshot().cycle_frames == frames, "a stilled cycle draws no frames");
    settings.reduced_motion = false;
    window.set_presentation_settings(settings);
    require(window.band_snapshot().cycling, "the cycle runs again after");

    // A frequency of 0 holds the band lit.
    cycling.frequency = 0.0;
    window.set_band_appearance(cycling);
    require(!window.band_snapshot().cycling && window.band_snapshot().drawn == lit, "frequency 0 holds lit");

    WindowBandAppearance bad = cycling;
    bad.frequency = -1.0;
    bool thrown = false;
    try {
        window.set_band_appearance(bad);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    require(thrown && window.band_snapshot().appearance == cycling, "a negative frequency is refused, nothing changed");
    window.motion_clock().set_time_source(nullptr);
}

// Colour roles and textures: a role lights the band in a theme colour
// when no lit colour is given; a texture draws beneath the colour, one
// surface per strip; the glint and a bad tile are refused.
void test_roles_and_texture() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("role.root"));
    Window window(root, Size{120.0, 80.0});
    window.declare_band(WindowBandOptions{3.0, WindowBandFullScreen::stays});
    const BasicControlStyle& style = window.theme().basic_style();
    WindowBandAppearance role{};
    role.state = WindowBandState::lit;
    role.lit_role = WindowBandColourRole::accent;
    window.set_band_appearance(role);
    require(window.band_snapshot().drawn == style.accent, "the accent role lights in the theme's accent");
    role.lit_role = WindowBandColourRole::text;
    window.set_band_appearance(role);
    require(window.band_snapshot().drawn == style.text, "the text role lights in the theme's text colour");
    role.lit_colour.set(Color::rgba(7, 8, 9));
    window.set_band_appearance(role);
    require(window.band_snapshot().drawn == Color::rgba(7, 8, 9), "a lit colour wins over the role");

    WindowBandTexture texture{};
    texture.source = WindowBandTextureSource::finish;
    texture.finish.kind = FinishKind::orange_peel;
    role.texture = texture;
    window.set_band_appearance(role);
    require(window.band_snapshot().textured, "the band reports its texture");
    ShapePainter painter{};
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 120.0, 80.0}));
    std::size_t colour_fills = 0U;
    for (const PaintedShape& shape : painter.shapes) {
        if (shape.colour == Color::rgba(7, 8, 9)) ++colour_fills;
    }
    require(colour_fills == 4U, "the colour is drawn over each textured strip");

    WindowBandAppearance glint = role;
    WindowBandTexture glinting = texture;
    glinting.finish.kind = FinishKind::glint;
    glint.texture = glinting;
    require(!window_band_appearance_valid(glint), "the glint is refused over the band");
    WindowBandTexture tiled{};
    tiled.procedural.tile = 0.0;
    require(!window_band_texture_valid(tiled), "a texture with no tile is refused");
    WindowBandTexture faint{};
    faint.opacity = 1.5;
    require(!window_band_texture_valid(faint), "an opacity past 1 is refused");
    bool thrown = false;
    try {
        window.set_band_appearance(glint);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    require(thrown && window.band_snapshot().appearance == role, "a refused texture changes nothing");

    // High contrast draws solid and untextured.
    PresentationSettings settings = window.presentation_settings();
    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    require(!window.band_snapshot().textured, "high contrast draws no texture");
}

// What the attached host lets the band rely on.
void test_shaping_report() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("shape.root"));
    Window window(root, Size{120.0, 90.0});
    window.set_corner_preference(WindowCornerPreference::square);
    const WindowShapingReport alone = window.shaping_report();
    require(!alone.host_attached && !alone.corners_reported && !alone.state_reported && !alone.native_border,
            "with no host, nothing is reported");
    require(alone.preference == WindowCornerPreference::square, "the preference is in the report");

    host::HeadlessHost host(window);
    require(host.dispatch(HostAttachEvent{{120.0, 90.0}, 1.0}, 1).accepted(), "attach refused");
    WindowCornerReport square{};
    square.shape = WindowCornerShape::square;
    square.complied = true;
    host.headless_services().set_corner_report(WindowCornerPreference::square, square);
    window.set_corner_preference(WindowCornerPreference::system);
    window.set_corner_preference(WindowCornerPreference::square);
    const WindowShapingReport attached = window.shaping_report();
    require(attached.host_attached && attached.corners_reported && attached.state_reported,
            "the reference host reports corners and window state");
    require(attached.square_corners && attached.rounded_corners, "the reference host may report either shape");
    require(!attached.native_border, "no host draws the band natively yet");
    require(attached.corners == square && !attached.full_screen, "the corners now, and not full screen");
    require(host::headless_capabilities().window_corner_shapes == WindowCornerShapes{true, true},
            "the capabilities carry the shapes");
}

void test_capability_names() {
    const std::string json = host::headless_capabilities().to_json();
    require(json.find("\"window_corners\"") != std::string::npos &&
                json.find("\"window_state_reports\"") != std::string::npos &&
                json.find("\"transient_surfaces\"") != std::string::npos,
            "the capability report names bits 32 to 35");
    require(json.find("\"window_corner_shapes\":{\"square\":true,\"rounded\":true}") != std::string::npos,
            "the capability report names the corner shapes");
}

} // namespace

int main() {
    try {
        test_layout_inset();
        test_band_hits_nothing();
        test_appearance_touches_paint_only();
        test_device_pixel_snapping();
        test_drawn_above_every_plane();
        test_theme_band();
        test_full_screen();
        test_host_corners();
        test_window_state_reports();
        test_per_edge_reservation();
        test_per_edge_snapping();
        test_limiter_and_cycling();
        test_roles_and_texture();
        test_shaping_report();
        test_capability_names();
        std::cout << "gui_forms_window_band_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_window_band_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
