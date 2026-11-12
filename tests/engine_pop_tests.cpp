// Pop as rule data (T2 plan, phase E9.5; owner E9-C, option 1: no new
// primitive). A pop is the design language's §9.3 item 1: a part rises one
// level, growing its shadow and brightening, on hover, selection or focus,
// within the overflow its control grants and never moving its neighbours.
// Here it is a theme's data alone:
// - the material primitive draws the face, from the theme's button recipe:
//   a gloss (E9.4) at rest, and a brighter gloss with a deeper shadow and a
//   glow when hot or selected;
// - a scale transition grows the part to 1.08 while hot, selected or
//   focus-visible;
// - a face cross-fade blends the two faces.
// The proof: the theme compiles with no new primitive; the hot face rises
// (its shadow deepens and it glows); while the pop moves nothing is baked,
// only the popped part is painted and it stays within its grant and its
// material's outsets, its neighbours untouched; at rest it is baked sharp
// once and the window's clock stops; selection pops it as hover does;
// reduced motion jumps to the end; and a part granted no overflow never
// grows.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/surface_material/presets/material_presets.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;
using std::chrono::milliseconds;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_pop_tests: " << message << '\n';
    std::exit(1);
}

// One part raster composited: where, and whether it was stretched.
struct RasterDraw final {
    std::shared_ptr<const PartRaster> raster{};
    Rect destination{};
    bool stretched{};
};

// A painter that keeps the part rasters composited, in window units, and
// draws nothing.
class RasterLog final : public Painter {
public:
    void save() override { saved.push_back(offset); }
    void restore() override {
        if (saved.empty()) return;
        offset = saved.back();
        saved.pop_back();
    }
    void translate(Point by) override { offset = Point{offset.x + by.x, offset.y + by.y}; }
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        if (!raster) return;
        const double scale = (*raster).scale().value;
        const Rect placed{origin.x + offset.x, origin.y + offset.y,
                          static_cast<double>((*raster).extent().width) / scale,
                          static_cast<double>((*raster).extent().height) / scale};
        draws.push_back(RasterDraw{raster, placed, false});
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double) override {
        if (!raster) return;
        const Rect placed{destination.x + offset.x, destination.y + offset.y, destination.width,
                          destination.height};
        draws.push_back(RasterDraw{raster, placed, true});
    }
    std::vector<RasterDraw> draws{};
    Point offset{};
    std::vector<Point> saved{};
};

// The motion clock's time, advanced by hand.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// A tile: a 60 × 24 button face 20 units in from its control's left edge
// and 28 down, hot under the pointer, selected when told.
// The tiles' parent paints nothing of its own, so the painter sees only the
// tiles' faces. (A Panel would now composite a face from the theme's rules.)
class Stage final : public Control {
public:
    explicit Stage(StableId stable_id) : Control(std::move(stable_id)) {}
};

class Tile final : public Control {
public:
    explicit Tile(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::enter) {
            hot = true;
            invalidate(Dirty::paint);
        }
        if (event.action == PointerAction::leave) {
            hot = false;
            invalidate(Dirty::paint);
        }
    }
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = face;
        part.key = PartKey{"face", {}, {}};
        part.granted_overflow = overflow;
        if (hot) part.states.insert(AnatomyState::hot);
        if (selected) part.states.insert(AnatomyState::selected);
        paint_part(painter, *this, part);
    }
    bool hot{};
    bool selected{};
    Rect face{20.0, 28.0, 60.0, 24.0};
    Insets overflow{4.0, 4.0, 4.0, 4.0};
};

// The popped scale: 1.08, within the grant (1 + 2 × 4 / 60 across).
constexpr double popped_scale = 1.08;

// The pop's scale machine: popped while hot, selected or focus-visible.
PropertyTransition pop_scale() {
    PropertyTransition transition{};
    transition.property = std::string(scale_property);
    const AnatomyState raising[] = {AnatomyState::hot, AnatomyState::selected, AnatomyState::focus_visible};
    for (const AnatomyState state : raising) {
        PresentationState raised{};
        raised.when.push_back(state);
        raised.target = popped_scale;
        transition.states.push_back(raised);
    }
    PresentationState rest{};
    rest.target = 1.0;
    transition.states.push_back(rest);
    transition.curve = MotionCurve::timed(milliseconds(120), MotionEasing::linear);
    return transition;
}

