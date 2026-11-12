// The presentational scale and text slots (T2 plan, phase E8; GIVEN D5-1
// and T1's hand-off): a scale is a transitionable engine property, checked
// at load; a scaled part is drawn at its scaled size about its centre, only
// as far as its control grants overflow, and never grows with none granted;
// its hit region and layout do not change; the built-in rules never scale.
// A part's text slot lies inside the shape its primitive draws at every
// display scale and text scale from 1 to 3, shrinking to the centre where
// the insets leave no room.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/part_shape/part_shape.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "support/theme_definition_heap.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;
using std::chrono::milliseconds;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_scale_slot_tests: " << message << '\n';
    std::exit(1);
}

class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// A painter that keeps where and how large the last part raster was
// composited, and draws nothing.
class RasterWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        last_origin = origin;
        last_extent = (*raster).extent();
        rasters = rasters + 1U;
        stretched = false;
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double) override {
        last_destination = destination;
        last_extent = (*raster).extent();
        scaled_rasters = scaled_rasters + 1U;
        stretched = true;
    }
    Point last_origin{};
    DeviceExtent last_extent{};
    std::size_t rasters{};
    // The last scaled composite's destination, how many there were, and
    // whether the last composite was scaled.
    Rect last_destination{};
    std::size_t scaled_rasters{};
    bool stretched{};
};

// A control showing one keyed face, hot while the pointer is over it.
class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
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
    void on_paint(Painter& painter, Rect) override { paint_part(painter, *this, face()); }
    [[nodiscard]] PartPaint face() const {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{20.0, 20.0, 40.0, 24.0};
        part.key = PartKey{"face", {}, {}};
        part.granted_overflow = overflow;
        if (hot) part.states.insert(AnatomyState::hot);
        return part;
    }
    bool hot{};
    Insets overflow{};
};

// The pad's face grows to `hot_scale` while hot, linearly over 100 ms.
PropertyTransition grow(double hot_scale) {
    PropertyTransition transition{};
    transition.property = std::string(scale_property);
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = hot_scale;
    PresentationState rest{};
    rest.target = 1.0;
    transition.states.push_back(hot);
    transition.states.push_back(rest);
    transition.curve = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    return transition;
}

ThemeRule pad_rule(std::string id, std::string_view primitive, double hot_scale) {
    ThemeRule rule{};
    rule.label = id;
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::move(id);
    rule.selector.part = "face";
    if (!primitive.empty()) rule.declarations.push_back(RuleDeclaration::primitive(primitive));
    rule.transitions.push_back(grow(hot_scale));
    return rule;
}

void scale_is_checked_at_load() {
    ThemeRuleSheet sheet{};
    sheet.rules.push_back(pad_rule("too_far", "pill_plate", 3.0));
    sheet.rules.push_back(pad_rule("too_small", "pill_plate", 0.25));
    sheet.rules.push_back(pad_rule("fine", "pill_plate", 1.5));
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    std::size_t invalid = 0U;
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == RuleDiagnosticKind::transition_invalid && diagnostic.property == scale_property) {
            invalid = invalid + 1U;
        }
    }
    require(invalid == 2U, "a scale outside 0.5 to 2 is refused at load");
    CompiledThemeRules::PartFacts facts{};
    facts.stable_id = "fine";
    facts.part = "face";
    facts.role = PartRole::face;
    const CompiledThemeRules::Resolution resolution = rules.resolve(facts);
    require(resolution.transition_count == 1U &&
                (*resolution.transitions[0]).property == PresentationProperty::scale,
            "a scale transition resolves as the engine property");

    for (const ThemeRule& rule : built_in_theme_rules().rules) {
        for (const PropertyTransition& transition : rule.transitions) {
            require(transition.property != scale_property, "the built-in rules never scale (D5-1)");
        }
    }
}

struct Fixture final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> pad;
    std::unique_ptr<Window> window;
    RasterWatch painter{};
};

