#include "gui_forms/anatomy.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/menu_controls.hpp"
#include "gui_forms/collection_controls.hpp"
#include "gui_forms/date_time_picker.hpp"
#include "gui_forms/diagnostic_controls.hpp"
#include "gui_forms/inspection_controls.hpp"
#include "gui_forms/input_controls.hpp"
#include "gui_forms/material.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/schema.hpp"
#include "gui_forms/timer.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

// Each test holds its window on the heap. main inlines every test, and
// the sanitizer build gives each inlined local its own stack slot, so
// windows held on the stack add up to a frame of 90 KB in main.

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool near(double left, double right) {
    return std::abs(left - right) <= 1.0e-9;
}

// Every published standard state must be declared by the control's anatomy.
void require_declared_states(const Control& control) {
    const ControlAnatomy& anatomy = control.anatomy();
    const AnatomyStateSet active = control.anatomy_states();
    for (std::size_t index = 0; index < anatomy_state_count; ++index) {
        const AnatomyState state = static_cast<AnatomyState>(index);
        if (active.contains(state)) {
            require(declares_state(anatomy, state),
                    std::string(anatomy.control_id) + " publishes undeclared state " +
                        std::string(anatomy_state_name(state)));
        }
    }
    const unsigned int declared_custom_bits =
        (1U << static_cast<unsigned int>(anatomy.custom_states.size())) - 1U;
    const unsigned int active_custom_bits = active.custom_bits();
    require((active_custom_bits & ~declared_custom_bits) == 0U,
            std::string(anatomy.control_id) + " publishes an undeclared custom state");
}

// Every present part lies inside the control's region, except for parts
// that are allowed to reach past it (none of the range controls today).
void require_parts_within_region(const Control& control) {
    const ControlAnatomy& anatomy = control.anatomy();
    std::vector<PartGeometry> geometry(anatomy.parts.size());
    control.fill_part_geometry(geometry);
    const Rect region = control.client_rectangle();
    for (std::size_t index = 0; index < geometry.size(); ++index) {
        if (!geometry[index].present) {
            require(geometry[index].bounds.empty(),
                    "an absent part must publish empty bounds");
            continue;
        }
        const Rect bounds = geometry[index].bounds;
        const bool inside = bounds.x >= region.x - 0.5 &&
                            bounds.y >= region.y - 0.5 &&
                            bounds.right() <= region.right() + 0.5 &&
                            bounds.bottom() <= region.bottom() + 0.5;
        require(inside, std::string(anatomy.control_id) + " part " +
                            std::string(anatomy.parts[index].name) +
                            " must lie within the control region");
    }
}

void test_names_are_registered_and_round_trip() {
    for (std::size_t index = 0; index < part_role_count; ++index) {
        const PartRole role = static_cast<PartRole>(index);
        const std::string_view name = part_role_name(role);
        require(is_registered_name(name), "part role names must be snake_case");
        const std::optional<PartRole> parsed = part_role_from_name(name);
        require(parsed.has_value() && *parsed == role,
                "part role names must round-trip");
    }
    for (std::size_t index = 0; index < anatomy_state_count; ++index) {
        const AnatomyState state = static_cast<AnatomyState>(index);
        const std::string_view name = anatomy_state_name(state);
        require(is_registered_name(name), "state names must be snake_case");
        const std::optional<AnatomyState> parsed = anatomy_state_from_name(name);
        require(parsed.has_value() && *parsed == state,
                "state names must round-trip");
    }
    require(part_role_name(PartRole::count).empty() &&
                anatomy_state_name(AnatomyState::count).empty(),
            "out-of-range enumerators must have no name");
    require(anatomy_state_name(AnatomyState::drop_accepted) == "drop_accepted" &&
                part_role_name(PartRole::preselect) == "preselect" &&
                anatomy_state_name(AnatomyState::preselect) == "preselect",
            "snake_case enumerators must map to the same registered names");
    require(!part_role_from_name("Thumb").has_value() &&
                !anatomy_state_from_name("drop-accepted").has_value(),
            "lookup by stored name must be exact");

    require(is_registered_name("scroll_thumb") && is_registered_name("a1_b2"),
            "snake_case names must be accepted");
    require(!is_registered_name("") && !is_registered_name("_thumb") &&
                !is_registered_name("thumb_") && !is_registered_name("scroll__thumb") &&
                !is_registered_name("Scroll") && !is_registered_name("scroll-thumb") &&
                !is_registered_name("1st") && !is_registered_name("scroll thumb"),
            "malformed names must be rejected");

    // One name, three spellings (owner decision 8).
    require(registered_name("scroll-thumb") == "scroll_thumb" &&
                registered_name("ScrollThumb") == "scroll_thumb" &&
                registered_name("scroll_thumb") == "scroll_thumb" &&
                registered_name("HScrollBar") == "h_scroll_bar" &&
                registered_name("Bounds.Width") == "bounds.width" &&
                registered_name("Font.LetterSpacing") == "font.letter_spacing",
            "every spelling converts to the stored snake_case name");
    require(css_spelling("drop_accepted") == "drop-accepted" &&
                display_spelling("h_scroll_bar") == "HScrollBar" &&
                display_spelling("font.letter_spacing") == "Font.LetterSpacing",
            "the CSS and display spellings are derived from the stored name");
}

constexpr std::array<PartDeclaration, 2> duplicate_parts{{
    {.name = "thumb", .role = PartRole::thumb, .composed_from = {}},
    {.name = "thumb", .role = PartRole::face, .composed_from = {}},
}};
constexpr std::array<PartDeclaration, 1> single_region{{
    {.name = "region", .role = PartRole::region, .composed_from = {}},
}};
constexpr std::array<AnatomyState, 2> duplicate_states{
    AnatomyState::hot, AnatomyState::hot,
};
constexpr std::array<CustomStateDeclaration, 1> shadowing_custom_state{{
    {.name = "pressed"},
}};
constexpr std::array<CustomStateDeclaration, 2> custom_states{{
    {.name = "armed"},
    {.name = "recording"},
}};

AnatomyDefect defect_of(const ControlAnatomy& anatomy) {
    const AnatomyValidation validation = validate_control_anatomy(anatomy);
    return validation.defect;
}

void test_validation_rejects_malformed_anatomy() {
    require(defect_of(base_control_anatomy()) == AnatomyDefect::none,
            "the base anatomy must validate");

    const ControlAnatomy no_parts{
        .control_id = "empty", .parts = {}, .states = {}, .custom_states = {}};
    require(defect_of(no_parts) == AnatomyDefect::no_parts,
            "an anatomy without parts must be rejected");

    const ControlAnatomy bad_id{
        .control_id = "TrackBar", .parts = single_region, .states = {},
        .custom_states = {}};
    require(defect_of(bad_id) == AnatomyDefect::control_id_not_snake_case,
            "a control id must be snake_case");

    const ControlAnatomy duplicated{
        .control_id = "dial", .parts = duplicate_parts, .states = {},
        .custom_states = {}};
    const AnatomyValidation duplicate_result = validate_control_anatomy(duplicated);
    require(duplicate_result.defect == AnatomyDefect::duplicate_part_name &&
                duplicate_result.index == 1U,
            "a duplicated part name must be rejected at its second declaration");

    const ControlAnatomy repeated_state{
        .control_id = "dial", .parts = single_region, .states = duplicate_states,
        .custom_states = {}};
    require(defect_of(repeated_state) == AnatomyDefect::duplicate_state,
            "a repeated state must be rejected");

    const ControlAnatomy shadowing{
        .control_id = "dial", .parts = single_region, .states = {},
        .custom_states = shadowing_custom_state};
    require(defect_of(shadowing) == AnatomyDefect::custom_state_shadows_standard_state,
            "a custom state may not reuse a standard state's name");

    const ControlAnatomy with_custom{
        .control_id = "dial", .parts = single_region, .states = {},
        .custom_states = custom_states};
    require(defect_of(with_custom) == AnatomyDefect::none,
            "declared custom states must validate");
    const std::optional<CustomStateIndex> recording =
        find_custom_state(with_custom, "recording");
    require(recording.has_value() && (*recording).value == 1U &&
                !find_custom_state(with_custom, "idle").has_value(),
            "custom states must be found by declared name");
    AnatomyStateSet states;
    states.insert(*recording);
    require(states.contains(*recording) && !states.contains(CustomStateIndex{0U}) &&
                states.standard_bits() == 0U,
            "custom state bits must be independent of standard states");
}

