// The control border (active_border.hpp; the active border study's phase
// 2, roadmap row 2.7): validation; the flash limiter (three transitions a
// second, no saturated-red flash) and the rest poses (reduced motion,
// ambient motion off, high contrast); the colour over time; the stroke's
// path and reach; and a control carrying one in a headless window: it
// paints after the content in its own colour, a moving pose runs its timer
// and a resting one does not, and clearing it ends both. The border is
// also eight registered properties (cp33): active_border and its fields,
// read and written by name through the property table.

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/control/active_border/active_border.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <chrono>
#include <cmath>
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

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::abs(left - right) < 1e-9;
    return result;
}

// One rounded stroke the painter received.
struct RoundedStroke final {
    Rect rect{};
    double radius{};
    Color color{};
    double width{};
};

// Records the rounded strokes and counts the rest.
class StrokePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override { ++square_strokes; }
    void stroke_rounded_rect(Rect rect, double radius, Color color, double width) override {
        strokes.push_back(RoundedStroke{rect, radius, color, width});
    }
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    // Whether a rounded stroke in `color` was received.
    [[nodiscard]] bool stroked(Color color) const noexcept {
        bool found = false;
        for (const RoundedStroke& stroke : strokes) found = found || stroke.color == color;
        return found;
    }

    std::vector<RoundedStroke> strokes{};
    int square_strokes{};
};

const Color blue = Color::rgba(20, 90, 200, 255);
const Color red = Color::rgba(230, 20, 10, 255);
const Color white = Color::rgba(255, 255, 255, 255);

ActiveBorderConditions plain_conditions() {
    ActiveBorderConditions conditions{};
    conditions.theme_colour = blue;
    conditions.high_contrast_colour = white;
    return conditions;
}

void validation() {
    ActiveBorder border{};
    require(active_border_valid(border), "the default border is valid");
    border.width = 0.0;
    require(!active_border_valid(border), "a border needs a width");
    border.width = maximum_active_border_width + 1.0;
    require(!active_border_valid(border), "a border's width is bounded");
    border.width = 2.0;
    border.offset = -maximum_active_border_offset - 1.0;
    require(!active_border_valid(border), "a border's offset is bounded");
    border.offset = 1.0;
    border.corner_radius = -1.0;
    require(!active_border_valid(border), "a corner radius is not negative");
    border.corner_radius = 4.0;
    border.frequency = std::numeric_limits<double>::quiet_NaN();
    require(!active_border_valid(border), "a frequency is finite");
    border.frequency = 40.0;
    require(active_border_valid(border), "a frequency above the limit is valid: the limiter lowers it");
    require(active_border_saturated_red(red) && !active_border_saturated_red(blue) &&
                !active_border_saturated_red(Color::rgba(0, 0, 0, 255)) &&
                !active_border_saturated_red(Color::rgba(255, 0, 0, 0)),
            "saturated red: red at least 0.8 of the channels, and visible");
}

void limiter_and_rest_poses() {
    ActiveBorder flashing{};
    flashing.motion = ActiveBorderMotion::flashing;
    flashing.frequency = 4.0;
    const ActiveBorderPose fast = resolve_active_border(flashing, plain_conditions());
    require(fast.motion == ActiveBorderMotion::flashing && near(fast.frequency, maximum_active_border_frequency),
            "a fast flash is limited to three transitions a second");
    require(fast.lit == blue && fast.alternate == Color::rgba(20, 90, 200, 0),
            "the theme's accent, blinking off");

    ActiveBorder red_flash = flashing;
    red_flash.colour = red;
    const ActiveBorderPose refused = resolve_active_border(red_flash, plain_conditions());
    require(refused.motion == ActiveBorderMotion::steady && refused.lit == red && near(refused.frequency, 0.0),
            "a saturated red does not flash");
    ActiveBorder red_alternate = flashing;
    red_alternate.alternate = red;
    require(resolve_active_border(red_alternate, plain_conditions()).motion == ActiveBorderMotion::steady,
            "nor does a flash to saturated red");
    ActiveBorder red_cycle = red_flash;
    red_cycle.motion = ActiveBorderMotion::cycling;
    require(resolve_active_border(red_cycle, plain_conditions()).motion == ActiveBorderMotion::cycling,
            "a red cycle blends and may move, limited");

    ActiveBorderConditions rest = plain_conditions();
    rest.rest = true;
    const ActiveBorderPose resting = resolve_active_border(flashing, rest);
    require(resting.motion == ActiveBorderMotion::steady && resting.lit == blue,
            "at rest (reduced motion, ambient motion off) the border stays lit");

    ActiveBorderConditions contrast = plain_conditions();
    contrast.high_contrast = true;
    ActiveBorder thin = flashing;
    thin.width = 1.0;
    thin.colour = red;
    const ActiveBorderPose forced = resolve_active_border(thin, contrast);
    require(forced.motion == ActiveBorderMotion::steady && forced.lit == white &&
                near(forced.width, minimum_high_contrast_active_border_width),
            "high contrast: the system colour, still, at least two units wide");

    ActiveBorder stopped = flashing;
    stopped.frequency = 0.0;
    require(resolve_active_border(stopped, plain_conditions()).motion == ActiveBorderMotion::steady,
            "a frequency of 0 is steady");
}