// The pop rule: the material face, its scale, and its face cross-fade.
ThemeRule pop_rule() {
    ThemeRule rule{};
    rule.label = "pop";
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.part = "face";
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    rule.transitions.push_back(pop_scale());
    PropertyTransition fade{};
    fade.property = std::string(face_property);
    fade.curve = MotionCurve::timed(milliseconds(120), MotionEasing::linear);
    rule.transitions.push_back(fade);
    return rule;
}

// The resting face: the gloss with a shallow shadow.
SurfaceMaterial resting_face() {
    SurfaceMaterial material = gloss_material(GlossColours{}, 4.0);
    material.shadows.push_back(MaterialShadow{Point{0.0, 1.0}, 1.0, 0.0, Color{20, 30, 42, 60}, false});
    return material;
}

// The raised face: a brighter gloss one level up, its shadow deeper, and
// a glow.
SurfaceMaterial raised_face() {
    GlossColours colours{};
    colours.top = Color{246, 250, 255, 255};
    colours.above_break = Color{216, 232, 250, 255};
    colours.below_break = Color{172, 204, 240, 255};
    colours.bottom = Color{204, 226, 250, 255};
    SurfaceMaterial material = gloss_material(colours, 4.0);
    material.shadows.push_back(MaterialShadow{Point{0.0, 3.0}, 3.0, 0.0, Color{20, 30, 42, 96}, false});
    MaterialGlow glow{};
    glow.color = Color{255, 255, 255, 120};
    glow.blur_radius = 1.5;
    glow.spread = 0.5;
    material.glows.push_back(glow);
    return material;
}

std::unique_ptr<ThemeDefinition> pop_theme() {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "engine-pop";
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    const std::size_t normal = static_cast<std::size_t>(ControlSurfaceState::normal);
    const std::size_t hot = static_cast<std::size_t>(ControlSurfaceState::hot);
    button.ordinary[normal].material = resting_face();
    button.ordinary[hot].material = raised_face();
    button.selected[normal].material = raised_face();
    button.selected[hot].material = raised_face();
    definition.rules.rules.push_back(pop_rule());
    return held;
}

struct Fixture final {
    ManualTime time{};
    std::shared_ptr<Stage> root;
    std::vector<std::shared_ptr<Tile>> tiles;
    std::unique_ptr<Window> window;
    RasterLog painter{};
};