void test_range_control_anatomies_validate() {
    std::shared_ptr<TrackBar> track = make_control<TrackBar>(StableId("anatomy.track"));
    std::shared_ptr<HScrollBar> scroll = make_control<HScrollBar>(StableId("anatomy.scroll"));
    std::shared_ptr<ProgressBar> progress = make_control<ProgressBar>(StableId("anatomy.progress"));
    const std::array<const Control*, 3> controls{track.get(), scroll.get(), progress.get()};
    for (std::size_t index = 0; index < controls.size(); ++index) {
        const ControlAnatomy& anatomy = (*controls[index]).anatomy();
        const AnatomyValidation validation = validate_control_anatomy(anatomy);
        require(validation.defect == AnatomyDefect::none,
                std::string(anatomy.control_id) + " anatomy is malformed: " +
                    std::string(anatomy_defect_name(validation.defect)));
        require(find_part(anatomy, "region").has_value(),
                std::string(anatomy.control_id) + " must declare its region");
    }
    require((*track).anatomy().control_id == "track_bar" &&
                (*scroll).anatomy().control_id == "h_scroll_bar" &&
                (*progress).anatomy().control_id == "progress_bar",
            "range controls must publish their registry ids");
    require(&(*track).anatomy() == &(*make_control<TrackBar>(StableId("anatomy.track.2"))).anatomy(),
            "an anatomy belongs to the type, not the instance");

    std::vector<PartGeometry> wrong_size(1U);
    bool rejected = false;
    try {
        (*track).fill_part_geometry(wrong_size);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "geometry storage of the wrong size must be rejected");
}

// The PlaySuite workaround copied "ten points at each end" from TrackBar's
// private pointer mapping. The published value span must give the same
// mapping, so a consumer can read it instead.
void test_track_bar_geometry_matches_pointer_mapping() {
    std::shared_ptr<TrackBar> slider = make_control<TrackBar>(StableId("anatomy.slider"));
    (*slider).set_requested_bounds({10.0, 10.0, 200.0, 28.0});
    (*slider).set_value(25.0);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(slider, Size{240.0, 60.0});
    Window& window = *held_window;
    window.perform_layout();

    const Rect span = (*slider).value_span_bounds();
    const PartGeometry track = (*slider).parts().track;
    const PartGeometry thumb = (*slider).parts().thumb;
    const PartGeometry fill = (*slider).parts().fill;
    const PartGeometry ticks = (*slider).parts().ticks;
    require(track.present && thumb.present && ticks.present,
            "classic track bar must publish track, thumb and ticks");
    require(!fill.present, "classic track bar has no fill");
    const Rect region = (*slider).client_rectangle();
    require(track.bounds == span && near(span.x, 10.0) &&
                near(span.width, region.width - 20.0),
            "classic track must be the value span inset by ten units");
    require(near(thumb.bounds.x + thumb.bounds.width * 0.5, span.x + span.width * 0.25),
            "thumb centre must sit at the value's position along the span");

    const Rect absolute = (*slider).absolute_bounds();
    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = {absolute.x + span.x + span.width * 0.75, absolute.y + 14.0};
    down.pointer_id = 3;
    require(window.dispatch_pointer(down) && near((*slider).value(), 75.0),
            "pointer at three quarters of the published span must select 75");
    const TrackBar::States dragging = (*slider).states();
    require(dragging.pressed && dragging.captured && !dragging.normal,
            "dragging must publish pressed and captured");
    require_declared_states(*slider);
    const TrackBar::Parts dragged = (*slider).parts();
    require(dragged.thumb.states.contains(AnatomyState::pressed) &&
                dragged.thumb.states.contains(AnatomyState::captured) &&
                !dragged.track.states.contains(AnatomyState::pressed),
            "the dragged thumb, not the track, is pressed and captured");
    PointerEvent up = down;
    up.action = PointerAction::up;
    require(window.dispatch_pointer(up), "pointer up must be handled");
    require((*slider).states().normal,
            "release must return to normal");
    require(!(*slider).parts().thumb.states.contains(AnatomyState::pressed),
            "release clears the thumb's own states");

    (*slider).set_visual_style(TrackBarVisualStyle::filled);
    const PartGeometry filled = (*slider).parts().fill;
    const PartGeometry groove = (*slider).parts().track;
    require(filled.present && near(filled.bounds.x, span.x) &&
                near(filled.bounds.width, span.width * 0.75) &&
                near(groove.bounds.width, span.width),
            "filled style must publish the value portion of the groove");
    (*slider).set_visual_style(TrackBarVisualStyle::compact);
    require(!(*slider).parts().ticks.present,
            "compact style draws no ticks");
    require_parts_within_region(*slider);
}

void test_scroll_bar_geometry_and_states() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.scroll.root"));
    std::shared_ptr<HScrollBar> horizontal = make_control<HScrollBar>(StableId("anatomy.scroll.h"));
    (*horizontal).set_requested_bounds({10.0, 10.0, 240.0, 18.0});
    (*horizontal).set_range(0.0, 100.0);
    (*horizontal).set_large_change(20.0);
    (*horizontal).set_value(50.0);
    std::shared_ptr<VScrollBar> vertical = make_control<VScrollBar>(StableId("anatomy.scroll.v"));
    (*vertical).set_requested_bounds({260.0, 10.0, 18.0, 180.0});
    (*root).add_child(horizontal);
    (*root).add_child(vertical);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{300.0, 210.0});
    Window& window = *held_window;
    window.perform_layout();

    const std::array<std::shared_ptr<ScrollBar>, 2> bars{horizontal, vertical};
    for (std::size_t index = 0; index < bars.size(); ++index) {
        const ScrollBar& bar = *bars[index];
        require(bar.parts().decrement_arrow.bounds == bar.decrement_button_bounds() &&
                    bar.parts().increment_arrow.bounds == bar.increment_button_bounds() &&
                    bar.parts().track.bounds == bar.track_bounds() &&
                    bar.parts().thumb.bounds == bar.thumb_bounds(),
                "scroll bar parts must equal its public geometry");
        require_parts_within_region(bar);
        require_declared_states(bar);
    }
    const ControlAnatomy& anatomy = (*horizontal).anatomy();
    const std::optional<PartIndex> arrow = find_part(anatomy, "decrement_arrow");
    require(arrow.has_value() &&
                anatomy.parts[(*arrow).value].role == PartRole::actuator &&
                anatomy.parts[(*arrow).value].composed_from == "button",
            "scroll arrows must be actuators composed from the button system");

    const Rect absolute = (*horizontal).absolute_bounds();
    const Rect thumb = (*horizontal).thumb_bounds();
    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = {absolute.x + thumb.x + thumb.width * 0.5,
                     absolute.y + thumb.y + thumb.height * 0.5};
    require(window.dispatch_pointer(down), "thumb press must be handled");
    const ScrollBar::States dragging = (*horizontal).states();
    require(dragging.pressed && dragging.captured,
            "thumb drag must publish pressed and captured");
    const ScrollBar::Parts dragged = (*horizontal).parts();
    require(dragged.thumb.states.contains(AnatomyState::pressed) &&
                dragged.thumb.states.contains(AnatomyState::captured) &&
                !dragged.decrement_arrow.states.contains(AnatomyState::pressed),
            "the dragged thumb carries pressed and captured; the arrows do not");
    PointerEvent up = down;
    up.action = PointerAction::up;
    require(window.dispatch_pointer(up), "thumb release must be handled");

    const Rect arrow_bounds = (*horizontal).decrement_button_bounds();
    PointerEvent arrow_down = down;
    arrow_down.position = {absolute.x + arrow_bounds.x + arrow_bounds.width * 0.5,
                           absolute.y + arrow_bounds.y + arrow_bounds.height * 0.5};
    require(window.dispatch_pointer(arrow_down), "arrow press must be handled");
    const ScrollBar::Parts arrow_pressed = (*horizontal).parts();
    require(arrow_pressed.decrement_arrow.states.contains(AnatomyState::pressed) &&
                !arrow_pressed.thumb.states.contains(AnatomyState::pressed),
            "a pressed arrow carries the pressed state on its own");
    PointerEvent arrow_up = arrow_down;
    arrow_up.action = PointerAction::up;
    require(window.dispatch_pointer(arrow_up), "arrow release must be handled");

    (*horizontal).enabled = false;
    const ScrollBar::States disabled = (*horizontal).states();
    require(disabled.disabled && !disabled.enabled,
            "a disabled control must publish disabled, not enabled");
}