void colour_over_time() {
    ActiveBorderPose flash{};
    flash.lit = blue;
    flash.alternate = white;
    flash.motion = ActiveBorderMotion::flashing;
    flash.frequency = 1.0;
    require(active_border_colour_at(flash, 0.0) == blue && active_border_colour_at(flash, 0.49) == blue,
            "a flash is lit in its first half");
    require(active_border_colour_at(flash, 0.5) == white && active_border_colour_at(flash, 0.99) == white,
            "and alternate in its second");
    require(active_border_colour_at(flash, 1.0) == blue, "lit again at the next cycle");
    require(active_border_colour_at(flash, -3.0) == blue &&
                active_border_colour_at(flash, std::numeric_limits<double>::infinity()) == blue,
            "a negative or infinite time is time 0");
    require(active_border_interval(flash) == std::chrono::milliseconds(500), "a flash repaints at its transitions");

    ActiveBorderPose cycle = flash;
    cycle.motion = ActiveBorderMotion::cycling;
    cycle.lit = Color::rgba(0, 0, 0, 255);
    cycle.alternate = Color::rgba(200, 100, 50, 255);
    require(active_border_colour_at(cycle, 0.0) == cycle.lit, "a cycle starts lit");
    require(active_border_colour_at(cycle, 0.5) == cycle.alternate, "and reaches the alternate at half a cycle");
    require(active_border_colour_at(cycle, 0.25) == Color::rgba(100, 50, 25, 255), "halfway between at a quarter");
    require(active_border_interval(cycle) > std::chrono::milliseconds(0), "a cycle repaints at its frame rate");

    ActiveBorderPose steady = flash;
    steady.motion = ActiveBorderMotion::steady;
    require(active_border_colour_at(steady, 0.7) == blue && active_border_interval(steady).count() == 0,
            "a steady pose is lit and never repaints by itself");
}

void path_and_reach() {
    ActiveBorder border{};
    border.width = 2.0;
    border.offset = 1.0;
    border.corner_radius = 4.0;
    const ActiveBorderPath outside = active_border_path(border, Size{100.0, 40.0});
    require(near(outside.rect.x, -2.0) && near(outside.rect.y, -2.0) && near(outside.rect.width, 104.0) &&
                near(outside.rect.height, 44.0) && near(outside.radius, 6.0),
            "the centre line lies offset plus half the width outside the box");
    require(near(active_border_reach(border), 3.0), "the reach is offset plus width");
    border.offset = -3.0;
    const ActiveBorderPath inside = active_border_path(border, Size{100.0, 40.0});
    require(near(inside.rect.x, 2.0) && near(inside.rect.width, 96.0) && near(inside.radius, 2.0),
            "a negative offset draws inside the box");
    require(near(active_border_reach(border), 0.0), "an inside border reaches nothing outside");
    border.corner_radius = 0.0;
    require(near(active_border_path(border, Size{100.0, 40.0}).radius, 0.0), "a square inside border stays square");
}

void control_carries_a_border() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("border.root"));
    const std::shared_ptr<Panel> card = make_control<Panel>(StableId("border.card"));
    (*card).set_requested_bounds(Rect{40.0, 40.0, 120.0, 60.0});
    (*root).add_child(card);
    Window window(root, Size{300.0, 200.0});
    window.perform_layout();

    ActiveBorder bad{};
    bad.width = -1.0;
    bool refused = false;
    try {
        (*card).set_active_border(bad);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !(*card).active_border().has_value(), "an invalid border throws and changes nothing");

    ActiveBorder steady{};
    steady.colour = blue;
    steady.corner_radius = 4.0;
    (*card).set_active_border(steady);
    require((*card).active_border() == std::optional<ActiveBorder>{steady}, "the border is set");
    StrokePainter first{};
    static_cast<void>(window.paint(first, Rect{0.0, 0.0, 300.0, 200.0}));
    require(first.stroked(blue), "the border paints in its colour");
    require((*card).active_border_pose().has_value() &&
                (*(*card).active_border_pose()).motion == ActiveBorderMotion::steady && !(*card).active_border_animating(),
            "a steady border runs no timer");

    ActiveBorder flashing = steady;
    flashing.motion = ActiveBorderMotion::flashing;
    flashing.frequency = 6.0;
    (*card).set_active_border(flashing);
    StrokePainter second{};
    static_cast<void>(window.paint(second, Rect{0.0, 0.0, 300.0, 200.0}));
    require(second.stroked(blue), "a flash starts lit");
    require((*card).active_border_animating() &&
                near((*(*card).active_border_pose()).frequency, maximum_active_border_frequency),
            "a flashing border runs its timer at the limited frequency");

    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    (*card).invalidate(Dirty::paint);
    StrokePainter calm{};
    static_cast<void>(window.paint(calm, Rect{0.0, 0.0, 300.0, 200.0}));
    require(calm.stroked(blue) && !(*card).active_border_animating() &&
                (*(*card).active_border_pose()).motion == ActiveBorderMotion::steady,
            "under reduced motion the border rests lit and its timer stops");
    settings.reduced_motion = false;
    settings.ambient_motion = false;
    window.set_presentation_settings(settings);
    (*card).invalidate(Dirty::paint);
    StrokePainter switched{};
    static_cast<void>(window.paint(switched, Rect{0.0, 0.0, 300.0, 200.0}));
    require(!(*card).active_border_animating(), "with ambient motion off it rests too");
    settings.ambient_motion = true;
    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    (*card).invalidate(Dirty::paint);
    StrokePainter forced{};
    static_cast<void>(window.paint(forced, Rect{0.0, 0.0, 300.0, 200.0}));
    require(!(*card).active_border_animating() && !forced.stroked(blue),
            "in high contrast it is still and takes the system colour");

    (*card).set_active_border(std::nullopt);
    require(!(*card).active_border().has_value() && !(*card).active_border_animating() &&
                !(*card).active_border_pose().has_value(),
            "clearing the border ends it");
}

