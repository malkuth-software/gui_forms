// The window outer band (owner, 2026-11-08; the active border study,
// section 2.2, and the owner's answers): the root is measured against the
// client size less the band and arranged inside it, coordinates follow
// the inset, the band hits nothing (overlays included), lighting and
// flashing repaint the strips only and never lay out, the width snaps to
// whole device pixels, the band is drawn after every plane, the theme's
// `window` rule styles it, high contrast and reduced motion still it,
// full screen hides it on request, and its corners follow the host's
// report.

#include "../src/core/theme_language/compiler/theme_compiler.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/gui_forms.hpp"
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
    const std::optional<PaintReceipt> receipt = window.paint(painter, Rect{0.0, 0.0, client.width, client.height});
    if (receipt) static_cast<void>(window.notify_presented(*receipt));
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

    const Color red = Color::rgba(200, 0, 0);
    const Color grey = Color::rgba(90, 90, 90);
    WindowBandAppearance lit{};
    lit.state = WindowBandState::lit;
    lit.lit_colour = red;
    lit.rest_colour = grey;
    window.set_band_appearance(lit);
    require(window.band_snapshot().drawn == red, "lit, the band draws the lit colour");
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
    require(window.band_snapshot().flash_changes == 2U && window.band_snapshot().drawn == red,
            "a second half second on, lit again");
    window.perform_layout();
    require(window.metrics_snapshot().arrange_passes == arranges, "flashing lays nothing out");

    // Reduced motion stills the flash at its lit pose.
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    require(!window.band_snapshot().flashing && window.band_snapshot().drawn == red,
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
    require(window.band_snapshot().drawn == red, "the appearance set while undeclared shows");
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
    lit.lit_colour = band_colour;
    window.set_band_appearance(lit);
    ShapePainter painter{};
    const std::optional<PaintReceipt> receipt = window.paint(painter, Rect{0.0, 0.0, 120.0, 80.0});
    require(receipt.has_value(), "the window paints");
    static_cast<void>(window.notify_presented(*receipt));
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
    lit.lit_colour = band_colour;
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
        std::cout << "gui_forms_window_band_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_window_band_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