void test_progress_bar_geometry_and_states() {
    std::shared_ptr<ProgressBar> progress = make_control<ProgressBar>(StableId("anatomy.bar"));
    (*progress).set_requested_bounds({0.0, 0.0, 204.0, 20.0});
    (*progress).set_value(50.0);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(progress, Size{204.0, 20.0});
    Window& window = *held_window;
    window.perform_layout();

    const PartGeometry fill = (*progress).parts().fill;
    const PartGeometry activity = (*progress).parts().activity;
    require(fill.present && !activity.present,
            "a determinate bar publishes its fill and no activity");
    require(near(fill.bounds.x, 2.0) && near(fill.bounds.width, 100.0),
            "fill must cover the value's share of the interior");
    require(!(*progress).states().indeterminate,
            "a determinate bar is not indeterminate");

    (*progress).set_visual_style(ProgressBarVisualStyle::marquee);
    require(!(*progress).parts().fill.present &&
                (*progress).parts().activity.present,
            "a marquee bar publishes its activity band instead of a fill");
    require((*progress).states().indeterminate,
            "a marquee bar is indeterminate");
    require_parts_within_region(*progress);
    require_declared_states(*progress);
}

// A control type that declares no anatomy of its own.
class UndeclaredControl final : public Control {
public:
    explicit UndeclaredControl(StableId stable_id) : Control(std::move(stable_id)) {}
};

void test_base_anatomy_for_undeclared_controls() {
    std::shared_ptr<UndeclaredControl> plain =
        make_control<UndeclaredControl>(StableId("anatomy.plain"));
    (*plain).set_requested_bounds({0.0, 0.0, 80.0, 40.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(plain, Size{80.0, 40.0});
    Window& window = *held_window;
    window.perform_layout();
    require(&(*plain).anatomy() == &base_control_anatomy(),
            "a type that declares nothing inherits the base anatomy");
    const PartGeometry region = (*plain).parts().region;
    require(region.present && region.bounds == (*plain).client_rectangle(),
            "the base region is the client rectangle");
    require(!find_part((*plain).anatomy(), "thumb").has_value(),
            "an undeclared part name must not resolve");
    require_declared_states(*plain);
}

// Records where text is drawn, in window coordinates, and measures text with
// Painter's default metrics so it can serve as the window's text metrics.
class TextOriginPainter final : public Painter {
public:
    void save() override { saved_offsets.push_back(offset); }
    void restore() override {
        if (!saved_offsets.empty()) {
            offset = saved_offsets.back();
            saved_offsets.pop_back();
        }
    }
    void translate(Point delta) override {
        offset.x += delta.x;
        offset.y += delta.y;
    }
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point origin, std::string_view, FontSpec, Color) override {
        const Point placed{origin.x + offset.x, origin.y + offset.y};
        text_origins.push_back(placed);
    }
    void draw_image(ImageId, Rect, double) override {}

    Point offset{};
    std::vector<Point> saved_offsets;
    std::vector<Point> text_origins;
};

void test_button_label_matches_painted_text() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.buttons.root"));
    (*root).set_requested_bounds({0.0, 0.0, 300.0, 120.0});
    std::shared_ptr<Button> button = make_control<Button>(StableId("anatomy.button"), "Apply");
    (*button).set_requested_bounds({20.0, 30.0, 120.0, 30.0});
    (*root).add_child(button);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{300.0, 120.0});
    Window& window = *held_window;
    TextOriginPainter painter;
    window.set_text_metrics_provider(&painter);
    window.perform_layout();

    const ControlAnatomy& anatomy = (*button).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                anatomy.control_id == "button",
            "Button must publish a valid anatomy");
    const PartGeometry face = (*button).parts().face;
    const PartGeometry label = (*button).parts().label;
    require(face.present && face.bounds == (*button).client_rectangle(),
            "the button face is its whole client rectangle");
    require(label.present && label.bounds.width > 0.0 && label.bounds.height > 0.0,
            "a button with text publishes its label");
    require(!(*button).parts().icon.present,
            "a button without an image publishes no icon");
    require(near(label.bounds.x + label.bounds.width * 0.5, face.bounds.width * 0.5),
            "a centred label is centred in the face");

    static_cast<void>(window.paint(painter, {0.0, 0.0, 300.0, 120.0}));
    require(painter.text_origins.size() == 1U, "the button must draw one line of text");
    const Rect absolute = (*button).absolute_bounds();
    require(near(painter.text_origins[0].x, absolute.x + label.bounds.x),
            "the published label must start where paint draws the text");
    require(painter.text_origins[0].y > absolute.y + label.bounds.y &&
                painter.text_origins[0].y <= absolute.y + label.bounds.bottom(),
            "the painted baseline must fall inside the published label");
    window.set_text_metrics_provider(nullptr);

    require(!(*button).states().preselect,
            "a button is not preselected until the form makes it the accept target");
    window.set_accept_button(button);
    require((*button).states().preselect,
            "the accept button publishes preselect");
    require((*button).parts().preselect.present,
            "the preselect cue part is present whether or not it is shown");

    const PointerEvent down{PointerAction::down, PointerButton::primary,
                            {absolute.x + 10.0, absolute.y + 10.0}};
    require(window.dispatch_pointer(down), "button press must be handled");
    const Button::States pressed = (*button).states();
    require(pressed.pressed && !pressed.normal,
            "a pressed button publishes pressed, not normal");
    require_declared_states(*button);
    require_parts_within_region(*button);
}