// Three tiles side by side, 100 units apart; the middle one is granted
// `middle_overflow`.
std::unique_ptr<Fixture> make_fixture(bool reduced_motion, Insets middle_overflow) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Stage>(StableId("root"));
    const char* const names[] = {"left", "middle", "right"};
    for (std::size_t index = 0U; index < 3U; ++index) {
        std::shared_ptr<Tile> tile = make_control<Tile>(StableId(std::string(names[index])));
        (*tile).set_requested_bounds(Rect{static_cast<double>(index) * 100.0, 0.0, 100.0, 80.0});
        (*made.root).add_child(tile);
        made.tiles.push_back(tile);
    }
    (*made.tiles[1]).overflow = middle_overflow;
    made.window = std::make_unique<Window>(made.root, Size{300.0, 80.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.set_theme(Theme::create(*pop_theme()));
    require(window.theme().engine().rules().diagnostics().empty(), "the pop theme compiles cleanly");
    if (reduced_motion) {
        PresentationSettings settings = window.presentation_settings();
        settings.reduced_motion = true;
        window.set_presentation_settings(settings);
    }
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

void repaint(Fixture& fixture) {
    Window& window = *fixture.window;
    fixture.painter.draws.clear();
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
}

std::uint64_t bakes(const Window& window) {
    const std::uint64_t count = window.engine_parts().cache_statistics().bakes;
    return count;
}

void hover_middle(Fixture& fixture) {
    static_cast<void>((*fixture.window).dispatch_pointer({PointerAction::move, PointerButton::none,
                                                          Point{150.0, 40.0}}));
}

// The middle face's extent with its grant and its material's outsets of 8
// grown by the popped scale: everything the pop may paint.
constexpr Rect middle_reach{120.0 - 4.0 - 8.0 * popped_scale - 0.5, 28.0 - 4.0 - 8.0 * popped_scale - 0.5,
                            60.0 + 8.0 + 16.0 * popped_scale + 1.0, 24.0 + 8.0 + 16.0 * popped_scale + 1.0};

bool within(Rect inner, Rect outer) {
    const bool inside = inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
                        inner.bottom() <= outer.bottom();
    return inside;
}

std::uint32_t alpha_at(const PartRaster& raster, std::uint32_t column, std::uint32_t row) {
    const std::size_t offset = static_cast<std::size_t>(row) * raster.row_bytes() + column * 4U + 3U;
    const std::uint32_t alpha = raster.pixels()[offset];
    return alpha;
}

// Whether the frame drew `old_face` and `new_face` both stretched over the
// same rectangle: the cross-fade composing with the moving scale.
bool both_faces_stretched(const RasterLog& painter, const std::shared_ptr<const PartRaster>& old_face,
                          const std::shared_ptr<const PartRaster>& new_face) {
    for (const RasterDraw& first : painter.draws) {
        if (!first.stretched || first.raster != old_face) continue;
        for (const RasterDraw& second : painter.draws) {
            if (second.stretched && second.raster == new_face && second.destination == first.destination) return true;
        }
    }
    return false;
}

void pop_is_rule_data() {
    require(builtin_primitive_registry().primitives().size() == 12U, "the pop adds no primitive");
    const ThemeRule rule = pop_rule();
    require(rule.declarations.size() == 2U && rule.declarations[0].name == "material",
            "the pop's face is the material primitive's");
    for (const PropertyTransition& transition : rule.transitions) {
        require(transition.property == scale_property || transition.property == face_property,
                "the pop moves only the engine's presentational scale and face");
    }
}

void hover_pops_within_the_grant() {
    std::unique_ptr<Fixture> fixture = make_fixture(false, Insets{4.0, 4.0, 4.0, 4.0});
    Fixture& made = *fixture;
    Window& window = *made.window;
    require(made.painter.draws.size() == 3U, "each tile composites its face");
    const std::shared_ptr<const PartRaster> rest = made.painter.draws[1].raster;
    const std::uint64_t rested = bakes(window);

    // The change: the raised face is baked now, once.
    hover_middle(made);
    repaint(made);
    require(window.motion_clock().armed(), "the pop starts");
    const std::uint64_t changed = bakes(window);
    require(changed - rested <= 2U, "the change bakes the raised face and the face to stretch, at most");
    std::shared_ptr<const PartRaster> raised{};
    for (const RasterDraw& draw : made.painter.draws) {
        if (draw.raster != rest) raised = draw.raster;
    }
    require(raised != nullptr, "the raised face is a face of its own");

    // It rises one level: its shadow deepens below it, and it glows above.
    require((*raised).extent().width == 76U && (*raised).extent().height == 40U,
            "the raised face is drawn at its own size while it moves");
    require(alpha_at(*raised, 38U, 35U) > alpha_at(*rest, 38U, 35U), "its shadow deepens below it");
    require(alpha_at(*raised, 38U, 7U) > alpha_at(*rest, 38U, 7U), "and it glows above its top");

    std::size_t frames = 0U;
    std::size_t stretched = 0U;
    std::size_t blended = 0U;
    for (int step = 0; step < 40 && window.motion_clock().armed(); ++step) {
        made.time.time += milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(made.time.time));
        repaint(made);
        if (!window.motion_clock().armed()) break;
        require(bakes(window) == changed, "nothing is baked while the pop moves");
        for (const RasterDraw& draw : made.painter.draws) {
            require(within(draw.destination, middle_reach),
                    "only the popped part is painted, within its grant and outsets");
            if (!draw.stretched) continue;
            stretched = stretched + 1U;
            const double factor = draw.destination.width / 76.0;
            require(factor > 1.0 - 1e-9 && factor <= popped_scale + 1e-9, "the face grows towards its pop");
        }
        if (both_faces_stretched(made.painter, rest, raised)) blended = blended + 1U;
        frames = frames + 1U;
    }
    require(frames >= 3U && stretched >= 3U, "the pop spans frames, stretching the face");
    require(blended >= 1U, "mid-pop, the face cross-fade composites both faces stretched over one rectangle");
    require(!window.motion_clock().armed(), "the pop settles");

    // At rest it is baked sharp once at its popped size, and the clock stops.
    require(bakes(window) - changed <= 1U, "the rest bakes at most the sharp popped face");
    require(!made.painter.draws.empty() && !made.painter.draws.back().stretched, "at rest the face is not stretched");
    const PartRaster& sharp = *made.painter.draws.back().raster;
    require(sharp.extent().width > 76U, "the popped face is baked at its popped size");
    made.time.time += milliseconds(16);
    const FramePollResult idle = window.poll_frame_schedule(made.time.time);
    require(idle.motion_clock_ticks == 0U && window.take_damage().empty(), "settled, the window runs no frames");

    // The neighbours kept their places and their faces.
    require((*made.tiles[0]).absolute_bounds() == Rect{0.0, 0.0, 100.0, 80.0} &&
                (*made.tiles[2]).absolute_bounds() == Rect{200.0, 0.0, 100.0, 80.0},
            "the pop never moves its neighbours");
}

void selection_pops_as_hover_does() {
    std::unique_ptr<Fixture> fixture = make_fixture(false, Insets{4.0, 4.0, 4.0, 4.0});
    Fixture& made = *fixture;
    Window& window = *made.window;
    (*made.tiles[1]).selected = true;
    (*made.tiles[1]).invalidate(Dirty::paint);
    repaint(made);
    require(window.motion_clock().armed(), "selection starts the pop");
    for (int step = 0; step < 40 && window.motion_clock().armed(); ++step) {
        made.time.time += milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(made.time.time));
        repaint(made);
    }
    require(!window.motion_clock().armed(), "and it settles");
    require(!made.painter.draws.empty() && (*made.painter.draws.back().raster).extent().width > 76U,
            "the selected part rests popped");
}