std::unique_ptr<Fixture> make_fixture(std::string_view primitive, Insets overflow, double hot_scale) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.pad = make_control<Pad>(StableId("pad"));
    (*made.pad).set_requested_bounds(Rect{100.0, 100.0, 100.0, 80.0});
    (*made.pad).overflow = overflow;
    (*made.root).add_child(made.pad);
    made.window = std::make_unique<Window>(made.root, Size{400.0, 300.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "engine-scale-test";
    definition.rules.rules.push_back(pad_rule("pad", primitive, hot_scale));
    window.set_theme(Theme::create(definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the scale theme compiles cleanly");
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

void repaint(Fixture& fixture) {
    Window& window = *fixture.window;
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
}

// Hovers the pad and runs the clock until everything settles.
void hover_and_settle(Fixture& fixture) {
    Window& window = *fixture.window;
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, Point{150.0, 140.0}}));
    repaint(fixture);
    for (int step = 0; step < 40 && window.motion_clock().armed(); ++step) {
        fixture.time.time += milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(fixture.time.time));
        repaint(fixture);
    }
    require(!window.motion_clock().armed(), "the scale settles");
}

void scale_grows_within_granted_overflow() {
    // 10 across and 6 down let the 40 by 24 face grow to exactly 1.5.
    std::unique_ptr<Fixture> fixture = make_fixture("pill_plate", Insets{10.0, 6.0, 10.0, 6.0}, 1.5);
    Fixture& made = *fixture;
    require(made.painter.rasters > 0U && made.painter.last_extent.width == 40U &&
                made.painter.last_extent.height == 24U,
            "at rest the face is drawn at its own size");
    const Point rest = made.painter.last_origin;
    const PartPaint before = (*made.pad).face();
    const ThemeEngine& engine = (*made.window).theme().engine();
    const PartHitRegion rest_region = engine.hit_region(*made.pad, before);
    const Rect layout = (*made.pad).arranged_bounds();
    hover_and_settle(made);
    require(made.painter.last_extent.width == 60U && made.painter.last_extent.height == 36U,
            "hot, the face is drawn at one and a half times its size");
    require(made.painter.last_origin.x == rest.x - 10.0 && made.painter.last_origin.y == rest.y - 6.0,
            "about its centre");
    const PartHitRegion hot_region = engine.hit_region(*made.pad, (*made.pad).face());
    require(hot_region.bounds == rest_region.bounds && hot_region.shape == rest_region.shape,
            "the hit region does not change");
    require((*made.pad).arranged_bounds() == layout, "nor does the layout");
}

void scale_is_clamped_to_the_grant() {
    std::unique_ptr<Fixture> none = make_fixture("pill_plate", Insets{}, 1.5);
    hover_and_settle(*none);
    require((*none).painter.last_extent.width == 40U && (*none).painter.last_extent.height == 24U,
            "a part granted no overflow never grows");
    std::unique_ptr<Fixture> some = make_fixture("pill_plate", Insets{5.0, 3.0, 5.0, 3.0}, 1.5);
    hover_and_settle(*some);
    require((*some).painter.last_extent.width == 50U && (*some).painter.last_extent.height == 30U,
            "a part grows only as far as its grant allows");
    std::unique_ptr<Fixture> shrinking = make_fixture("pill_plate", Insets{}, 0.5);
    hover_and_settle(*shrinking);
    require((*shrinking).painter.last_extent.width == 20U && (*shrinking).painter.last_extent.height == 12U,
            "a part may shrink without any grant");
}