void test_check_box_and_radio_button_parts() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.choices.root"));
    (*root).set_requested_bounds({0.0, 0.0, 300.0, 120.0});
    std::shared_ptr<CheckBox> check = make_control<CheckBox>(StableId("anatomy.check"), "Wrap lines");
    (*check).set_requested_bounds({10.0, 10.0, 160.0, 24.0});
    std::shared_ptr<RadioButton> radio = make_control<RadioButton>(StableId("anatomy.radio"), "Metric");
    (*radio).set_requested_bounds({10.0, 50.0, 160.0, 24.0});
    (*root).add_child(check);
    (*root).add_child(radio);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{300.0, 120.0});
    Window& window = *held_window;
    window.perform_layout();

    require(validate_control_anatomy((*check).anatomy()).defect == AnatomyDefect::none &&
                validate_control_anatomy((*radio).anatomy()).defect == AnatomyDefect::none,
            "CheckBox and RadioButton must publish valid anatomies");
    const CheckBox::Parts check_parts = (*check).parts();
    const RadioButton::Parts radio_parts = (*radio).parts();
    const std::array<PartGeometry, 2> wells{check_parts.well, radio_parts.well};
    const std::array<PartGeometry, 2> labels{check_parts.label, radio_parts.label};
    const std::array<PartGeometry, 2> marks{check_parts.indicator, radio_parts.indicator};
    for (std::size_t index = 0; index < wells.size(); ++index) {
        const PartGeometry well = wells[index];
        const PartGeometry label = labels[index];
        require(well.present && near(well.bounds.x, 1.0) &&
                    near(well.bounds.width, 15.0) && near(well.bounds.height, 15.0) &&
                    near(well.bounds.y, (24.0 - 15.0) * 0.5),
                "a choice publishes its 15-unit well, centred vertically");
        require(label.present && label.bounds.x >= well.bounds.right(),
                "the label follows the well");
        require(!marks[index].present, "an unchecked choice shows no mark");
    }
    require_declared_states(*check);
    require_declared_states(*radio);
    require_parts_within_region(*check);
    require_parts_within_region(*radio);
    require(!(*check).parts().face.present,
            "a normal-appearance check box has no button face");

    (*check).set_checked(true);
    (*radio).set_checked(true);
    const PartGeometry check_mark = (*check).parts().indicator;
    const PartGeometry radio_dot = (*radio).parts().indicator;
    const Rect check_well = (*check).parts().well.bounds;
    const Rect radio_well = (*radio).parts().well.bounds;
    require(check_mark.present && check_well.contains(check_mark.bounds),
            "a checked box publishes its mark inside the well");
    require(radio_dot.present && radio_well.contains(radio_dot.bounds) &&
                near(radio_dot.bounds.width, 7.0),
            "a checked radio publishes its 7-unit dot inside the well");
    require((*check).states().checked &&
                (*radio).states().checked,
            "checked choices publish checked");

    (*check).set_three_state(true);
    (*check).set_check_state(CheckState::indeterminate);
    const CheckBox::States mixed = (*check).states();
    require(mixed.indeterminate && !mixed.checked &&
                (*check).parts().indicator.present,
            "an indeterminate box publishes indeterminate and its bar");

    (*check).set_indicator_style(ChoiceIndicatorStyle::toggle);
    (*check).set_check_state(CheckState::unchecked);
    const PartGeometry knob_off = (*check).parts().indicator;
    (*check).set_check_state(CheckState::checked);
    const PartGeometry knob_on = (*check).parts().indicator;
    require(knob_off.present && knob_on.present &&
                knob_on.bounds.x > knob_off.bounds.x &&
                near((*check).parts().well.bounds.width, 30.0),
            "a toggle publishes its knob, which moves when checked");

    (*check).set_appearance(CheckBoxAppearance::button);
    require((*check).parts().face.present &&
                !(*check).parts().well.present &&
                !(*check).parts().indicator.present,
            "the button appearance publishes a face instead of a well");
    require_parts_within_region(*check);
}