// The control border through the property table: each field reads the
// border's (or the default border's), writing one shows the border with
// that field, active_border false clears it, the colours are nullable, an
// invalid value is refused whole, and the border serializes only when
// shown, each field only when it differs from the default.
void border_properties() {
    std::shared_ptr<Panel> card = make_control<Panel>(StableId("props.card"));
    const char* const names[] = {"active_border",        "active_border_color",         "active_border_alternate_color",
                                 "active_border_width",  "active_border_offset",        "active_border_corner_radius",
                                 "active_border_motion", "active_border_frequency"};
    for (const char* name : names) {
        const std::optional<PropertyDescriptor> descriptor = (*card).property_descriptor(name);
        require(descriptor.has_value() && (*descriptor).category == "Appearance" && !(*descriptor).bindable,
                "each border property is registered, in Appearance, not bindable");
        require(!(*card).should_serialize_property(name), "nothing serializes without a border");
    }
    require((*(*card).property_descriptor("active_border_color")).nullable, "the lit colour is nullable");
    const std::optional<PropertyDescriptor> motion = (*card).property_descriptor("active_border_motion");
    require((*motion).kind == BindingValueKind::enumeration && (*motion).enumeration != nullptr &&
                (*(*motion).enumeration).choices.size() == 3U,
            "the motion is an enumeration of steady, flashing and cycling");

    require((*card).property_value("active_border") == BindingValue{false}, "no border at first");
    require((*card).property_value("active_border_width") == BindingValue{2.0}, "a field reads the default border's");
    require((*card).property_value("active_border_color") == BindingValue{}, "no colour reads null");

    (*card).set_property_value("active_border_width", BindingValue{3.0});
    require((*card).active_border().has_value() && near((*(*card).active_border()).width, 3.0),
            "writing a field shows the border with that field");
    require((*card).property_value("active_border") == BindingValue{true}, "and active_border reads true");
    require((*card).should_serialize_property("active_border") &&
                (*card).should_serialize_property("active_border_width") &&
                !(*card).should_serialize_property("active_border_offset"),
            "shown, the border and its changed fields serialize");

    const Color teal = Color::rgba(0, 128, 128);
    (*card).set_property_value("active_border_color", BindingValue{teal});
    (*card).set_property_value("active_border_motion",
                               BindingValue{PropertyEnumValue{"active_border_motion", "cycling", 2}});
    (*card).set_property_value("active_border_frequency", BindingValue{0.5});
    const ActiveBorder written = *(*card).active_border();
    require(written.colour == teal && written.motion == ActiveBorderMotion::cycling && near(written.frequency, 0.5) &&
                near(written.width, 3.0),
            "each write keeps the other fields");
    require((*card).property_value("active_border_color") == BindingValue{teal}, "the colour reads back");
    const std::optional<BindingValue> read_motion = (*card).property_value("active_border_motion");
    require(read_motion && std::get<PropertyEnumValue>(*read_motion).name == "cycling", "the motion reads back");
    (*card).set_property_value("active_border_color", BindingValue{});
    require(!(*(*card).active_border()).colour.has_value(), "null clears the colour to the theme's");

    bool refused = false;
    try {
        (*card).set_property_value("active_border_width", BindingValue{20.0});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && near((*(*card).active_border()).width, 3.0), "a width past 8 is refused, nothing changed");

    (*card).set_property_value("active_border", BindingValue{false});
    require(!(*card).active_border().has_value(), "active_border false clears the border");
    (*card).set_property_value("active_border", BindingValue{true});
    require((*card).active_border() == std::optional<ActiveBorder>{ActiveBorder{}},
            "active_border true shows the default border");
}

} // namespace

int main() {
    validation();
    limiter_and_rest_poses();
    colour_over_time();
    path_and_reach();
    control_carries_a_border();
    border_properties();
    std::cout << "active border: validation, the flash limiter and rest poses, colour over time, path and reach, "
                 "a control carrying one, and its properties passed\n";
    return EXIT_SUCCESS;
}