// E7r form (b): while the scale moves, the part's raster at its own size is
// stretched about its centre and nothing is baked; at rest it is baked
// sharp at its scaled size.
void scale_motion_stretches_without_baking() {
    std::unique_ptr<Fixture> fixture = make_fixture("pill_plate", Insets{10.0, 6.0, 10.0, 6.0}, 1.5);
    Fixture& made = *fixture;
    Window& window = *made.window;
    const std::uint64_t rested = window.engine_parts().cache_statistics().bakes;
    require(rested == 1U, "the face at rest is one bake");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, Point{150.0, 140.0}}));
    repaint(made);
    require(window.motion_clock().armed(), "the scale moves");
    // The move's start bakes the shared raster at the face's own size.
    const std::uint64_t started = window.engine_parts().cache_statistics().bakes;
    require(started == rested + 1U, "the move's start bakes the shared face once");
    std::size_t moving_frames = 0U;
    for (int step = 0; step < 40 && window.motion_clock().armed(); ++step) {
        made.time.time += milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(made.time.time));
        repaint(made);
        if (!window.motion_clock().armed()) break;
        moving_frames = moving_frames + 1U;
        require(made.painter.stretched, "a moving scale stretches the raster");
        require(made.painter.last_extent.width == 40U && made.painter.last_extent.height == 24U,
                "the stretched raster is the face at its own size");
        const Rect destination = made.painter.last_destination;
        require(std::abs(destination.x + destination.width * 0.5 - 40.0) < 1e-9 &&
                    std::abs(destination.y + destination.height * 0.5 - 32.0) < 1e-9,
                "about the face's centre");
        require(destination.width > 40.0 && destination.width < 60.0, "between its own and its scaled size");
        require(window.engine_parts().cache_statistics().bakes == started, "nothing is baked while it moves");
    }
    require(moving_frames >= 3U, "the move spans frames");
    require(!made.painter.stretched && made.painter.last_extent.width == 60U && made.painter.last_extent.height == 36U,
            "at rest the face is drawn sharp at its scaled size");
    require(window.engine_parts().cache_statistics().bakes == started + 1U, "baked once, at rest");

    // Leaving: the shrink's start bakes the face in its new state once.
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, Point{390.0, 290.0}}));
    repaint(made);
    const std::uint64_t leaving = window.engine_parts().cache_statistics().bakes;
    require(leaving == started + 2U, "the shrink's start bakes the face's new state once");
    for (int step = 0; step < 40 && window.motion_clock().armed(); ++step) {
        made.time.time += milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(made.time.time));
        repaint(made);
        if (!window.motion_clock().armed()) break;
        require(made.painter.stretched, "the shrink stretches the raster too");
        require(window.engine_parts().cache_statistics().bakes == leaving, "nothing is baked while it shrinks");
    }
    require(!made.painter.stretched && made.painter.last_extent.width == 40U, "at rest it is at its own size");

    // Hovered again, both shared faces are held: the move's start bakes
    // nothing, and only the sharp rest does.
    const std::uint64_t again = window.engine_parts().cache_statistics().bakes;
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, Point{150.0, 140.0}}));
    repaint(made);
    require(window.engine_parts().cache_statistics().bakes == again, "a second grow's start bakes nothing");
    hover_and_settle(made);
    require(window.engine_parts().cache_statistics().bakes == again + 1U, "only its rest is baked");
}

// -- text slots ----------------------------------------------------------------

bool inside_shape(PartShape shape, Rect plate, Point point) {
    const bool inside = shape_signed_distance(shape, plate, point) <= 1e-9;
    return inside;
}