void test_link_label_custom_visited_state() {
    std::shared_ptr<LinkLabel> link = make_control<LinkLabel>(StableId("anatomy.link"), "Learn more");
    (*link).set_requested_bounds({0.0, 0.0, 160.0, 22.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(link, Size{160.0, 22.0});
    Window& window = *held_window;
    window.perform_layout();
    const ControlAnatomy& anatomy = (*link).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none,
            "LinkLabel must publish a valid anatomy");
    const std::optional<CustomStateIndex> visited = find_custom_state(anatomy, "visited");
    require(visited.has_value(), "LinkLabel declares a visited state");
    require(!(*link).states().visited, "a new link is not visited");
    (*link).set_visited(true);
    require((*link).states().visited && (*link).anatomy_states().contains(*visited),
            "a visited link publishes its visited state");
    const PartGeometry label = (*link).parts().label;
    const PartGeometry underline = (*link).parts().underline;
    require(label.present && underline.present &&
                near(label.bounds.x, underline.bounds.x) &&
                near(label.bounds.width, underline.bounds.width) &&
                underline.bounds.y > label.bounds.bottom(),
            "the underline runs beneath the label across its width");
    require_parts_within_region(*link);
}

void test_drop_down_button_disclosure() {
    std::shared_ptr<DropDownButton> button = make_control<DropDownButton>(
        StableId("anatomy.drop"), "Export", DropDownButtonMode::split);
    (*button).set_requested_bounds({0.0, 0.0, 140.0, 28.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(button, Size{140.0, 28.0});
    Window& window = *held_window;
    window.perform_layout();
    require(validate_control_anatomy((*button).anatomy()).defect == AnatomyDefect::none &&
                (*button).anatomy().control_id == "drop_down_button",
            "DropDownButton must publish its own valid anatomy");
    const PartGeometry segment = (*button).parts().drop_down;
    const PartGeometry arrow = (*button).parts().disclosure;
    const Rect region = (*button).client_rectangle();
    require(segment.present && segment.bounds == (*button).drop_down_bounds() &&
                near(segment.bounds.right(), region.right()) &&
                near(segment.bounds.width, (*button).drop_down_width()),
            "the drop-down segment sits at the trailing edge");
    require(arrow.present && segment.bounds.contains(arrow.bounds),
            "the disclosure arrow lies inside its segment");
    require((*button).states().closed,
            "a shut drop-down publishes closed");
    (*button).set_drop_down_open(true);
    const DropDownButton::States open = (*button).states();
    require(open.open && !open.closed,
            "an open drop-down publishes open");
    require_declared_states(*button);
    require_parts_within_region(*button);
}

void test_panel_and_scrollable_parts() {
    std::shared_ptr<Panel> panel = make_control<Panel>(StableId("anatomy.panel"));
    (*panel).set_requested_bounds({0.0, 0.0, 200.0, 120.0});
    std::shared_ptr<Label> tall = make_control<Label>(StableId("anatomy.panel.tall"), "Tall");
    (*tall).set_requested_bounds({0.0, 0.0, 120.0, 400.0});
    (*panel).add_child(tall);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(panel, Size{200.0, 120.0});
    Window& window = *held_window;
    window.perform_layout();
    require(validate_control_anatomy((*panel).anatomy()).defect == AnatomyDefect::none &&
                (*panel).anatomy().control_id == "panel",
            "Panel must publish its own valid anatomy");
    require((*panel).parts().face.present && !(*panel).parts().edge.present,
            "a borderless panel publishes a face and no edge");
    (*panel).border_style = BorderStyle::line;
    require((*panel).parts().edge.present,
            "a bordered panel publishes its edge");
    require(!(*panel).parts().scrollable.vertical_thumb.present,
            "without auto-scroll no scroll axis is published");

    (*panel).auto_scroll = true;
    window.perform_layout();
    const PartGeometry track = (*panel).parts().scrollable.vertical_track;
    const PartGeometry thumb = (*panel).parts().scrollable.vertical_thumb;
    const PartGeometry viewport = (*panel).parts().scrollable.viewport;
    require(track.present && thumb.present && track.bounds.contains(thumb.bounds),
            "an overflowing auto-scroll panel publishes its vertical thumb inside its track");
    require(viewport.present && viewport.bounds.right() <= track.bounds.x,
            "the viewport ends where the vertical scroll axis begins");
    const ControlAnatomy& anatomy = (*panel).anatomy();
    const std::optional<PartIndex> thumb_index = find_part(anatomy, "vertical_thumb");
    require(thumb_index.has_value() &&
                anatomy.parts[(*thumb_index).value].role == PartRole::thumb,
            "a container's scroll thumb shares the thumb role with ScrollBar");
    require_declared_states(*panel);
    require_parts_within_region(*panel);
}

void test_group_box_and_label_parts() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.groups.root"));
    (*root).set_requested_bounds({0.0, 0.0, 320.0, 200.0});
    std::shared_ptr<GroupBox> group = make_control<GroupBox>(StableId("anatomy.group"), "Output");
    (*group).set_requested_bounds({10.0, 10.0, 200.0, 100.0});
    std::shared_ptr<Label> label = make_control<Label>(StableId("anatomy.label"), "Ready");
    (*label).set_requested_bounds({10.0, 130.0, 160.0, 24.0});
    (*root).add_child(group);
    (*root).add_child(label);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{320.0, 200.0});
    Window& window = *held_window;
    TextOriginPainter painter;
    window.set_text_metrics_provider(&painter);
    window.perform_layout();

    const PartGeometry caption = (*group).parts().caption;
    const PartGeometry edge = (*group).parts().edge;
    require(caption.present && edge.present && caption.bounds.y < edge.bounds.y &&
                caption.bounds.bottom() > edge.bounds.y,
            "the group caption sits across the top of its frame");
    require((*group).anatomy().control_id == "group_box", "GroupBox publishes its own id");

    const PartGeometry text = (*label).parts().label;
    require(text.present && text.bounds.width > 0.0 && near(text.bounds.x, 2.0),
            "a near-aligned label publishes its text block from the left inset");
    static_cast<void>(window.paint(painter, {0.0, 0.0, 320.0, 200.0}));
    const Rect absolute = (*label).absolute_bounds();
    bool found = false;
    for (std::size_t index = 0; index < painter.text_origins.size(); ++index) {
        const Point origin = painter.text_origins[index];
        if (near(origin.x, absolute.x + text.bounds.x) &&
            origin.y > absolute.y + text.bounds.y &&
            origin.y <= absolute.y + text.bounds.bottom()) {
            found = true;
        }
    }
    window.set_text_metrics_provider(nullptr);
    require(found, "the label's published block must contain where paint draws its text");
    require_parts_within_region(*group);
    require_parts_within_region(*label);
}

// A record read through a base type agrees with what the dynamic type
// publishes: MaterialPanel paints no border, so even with a border style set
// its Panel view has no edge.
void test_base_record_follows_dynamic_type() {
    std::shared_ptr<MaterialPanel> material =
        make_control<MaterialPanel>(StableId("anatomy.material"));
    (*material).set_requested_bounds({0.0, 0.0, 120.0, 60.0});
    (*material).border_style = BorderStyle::line;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(material, Size{120.0, 60.0});
    Window& window = *held_window;
    window.perform_layout();
    const Panel& as_panel = *material;
    const Panel::Parts parts = as_panel.parts();
    require(parts.face.present && !parts.edge.present,
            "a base-type record must report the dynamic type's parts");
    require(parts.scrollable.region.bounds == (*material).client_rectangle(),
            "composed records carry the scrollable parts");
    const Control& as_control = *material;
    const Control::States states = as_control.states();
    require(states.normal && states.enabled && !states.disabled,
            "the base states record reads the dynamic type's states");
}

void test_text_box_caret_and_selection() {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("anatomy.text"), "hello world");
    (*box).set_requested_bounds({0.0, 0.0, 200.0, 28.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(box, Size{200.0, 28.0});
    Window& window = *held_window;
    TextOriginPainter painter;
    window.set_text_metrics_provider(&painter);
    window.perform_layout();
    static_cast<void>(window.paint(painter, {0.0, 0.0, 200.0, 28.0}));

    (*box).select(Utf8Offset(0U), Utf8Offset(0U));
    const PartGeometry start_caret = (*box).parts().caret;
    (*box).select(Utf8Offset(5U), Utf8Offset(5U));
    const TextBox::Parts at_five = (*box).parts();
    require(at_five.well.present && at_five.content.present,
            "a text box publishes its well and content");
    require(start_caret.present && at_five.caret.present &&
                at_five.caret.bounds.x > start_caret.bounds.x &&
                at_five.content.bounds.contains(at_five.caret.bounds),
            "the caret moves with the insertion point inside the content");
    require(!at_five.selection.present, "an empty selection publishes no highlight");

    (*box).select(Utf8Offset(0U), Utf8Offset(5U));
    const TextBox::Parts selected = (*box).parts();
    require(selected.selection.present && !selected.caret.present &&
                near(selected.selection.bounds.x, at_five.content.bounds.x) &&
                near(selected.selection.bounds.right(), at_five.caret.bounds.x + 0.5),
            "a selection publishes its highlight between the two boundaries");
    window.set_text_metrics_provider(nullptr);

    (*box).read_only = true;
    (*box).set_visual_status(ControlVisualStatus::invalid);
    const TextBox::States states = (*box).states();
    require(states.read_only && states.invalid && !states.pending,
            "read-only and validity are published states");
    require_declared_states(*box);
    require_parts_within_region(*box);
}

void test_spin_combo_and_picker_parts() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.inputs.root"));
    (*root).set_requested_bounds({0.0, 0.0, 320.0, 160.0});
    std::shared_ptr<NumericUpDown> spin = make_control<NumericUpDown>(StableId("anatomy.spin"));
    (*spin).set_requested_bounds({10.0, 10.0, 120.0, 26.0});
    std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("anatomy.combo"));
    (*combo).set_requested_bounds({10.0, 50.0, 160.0, 26.0});
    (*combo).items = {"One", "Two"};
    std::shared_ptr<DateTimePicker> picker = make_control<DateTimePicker>(StableId("anatomy.picker"));
    (*picker).set_requested_bounds({10.0, 90.0, 200.0, 26.0});
    (*root).add_child(spin);
    (*root).add_child(combo);
    (*root).add_child(picker);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{320.0, 160.0});
    Window& window = *held_window;
    window.perform_layout();

    const NumericUpDown::Parts spin_parts = (*spin).parts();
    require(spin_parts.field.present && spin_parts.increment_arrow.present &&
                spin_parts.decrement_arrow.present &&
                near(spin_parts.field.bounds.right(), spin_parts.increment_arrow.bounds.x) &&
                near(spin_parts.increment_arrow.bounds.bottom(),
                     spin_parts.decrement_arrow.bounds.y),
            "a spin box publishes its field and the two halves of its actuator");
    const ControlAnatomy& spin_anatomy = (*spin).anatomy();
    const std::optional<PartIndex> field = find_part(spin_anatomy, "field");
    require(field.has_value() &&
                spin_anatomy.parts[(*field).value].composed_from == "text_box",
            "the spin box field is composed from the text box system");

    const ComboBox::Parts closed = (*combo).parts();
    require(closed.drop_down.present && closed.disclosure.present &&
                closed.drop_down.bounds.contains(closed.disclosure.bounds) &&
                closed.content.bounds.right() <= closed.divider.bounds.x,
            "a combo box publishes its field, divider, drop-down and arrow");
    require((*combo).states().closed, "a shut combo box is closed");
    (*combo).set_dropped_down(true);
    require((*combo).states().open && !(*combo).states().closed,
            "an open combo box publishes open");
    (*combo).set_dropped_down(false);
    // Hot under the pointer (stage T4, hot states): the anatomy declares it.
    PointerEvent over{};
    over.action = PointerAction::move;
    over.position = Point{40.0, 63.0};
    static_cast<void>(window.dispatch_pointer(over));
    require((*combo).states().hot, "a combo box under the pointer is hot");
    over.position = Point{300.0, 150.0};
    static_cast<void>(window.dispatch_pointer(over));
    require(!(*combo).states().hot, "a combo box the pointer has left is not hot");

    require((*picker).parts().drop_down.present && !(*picker).parts().well.present,
            "a plain picker has a drop-down and no check box");
    (*picker).set_show_check_box(true);
    (*picker).set_checked(true);
    const DateTimePicker::Parts checked = (*picker).parts();
    require(checked.well.present && checked.indicator.present &&
                checked.well.bounds.contains(checked.indicator.bounds) &&
                checked.content.bounds.x >= checked.well.bounds.right() &&
                (*picker).states().checked,
            "a checked picker publishes its check well, mark and state");
    (*picker).set_show_up_down(true);
    const DateTimePicker::Parts spinning = (*picker).parts();
    require(!spinning.drop_down.present && spinning.increment_arrow.present &&
                spinning.decrement_arrow.present,
            "the up-down picker publishes actuators instead of a drop-down");
    const std::array<const Control*, 3> controls{spin.get(), combo.get(), picker.get()};
    for (std::size_t index = 0; index < controls.size(); ++index) {
        require(validate_control_anatomy((*controls[index]).anatomy()).defect ==
                    AnatomyDefect::none,
                "every input anatomy must validate");
        require_declared_states(*controls[index]);
        require_parts_within_region(*controls[index]);
    }
}