void reduced_motion_jumps() {
    std::unique_ptr<Fixture> fixture = make_fixture(true, Insets{4.0, 4.0, 4.0, 4.0});
    Fixture& made = *fixture;
    Window& window = *made.window;
    hover_middle(made);
    repaint(made);
    require(!window.motion_clock().armed(), "under reduced motion the pop runs no frames");
    require(!made.painter.draws.empty(), "the part is painted at once");
    for (const RasterDraw& draw : made.painter.draws) {
        require(!draw.stretched, "and never stretched");
    }
    require((*made.painter.draws.back().raster).extent().width > 76U, "it jumps to its popped size");
}

void no_grant_never_grows() {
    std::unique_ptr<Fixture> fixture = make_fixture(false, Insets{});
    Fixture& made = *fixture;
    Window& window = *made.window;
    hover_middle(made);
    repaint(made);
    for (int step = 0; step < 40 && window.motion_clock().armed(); ++step) {
        made.time.time += milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(made.time.time));
        repaint(made);
        for (const RasterDraw& draw : made.painter.draws) {
            require(!draw.stretched, "a part granted no overflow is never stretched");
        }
    }
    require(!made.painter.draws.empty() && (*made.painter.draws.back().raster).extent().width == 76U,
            "and rests at its own size, raised");
}

} // namespace

int main() {
    pop_is_rule_data();
    hover_pops_within_the_grant();
    selection_pops_as_hover_does();
    reduced_motion_jumps();
    no_grant_never_grows();
    std::cout << "gui_forms_engine_pop_tests passed\n";
    return 0;
}