void slots_lie_inside_the_shape() {
    const char* const plates[] = {"pill_plate", "gold_plate", "inset_well"};
    const std::array<Size, 5> sizes{Size{40.0, 24.0}, Size{24.0, 24.0}, Size{100.0, 20.0}, Size{20.0, 60.0},
                                    Size{12.0, 12.0}};
    const std::array<double, 4> scales{1.0, 1.25, 1.5, 2.0};
    const std::array<double, 4> text_scales{1.0, 1.5, 2.0, 3.0};
    std::size_t checked = 0U;
    for (const char* const name : plates) {
        const EnginePrimitive* const primitive = builtin_primitive_registry().find(name);
        require(primitive != nullptr, "the plate is registered");
        const PrimitiveDescriptor& descriptor = (*primitive).descriptor();
        for (std::size_t kind = 0U; kind < part_shape_kind_count; ++kind) {
            PrimitiveParameterBlock parameters = default_parameters(descriptor);
            require(set_choice_parameter(parameters, descriptor, "shape", part_shape_names[kind]) ==
                        ParameterAssignment::assigned,
                    "the shape is chosen");
            require(set_number_parameter(parameters, descriptor, "corner_radius", 6.0) ==
                        ParameterAssignment::assigned,
                    "the radius is set");
            const PartShape shape = (*primitive).shape(parameters);
            for (const Size size : sizes) {
                for (const double scale : scales) {
                    for (const double text_scale : text_scales) {
                        const Insets insets =
                            (*primitive).content_insets(parameters, size, DeviceScale{scale}, text_scale);
                        const double width = size.width - insets.left - insets.right;
                        const double height = size.height - insets.top - insets.bottom;
                        if (!(width > 0.0) || !(height > 0.0)) continue;
                        const double line = std::max(1.0, std::floor(std::min(scale, 2.0) + 0.5)) / scale;
                        const Rect plate{0.0, 0.0, size.width, size.height - line};
                        const std::array<Point, 4> corners{
                            Point{insets.left, insets.top}, Point{insets.left + width, insets.top},
                            Point{insets.left, insets.top + height}, Point{insets.left + width, insets.top + height}};
                        for (const Point corner : corners) {
                            require(inside_shape(shape, plate, corner), "a text slot lies inside the plate's shape");
                        }
                        checked = checked + 1U;
                    }
                }
            }
        }
    }
    require(checked > 200U, "most cases leave room for text");
}

void engine_slots_shrink_to_the_centre() {
    std::unique_ptr<Fixture> fixture = make_fixture("pill_plate", Insets{}, 1.0);
    Fixture& made = *fixture;
    Window& window = *made.window;
    const ThemeEngine& engine = window.theme().engine();
    PartPaint part = (*made.pad).face();
    const Rect bounds = part.bounds;
    double previous_width = bounds.width;
    const std::array<double, 4> text_scales{1.0, 1.5, 2.0, 3.0};
    for (const double text_scale : text_scales) {
        window.set_text_scale(text_scale);
        const Rect slot = engine.text_slot(*made.pad, part);
        require(slot.x >= bounds.x && slot.y >= bounds.y && slot.x + slot.width <= bounds.x + bounds.width &&
                    slot.y + slot.height <= bounds.y + bounds.height,
                "the slot stays inside the part at every text scale");
        require(slot.width <= previous_width, "a larger text scale pads the slot further");
        previous_width = slot.width;
    }
    window.set_text_scale(1.0);
    const Rect normal = engine.text_slot(*made.pad, part);
    // The rounded rectangle of radius 3, two lines and 4 of padding across.
    require(normal.width > 20.0 && normal.width < 32.0 && normal.height > 16.0 && normal.height < 20.0,
            "at text scale 1 the slot is the face less its lines and padding");

    part.bounds = Rect{20.0, 20.0, 12.0, 24.0};
    window.set_text_scale(3.0);
    const Rect narrow = engine.text_slot(*made.pad, part);
    require(narrow.width == 0.0 && narrow.x == 26.0 && narrow.height > 0.0,
            "where the padding leaves no room across, the slot is the centre line");

    PartPaint plain = part;
    plain.key = PartKey{"other", {}, {}};
    require(engine.text_slot(*made.pad, plain) == plain.bounds, "a part the recipe draws has its bounds");
}

} // namespace

int main() {
    scale_is_checked_at_load();
    scale_grows_within_granted_overflow();
    scale_is_clamped_to_the_grant();
    scale_motion_stretches_without_baking();
    slots_lie_inside_the_shape();
    engine_slots_shrink_to_the_centre();
    std::cout << "engine scale and text slot tests passed\n";
    return 0;
}