void test_list_rows_are_item_records() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.lists.root"));
    (*root).set_requested_bounds({0.0, 0.0, 400.0, 200.0});
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("anatomy.list"));
    (*list).set_requested_bounds({10.0, 10.0, 160.0, 120.0});
    (*list).items = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta"};
    std::shared_ptr<CheckedListBox> checks = make_control<CheckedListBox>(StableId("anatomy.checks"));
    (*checks).set_requested_bounds({200.0, 10.0, 160.0, 120.0});
    (*checks).items = {"Wrap", "Spell", "Grammar"};
    (*root).add_child(list);
    (*root).add_child(checks);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{400.0, 200.0});
    Window& window = *held_window;
    window.perform_layout();

    require(validate_control_anatomy((*list).anatomy()).defect == AnatomyDefect::none &&
                validate_control_anatomy((*checks).anatomy()).defect == AnatomyDefect::none,
            "list anatomies with item parts must validate");
    (*list).select_index(1U);
    const std::vector<ListBox::Item> rows = (*list).realized_items();
    require(rows.size() == (*list).realized_item_count() && rows.size() >= 4U &&
                rows.size() < 7U,
            "a list realizes only the rows that fit, plus one");
    const std::vector<SemanticNode> semantic = (*list).semantic_virtual_children();
    for (std::size_t item = 0; item < rows.size(); ++item) {
        const ListBox::Item& row = rows[item];
        require(row.index == item && row.stable_id == semantic[row.index].stable_id,
                "an item's identity is its semantic virtual child's stable ID");
        require(row.parts.row.present && row.parts.label.present &&
                    row.parts.row.bounds.contains(row.parts.label.bounds),
                "each row publishes its label inside its row");
        if (item > 0U) {
            require(near(row.parts.row.bounds.y, rows[item - 1U].parts.row.bounds.bottom()),
                    "rows stack without gaps");
        }
    }
    require(rows[1].states.selected && rows[1].states.current && !rows[0].states.selected,
            "item states are per row");

    (*checks).set_item_check_state(2U, CheckState::checked);
    const std::vector<CheckedListBox::Item> checked_rows = (*checks).realized_items();
    require(checked_rows.size() == 3U && checked_rows[2].states.checked &&
                !checked_rows[0].states.checked,
            "checked rows publish checked per item");
    require(checked_rows[2].parts.check.present && !checked_rows[0].parts.check.present &&
                checked_rows[2].parts.check_well.bounds.contains(checked_rows[2].parts.check.bounds) &&
                checked_rows[2].parts.label.bounds.x >= checked_rows[2].parts.check_well.bounds.right(),
            "a checked row publishes its mark inside its check well, before the label");

    const ListBox& as_list = *checks;
    require(as_list.realized_items().size() == 3U,
            "a base-type item view reads the dynamic type's rows");
    bool rejected = false;
    std::vector<PartGeometry> wrong(1U);
    try {
        (*list).fill_realized_item_geometry(0U, wrong);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "item geometry storage of the wrong size must be rejected");
    rejected = false;
    std::vector<PartGeometry> sized((*list).anatomy().item_systems.front().parts.size());
    try {
        (*list).fill_realized_item_geometry(rows.size(), sized);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    require(rejected, "an item past the realized rows must be rejected");
}

constexpr std::array<PartDeclaration, 1> clashing_item_parts{{
    {.name = "region", .role = PartRole::region, .composed_from = {}},
}};
constexpr std::array<ItemSystemDeclaration, 1> clashing_item_systems{{
    {.name = "item", .parts = clashing_item_parts, .states = {}, .custom_states = {}},
}};

constexpr std::array<PartDeclaration, 1> row_item_parts{{
    {.name = "row", .role = PartRole::region, .composed_from = {}},
}};
constexpr std::array<PartDeclaration, 1> column_item_parts{{
    {.name = "column_header", .role = PartRole::region, .composed_from = {}},
}};
constexpr std::array<ItemSystemDeclaration, 2> row_and_column_systems{{
    {.name = "item", .parts = row_item_parts, .states = {}, .custom_states = {}},
    {.name = "column", .parts = column_item_parts, .states = {}, .custom_states = {}},
}};
// The second system repeats the first system's part name.
constexpr std::array<ItemSystemDeclaration, 2> repeated_part_systems{{
    {.name = "item", .parts = row_item_parts, .states = {}, .custom_states = {}},
    {.name = "column", .parts = row_item_parts, .states = {}, .custom_states = {}},
}};
constexpr std::array<ItemSystemDeclaration, 2> repeated_name_systems{{
    {.name = "item", .parts = row_item_parts, .states = {}, .custom_states = {}},
    {.name = "item", .parts = column_item_parts, .states = {}, .custom_states = {}},
}};
constexpr std::array<ItemSystemDeclaration, 1> empty_item_system{{
    {.name = "item", .parts = {}, .states = {}, .custom_states = {}},
}};

void test_item_names_never_repeat_control_parts() {
    const ControlAnatomy clashing{
        .control_id = "grid", .parts = single_region, .states = {},
        .custom_states = {}, .item_systems = clashing_item_systems};
    require(validate_control_anatomy(clashing).defect == AnatomyDefect::duplicate_item_part_name,
            "an item part may not reuse a control part's name");
}

constexpr std::array<AnatomyState, 2> repeated_part_state{{
    AnatomyState::pressed, AnatomyState::pressed}};
constexpr std::array<PartDeclaration, 1> repeated_state_part{{
    {.name = "thumb", .role = PartRole::thumb, .composed_from = {}, .states = repeated_part_state},
}};

void test_part_states_are_validated() {
    const ControlAnatomy repeated{
        .control_id = "slider", .parts = repeated_state_part, .states = {},
        .custom_states = {}, .item_systems = {}};
    require(validate_control_anatomy(repeated).defect == AnatomyDefect::duplicate_part_state,
            "a part lists each of its own states once");
    require(declares_part_state(repeated_state_part[0], AnatomyState::pressed) &&
                !declares_part_state(repeated_state_part[0], AnatomyState::hot),
            "a part's declaration lists the states it takes on its own");
}

// A type's interactions are its public events, base types first, named in
// by the accessor's name with their payload as the accessor spells it.
void test_interactions_follow_the_type() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.interactions.root"));
    std::shared_ptr<Button> button =
        make_control<Button>(StableId("anatomy.interactions.button"), "Go");
    (*root).add_child(button);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    Window& window = *held_window;
    const std::vector<InteractionDeclaration> declared = (*button).interactions();
    require(validate_interactions(declared).defect == InteractionDefect::none,
            "a button's interactions validate");
    const std::size_t visible = find_interaction(declared, "visible_changed");
    const std::size_t clicked = find_interaction(declared, "clicked");
    const std::size_t dialog = find_interaction(declared, "dialog_result_changed");
    require(visible < clicked && clicked < dialog && dialog < declared.size(),
            "Control's events come before ButtonBase's, and those before Button's");
    require(declared[clicked].payload == "ButtonBase" && declared[clicked].mutable_payload,
            "clicked passes the button by mutable reference");
    const std::size_t validating = find_interaction(declared, "validating");
    const std::size_t text = find_interaction(declared, "text_changed");
    require(declared[validating].mutable_payload && declared[text].payload == "std::string" &&
                !declared[text].mutable_payload,
            "payloads are spelled without const or reference marks");
    require(find_interaction(declared, "tick") == declared.size(),
            "a control lists only events its type raises");

    Timer timer(window, std::chrono::milliseconds(10));
    const std::vector<InteractionDeclaration> timer_declared = timer.interactions();
    require(timer_declared.size() == 1U && timer_declared[0].name == "tick" &&
                timer_declared[0].payload.empty(),
            "a component lists its own events");
}

constexpr std::array<InteractionDeclaration, 2> repeated_interactions{{
    {.name = "clicked", .payload = {}, .mutable_payload = false},
    {.name = "clicked", .payload = {}, .mutable_payload = false},
}};
constexpr std::array<InteractionDeclaration, 1> kebab_interaction{{
    {.name = "value-changed", .payload = "double", .mutable_payload = false},
}};

void test_interactions_are_validated() {
    require(validate_interactions(repeated_interactions).defect == InteractionDefect::duplicate_name,
            "an interaction name appears once in a type");
    require(validate_interactions(kebab_interaction).defect ==
                InteractionDefect::name_not_snake_case,
            "interaction names are snake_case");
}

void test_item_systems_are_distinct() {
    const ControlAnatomy two{
        .control_id = "grid", .parts = single_region, .states = {},
        .custom_states = {}, .item_systems = row_and_column_systems};
    require(validate_control_anatomy(two).defect == AnatomyDefect::none,
            "a control may declare a second item system");
    const std::optional<ItemSystemIndex> column = find_item_system(two, "column");
    require(column.has_value() && (*column).value == 1U &&
                find_item_part(two.item_systems[1], "column_header").has_value() &&
                !find_item_part(two.item_systems[1], "row").has_value(),
            "item parts are found within their own system");
    const ControlAnatomy repeated_part{
        .control_id = "grid", .parts = single_region, .states = {},
        .custom_states = {}, .item_systems = repeated_part_systems};
    const AnatomyValidation repeated = validate_control_anatomy(repeated_part);
    require(repeated.defect == AnatomyDefect::duplicate_item_part_name &&
                repeated.item_system == 1U,
            "an item part name is unique across item systems");
    const ControlAnatomy repeated_name{
        .control_id = "grid", .parts = single_region, .states = {},
        .custom_states = {}, .item_systems = repeated_name_systems};
    require(validate_control_anatomy(repeated_name).defect ==
                AnatomyDefect::duplicate_item_system_name,
            "item system names are unique");
    const ControlAnatomy empty{
        .control_id = "grid", .parts = single_region, .states = {},
        .custom_states = {}, .item_systems = empty_item_system};
    require(validate_control_anatomy(empty).defect == AnatomyDefect::item_system_without_parts,
            "an item system declares at least one part");
}

// FlagsValueEditor needs an enum descriptor; the Gallery coverage test
// checks it with the Gallery's own.
void test_value_editors_and_diagnostic_views() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.editors.root"));
    (*root).set_requested_bounds({0.0, 0.0, 360.0, 200.0});
    std::shared_ptr<ColorValueEditor> color = make_control<ColorValueEditor>(StableId("anatomy.color"));
    (*color).set_requested_bounds({10.0, 10.0, 200.0, 28.0});
    std::shared_ptr<MetricsView> metrics = make_control<MetricsView>(StableId("anatomy.metrics"));
    (*metrics).set_requested_bounds({10.0, 90.0, 300.0, 90.0});
    (*root).add_child(color);
    (*root).add_child(metrics);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{360.0, 200.0});
    Window& window = *held_window;
    window.perform_layout();

    const ColorValueEditor::Parts color_parts = (*color).parts();
    require(color_parts.swatch.present && color_parts.field.present &&
                color_parts.swatch.bounds.right() <= color_parts.field.bounds.x,
            "the colour editor's swatch sits before its text field");
    const MetricsView::Parts metric_parts = (*metrics).parts();
    require(metric_parts.accent_strip.present &&
                near(metric_parts.accent_strip.bounds.width, (*metrics).accent_width()) &&
                near(metric_parts.accent_strip.bounds.x, 0.0),
            "the metrics view publishes its accent strip on the leading edge");
    const std::array<const Control*, 2> controls{color.get(), metrics.get()};
    for (std::size_t index = 0; index < controls.size(); ++index) {
        require(validate_control_anatomy((*controls[index]).anatomy()).defect ==
                    AnatomyDefect::none,
                "every editor and view anatomy must validate");
        require_declared_states(*controls[index]);
        require_parts_within_region(*controls[index]);
    }
}

void test_tabs_menus_and_breadcrumbs_are_items() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.nav.root"));
    (*root).set_requested_bounds({0.0, 0.0, 640.0, 360.0});
    std::shared_ptr<TabControl> tabs = make_control<TabControl>(StableId("anatomy.tabs"));
    (*tabs).set_requested_bounds({10.0, 10.0, 400.0, 160.0});
    (*tabs).add_page(make_control<TabPage>(StableId("anatomy.tabs.general"), "General"));
    (*tabs).add_page(make_control<TabPage>(StableId("anatomy.tabs.advanced"), "Advanced"));
    std::shared_ptr<MenuStrip> menu = make_control<MenuStrip>(StableId("anatomy.menu"));
    (*menu).set_requested_bounds({10.0, 180.0, 400.0, 28.0});
    // Every top-level item needs a menu of its own.
    const std::shared_ptr<Command> open = std::make_shared<Command>("anatomy.open", "Open");
    MenuStripItemSpec file;
    file.stable_id = "anatomy.menu.file";
    file.text = "&File";
    file.items = {{"anatomy.menu.file.open", MenuItemKind::command, open}};
    MenuStripItemSpec hidden;
    hidden.stable_id = "anatomy.menu.hidden";
    hidden.text = "Hidden";
    hidden.items = {{"anatomy.menu.hidden.open", MenuItemKind::command, open}};
    hidden.visible = false;
    MenuStripItemSpec edit;
    edit.stable_id = "anatomy.menu.edit";
    edit.text = "&Edit";
    edit.items = {{"anatomy.menu.edit.open", MenuItemKind::command, open}};
    edit.enabled = false;
    (*menu).set_items({file, hidden, edit});
    std::shared_ptr<BreadcrumbTrail> trail = make_control<BreadcrumbTrail>(StableId("anatomy.trail"));
    (*trail).set_requested_bounds({10.0, 220.0, 420.0, 30.0});
    BreadcrumbSegment home;
    home.stable_id = "anatomy.trail.home";
    home.text = "Home";
    BreadcrumbSegment docs;
    docs.stable_id = "anatomy.trail.docs";
    docs.text = "Documents";
    (*trail).set_segments({home, docs});
    std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("anatomy.split"));
    (*split).set_requested_bounds({10.0, 260.0, 400.0, 90.0});
    (*root).add_child(tabs);
    (*root).add_child(menu);
    (*root).add_child(trail);
    (*root).add_child(split);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{640.0, 360.0});
    Window& window = *held_window;
    window.perform_layout();

    const std::vector<TabControl::Item> tab_items = (*tabs).realized_items();
    require(tab_items.size() == 2U && tab_items[0].stable_id == "anatomy.tabs.general.tab" &&
                tab_items[0].states.selected && !tab_items[1].states.selected,
            "each tab is an item with the semantic tree's id and its own selected state");
    require(tab_items[0].parts.tab_seam.present && !tab_items[1].parts.tab_seam.present &&
                tab_items[0].parts.tab.bounds.contains(tab_items[0].parts.tab_label.bounds),
            "only the selected tab publishes its seam with the display");
    require((*tabs).parts().display.present, "the tab control publishes its display");

    const std::vector<MenuStrip::Item> menu_items = (*menu).realized_items();
    require(menu_items.size() == 2U && menu_items[0].index == 0U && menu_items[1].index == 2U &&
                menu_items[1].states.disabled && !menu_items[0].states.disabled,
            "hidden menu items are not realized; indices stay model positions");
    require(menu_items[0].parts.menu_label.present &&
                menu_items[0].parts.menu_item.bounds.right() <= menu_items[1].parts.menu_item.bounds.x,
            "menu items publish their labels and run left to right");
    require((*menu).parts().divider.present, "the menu strip publishes its bottom rule");

    const std::vector<BreadcrumbTrail::Item> segments = (*trail).realized_items();
    require(segments.size() >= 2U, "the breadcrumb realizes its visible segments");
    require(segments.front().parts.segment_cap.present && !segments.back().parts.segment_cap.present,
            "every segment but the last ends in a chevron cap");

    const SplitContainer::Parts split_parts = (*split).parts();
    require(split_parts.seam.present && split_parts.seam.bounds.height > 0.0,
            "the split container publishes its seam");
    const std::array<const Control*, 4> controls{tabs.get(), menu.get(), trail.get(), split.get()};
    for (std::size_t index = 0; index < controls.size(); ++index) {
        require(validate_control_anatomy((*controls[index]).anatomy()).defect ==
                    AnatomyDefect::none,
                "navigation anatomies must validate");
        require_declared_states(*controls[index]);
        require_parts_within_region(*controls[index]);
    }
}

void test_tree_and_correspondence_items() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.trees.root"));
    (*root).set_requested_bounds({0.0, 0.0, 700.0, 400.0});
    std::shared_ptr<TreeView> tree = make_control<TreeView>(StableId("anatomy.tree"));
    (*tree).set_requested_bounds({10.0, 10.0, 220.0, 160.0});
    TreeViewItem parent;
    parent.stable_id = "anatomy.tree.parent";
    parent.text = "Parent";
    parent.expandable = true;
    parent.expanded = true;
    TreeViewItem child;
    child.stable_id = "anatomy.tree.child";
    child.text = "Child";
    child.depth = 1U;
    child.enabled = false;
    (*tree).set_items({parent, child});
    std::shared_ptr<CorrespondenceView> view =
        make_control<CorrespondenceView>(StableId("anatomy.correspondence"));
    (*view).set_requested_bounds({250.0, 10.0, 420.0, 300.0});
    CorrespondenceItem first;
    first.stable_id = "anatomy.correspondence.first";
    first.title = "First";
    first.stale = true;
    CorrespondenceItem second;
    second.stable_id = "anatomy.correspondence.second";
    second.title = "Second";
    second.unavailable = true;
    (*view).set_items({first, second});
    (*root).add_child(tree);
    (*root).add_child(view);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{700.0, 400.0});
    Window& window = *held_window;
    window.perform_layout();

    const std::vector<TreeView::Item> nodes = (*tree).realized_items();
    require(nodes.size() == 2U && nodes[0].stable_id == "anatomy.tree.parent" &&
                nodes[0].states.open && nodes[0].parts.expander.present &&
                !nodes[1].parts.expander.present && nodes[1].states.disabled,
            "tree nodes publish their expander, open state and availability per node");
    require(nodes[1].parts.label.bounds.x > nodes[0].parts.label.bounds.x,
            "a deeper node's label is indented");

    const ControlAnatomy& anatomy = (*view).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                find_item_custom_state(anatomy.item_systems.front(), "pinned").has_value() &&
                find_item_custom_state(anatomy.item_systems.front(), "stale").has_value(),
            "correspondence rows declare their own pinned and stale states");
    const std::vector<CorrespondenceView::Item> cards = (*view).realized_items();
    require(cards.size() == 2U && cards[0].states.stale && !cards[1].states.stale &&
                cards[1].states.unavailable && !cards[0].states.pinned,
            "custom and standard item states are published per card");
    require(cards[0].parts.card.bounds.contains(cards[0].parts.glyph.bounds) &&
                cards[0].parts.status_rail.present,
            "each card publishes its rail and glyph inside the card");
}

// The details view's column headers are items of a second system, after
// the rows in the generic path and as typed records through
// realized_columns().
void test_object_view_columns_are_items() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("anatomy.objects.root"));
    std::shared_ptr<ObjectView> view = make_control<ObjectView>(StableId("anatomy.objects"));
    (*view).set_bounds({0.0, 0.0, 400.0, 200.0});
    (*view).set_view_mode(ObjectViewMode::details);
    std::vector<ObjectDetailsColumn> columns;
    columns.push_back({.id = {"name"}, .label = "Name", .width = 150.0});
    columns.push_back({.id = {"size"}, .label = "Size", .width = 120.0});
    columns.push_back({.id = {"kind"}, .label = "Kind", .width = 100.0, .sortable = false});
    std::vector<ObjectViewItem> items;
    for (std::size_t index = 0; index < 2U; ++index) {
        ObjectViewItem item;
        item.stable_id = "anatomy.objects.item." + std::to_string(index);
        item.name = "Object " + std::to_string(index);
        item.cells.push_back({{"name"}, item.name});
        item.cells.push_back({{"size"}, std::to_string(index)});
        item.cells.push_back({{"kind"}, "File"});
        items.push_back(item);
    }
    ObjectDetailsSort sort;
    sort.column = ObjectColumnId{"name"};
    (*view).set_details_model(std::move(columns), std::move(items), sort);
    (*root).add_child(view);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{400.0, 200.0});
    Window& window = *held_window;
    window.perform_layout();

    const ControlAnatomy& anatomy = (*view).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                anatomy.item_systems.size() == 2U &&
                find_item_system(anatomy, "column").has_value(),
            "the object view declares rows and columns as item systems");
    const std::vector<ObjectView::Column> realized = (*view).realized_columns();
    require(realized.size() == 3U && realized[0].stable_id == "anatomy.objects.column.name" &&
                realized[0].index == 0U && realized[2].index == 2U,
            "every visible column is a realized column with a stable identity");
    require(realized[0].states.sorted_ascending && !realized[0].states.sorted_descending &&
                !realized[1].states.sorted_ascending,
            "the sorted column publishes its sort direction as an item state");
    require(realized[0].parts.sort_glyph.present && !realized[1].parts.sort_glyph.present,
            "only the sorted column has a sort glyph");
    require(realized[0].parts.column_header.bounds.contains(realized[0].parts.column_label.bounds) &&
                realized[1].parts.column_header.bounds.x >=
                    realized[0].parts.column_header.bounds.right() - 0.5,
            "column labels sit inside their headers, which run left to right");
    const ObjectView::Parts parts = (*view).parts();
    require(parts.header.bounds.contains(realized[1].parts.column_header.bounds),
            "column headers lie in the header band");

    // The generic path: the rows first, then the columns.
    const std::vector<ObjectView::Item> rows = (*view).realized_items();
    const std::size_t total = (*view).realized_item_count();
    require(total == rows.size() + realized.size(), "rows and columns are realized together");
    require((*view).realized_item_system(0U).value == 0U &&
                (*view).realized_item_system(total - 1U).value == 1U,
            "each realized item names its item system");
    std::vector<PartGeometry> column_geometry(anatomy.item_systems[1].parts.size());
    (*view).fill_realized_item_geometry(rows.size(), column_geometry);
    require(column_geometry[0] == realized[0].parts.column_header &&
                (*view).realized_item_stable_id(rows.size()) == realized[0].stable_id,
            "the generic path publishes the same column geometry and identity");
    bool rejected = false;
    std::vector<PartGeometry> row_sized(anatomy.item_systems[0].parts.size());
    try {
        (*view).fill_realized_item_geometry(rows.size(), row_sized);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "a column's geometry needs storage for the column system's parts");
}

constexpr std::array<CustomStateDeclaration, 1> shadowing_item_custom_state{{
    {.name = "selected"},
}};
constexpr std::array<ItemSystemDeclaration, 1> shadowing_item_system{{
    {.name = "item", .parts = row_item_parts, .states = {},
     .custom_states = shadowing_item_custom_state},
}};

void test_item_custom_states_never_shadow_standard_states() {
    const ControlAnatomy shadowing{
        .control_id = "grid", .parts = single_region, .states = {},
        .custom_states = {}, .item_systems = shadowing_item_system};
    require(validate_control_anatomy(shadowing).defect ==
                AnatomyDefect::item_custom_state_shadows_standard_state,
            "an item custom state may not reuse a standard state's name");
}

} // namespace

int main() {
    try {
        test_names_are_registered_and_round_trip();
        test_validation_rejects_malformed_anatomy();
        test_range_control_anatomies_validate();
        test_track_bar_geometry_matches_pointer_mapping();
        test_scroll_bar_geometry_and_states();
        test_progress_bar_geometry_and_states();
        test_base_anatomy_for_undeclared_controls();
        test_button_label_matches_painted_text();
        test_check_box_and_radio_button_parts();
        test_link_label_custom_visited_state();
        test_drop_down_button_disclosure();
        test_panel_and_scrollable_parts();
        test_group_box_and_label_parts();
        test_base_record_follows_dynamic_type();
        test_text_box_caret_and_selection();
        test_spin_combo_and_picker_parts();
        test_list_rows_are_item_records();
        test_item_names_never_repeat_control_parts();
        test_value_editors_and_diagnostic_views();
        test_tabs_menus_and_breadcrumbs_are_items();
        test_tree_and_correspondence_items();
        test_item_custom_states_never_shadow_standard_states();
        test_item_systems_are_distinct();
        test_part_states_are_validated();
        test_interactions_follow_the_type();
        test_interactions_are_validated();
        test_object_view_columns_are_items();
        std::cout << "gui_forms_anatomy_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_anatomy_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
