#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/group_box/scaled_group_box/scaled_group_box.hpp"
#include "gui_forms/controls/panel/scaled_panel/scaled_panel.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/diagnostic_controls.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/text.hpp"
#include "../src/controls/basic/basic_control_rendering.hpp"
#include "support/named_callbacks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

// Each test holds its window on the heap. main inlines every test, and
// the sanitizer build gives each inlined local its own stack slot, so
// windows held on the stack add up to a frame of 105 KB in main.
namespace callbacks = gui_forms::test_support;

constexpr std::array<std::uint8_t, 70> one_pixel_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SurfaceMaterial solid_material(Color color) {
    const MaterialFillLayer fill = MaterialFillLayer::solid(color);
    const SurfaceMaterial material = SurfaceMaterial::from_parts(&fill, 1U, nullptr, 0U, nullptr, 0.0);
    return material;
}

class RecordingPainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(Point) override {}
    void clip_rect(Rect) override { ++clips; }
    void clip_rounded_rect(Rect, double) override { ++rounded_clips; }
    void fill_rect(Rect bounds, Color color) override {
        ++fills;
        last_fill_bounds = bounds;
        last_fill_color = color;
        fill_colors.push_back(color);
    }
    void fill_rounded_rect(Rect bounds, double, Color color) override {
        ++rounded_fills;
        last_fill_bounds = bounds;
        last_fill_color = color;
        fill_colors.push_back(color);
    }
    void stroke_rect(Rect, Color, double) override { ++strokes; }
    void stroke_rounded_rect(Rect bounds, double, Color color, double width) override {
        ++rounded_strokes;
        last_rounded_stroke_bounds = bounds;
        last_rounded_stroke_color = color;
        last_rounded_stroke_width = width;
    }
    void fill_linear_gradient(Rect, Point, Point,
                              std::span<const GradientStop>) override {
        ++gradients;
    }
    void draw_line(Point from, Point to, Color, double) override {
        ++lines;
        line_starts.push_back(from);
        line_ends.push_back(to);
        text_line_order.push_back('L');
    }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec font, Color color) override {
        texts.emplace_back(text);
        text_colors.push_back(color);
        roles.push_back(font.role);
        text_origins.push_back(origin);
        text_line_order.push_back('T');
    }
    void draw_image(ImageId image, Rect destination, double opacity) override {
        images.push_back(image);
        image_destinations.push_back(destination);
        image_opacities.push_back(opacity);
    }
    Size measure_text_utf8(const std::string_view text, const FontSpec font) override {
        ++text_measurements;
        measured_text_bytes += text.size();
        const Size measured = Painter::measure_text_utf8(text, font);
        return measured;
    }

    std::uint64_t saves{};
    std::uint64_t restores{};
    std::uint64_t clips{};
    std::uint64_t rounded_clips{};
    std::uint64_t fills{};
    std::uint64_t rounded_fills{};
    std::uint64_t strokes{};
    std::uint64_t rounded_strokes{};
    std::uint64_t gradients{};
    std::uint64_t lines{};
    std::size_t text_measurements{};
    std::size_t measured_text_bytes{};
    Rect last_fill_bounds{};
    Color last_fill_color{};
    Rect last_rounded_stroke_bounds{};
    Color last_rounded_stroke_color{};
    double last_rounded_stroke_width{};
    std::vector<Color> fill_colors{};
    std::vector<std::string> texts{};
    std::vector<Color> text_colors{};
    std::vector<FontRole> roles{};
    std::vector<Point> text_origins{};
    std::vector<Point> line_starts{};
    std::vector<Point> line_ends{};
    std::vector<char> text_line_order{};
    std::vector<ImageId> images{};
    std::vector<Rect> image_destinations{};
    std::vector<double> image_opacities{};
};

class FocusSink final : public Control {
public:
    explicit FocusSink(StableId stable_id) : Control(std::move(stable_id)) {
        set_focusable(true);
    }
};

class CancelValidation final {
public:
    void operator()(ControlValidationEvent& event) const {
        event.cancel = true;
    }
};

class TraceDialogResult final {
public:
    explicit TraceDialogResult(std::string& trace) noexcept : trace_(trace) {}

    void operator()(DialogResult result) const {
        trace_ += "result:" +
            std::to_string(static_cast<unsigned>(result)) + "\n";
    }

private:
    std::string& trace_;
};

class ReplaceButtonDialogResult final {
public:
    explicit ReplaceButtonDialogResult(std::shared_ptr<Button> button) noexcept
        : button_(std::move(button)) {}

    void operator()(ButtonBase&) const {
        (*button_).set_dialog_result(DialogResult::no);
    }

private:
    std::shared_ptr<Button> button_{};
};

class TraceCheckState final {
public:
    explicit TraceCheckState(std::string& trace) noexcept : trace_(trace) {}

    void operator()(CheckState value) const {
        trace_ += value == CheckState::checked
            ? "state:checked\n" : "state:other\n";
    }

private:
    std::string& trace_;
};

class TraceBoolean final {
public:
    TraceBoolean(std::string& trace, std::string_view true_text,
                 std::string_view false_text) noexcept
        : trace_(trace), true_text_(true_text), false_text_(false_text) {}

    void operator()(bool value) const {
        trace_.append(value ? true_text_ : false_text_);
    }

private:
    std::string& trace_;
    std::string_view true_text_{};
    std::string_view false_text_{};
};

class RecordDrawingPaint final {
public:
    RecordDrawingPaint(std::uint64_t& count, Rect& bounds, Rect& damage) noexcept
        : count_(count), bounds_(bounds), damage_(damage) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        ++count_;
        bounds_ = bounds;
        damage_ = damage;
        painter.fill_rect(bounds, Color::rgba(1, 2, 3));
    }

private:
    std::uint64_t& count_;
    Rect& bounds_;
    Rect& damage_;
};

// A GroupBox subclass using the protected hooks of the text capability
// (P-8): coerce_text upper-cases the incoming caption before the compare,
// and on_text_changed counts each changed write after the touch.
class UpperCaseGroupBox final : public GroupBox {
public:
    using GroupBox::GroupBox;
    int text_changes{};

protected:
    void coerce_text(std::string& incoming) override {
        for (char& byte : incoming) {
            if (byte >= 'a' && byte <= 'z') byte = static_cast<char>(byte - 'a' + 'A');
        }
    }
    void on_text_changed() override {
        GroupBox::on_text_changed();
        ++text_changes;
    }
};

// on_size_mode_changed counts each changed SizeMode write after the touch.
class CountingPictureBox final : public PictureBox {
public:
    using PictureBox::PictureBox;
    int size_mode_changes{};

protected:
    void on_size_mode_changed() override {
        PictureBox::on_size_mode_changed();
        ++size_mode_changes;
    }
};

void attempt_label_mutation(const std::shared_ptr<Label>& label,
                            bool& rejected) noexcept {
    try {
        (*label).text = "After";
    } catch (const std::logic_error&) {
        rejected = true;
    }
}

Point center(const Control::Ptr& control) {
    const Rect bounds = (*control).absolute_bounds();
    const Point center{.x = bounds.x + bounds.width * 0.5,
                       .y = bounds.y + bounds.height * 0.5};
    return center;
}

void click(Window& window, const Control::Ptr& control) {
    const Point point = center(control);
    const bool operation_check_1 = window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, point});
    require(operation_check_1, "reusable control must handle primary down");
    const bool operation_check_2 = window.dispatch_pointer(
                {PointerAction::up, PointerButton::primary, point});
    require(operation_check_2, "reusable control must handle primary up");
}

void test_public_controls_render_with_role_policy() {
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("controls.panel"));
    (*panel).accessible_name = "Public panel group";
    (*panel).set_requested_bounds({0.0, 0.0, 360.0, 180.0});
    (*panel).border_style = BorderStyle::line;
    std::shared_ptr<gui_forms::GroupBox> group = make_control<GroupBox>(StableId("controls.group"), "Reusable controls");
    (*group).set_requested_bounds({10.0, 10.0, 340.0, 160.0});
    // The caption is the group's `text` property (Windows Forms' GroupBox.Text).
    (*group).set_property_value("text", BindingValue{std::string("Shared controls")});
    require((*group).text.read() == "Shared controls", "GroupBox text is a property");
    require(std::get<std::string>(*(*group).property_value("text")) == "Shared controls",
            "GroupBox text reads back as a property");
    (*group).text = "Reusable controls";
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("controls.label"), "Field content");
    (*label).set_requested_bounds({12.0, 24.0, 140.0, 24.0});
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("controls.button"), "Apply");
    (*button).set_requested_bounds({12.0, 54.0, 90.0, 28.0});
    (*button).set_default_button(true);
    std::shared_ptr<gui_forms::CheckBox> check = make_control<CheckBox>(StableId("controls.check"), "Checked option");
    (*check).set_requested_bounds({12.0, 88.0, 150.0, 24.0});
    (*check).set_checked(true);
    std::shared_ptr<gui_forms::RadioButton> radio = make_control<RadioButton>(StableId("controls.radio"), "Radio option");
    (*radio).set_requested_bounds({170.0, 88.0, 150.0, 24.0});
    (*radio).set_checked(true);
    (*group).add_child(label);
    (*group).add_child(button);
    (*group).add_child(check);
    (*group).add_child(radio);
    (*panel).add_child(group);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(panel, Size{360.0, 180.0});
    Window& window = *held_window;
    window.perform_layout();

    RecordingPainter painter{};
    window.paint(painter, {0.0, 0.0, 360.0, 180.0});
    require(painter.saves == painter.restores && painter.clips >= 6 &&
                painter.gradients >= 4 && painter.rounded_clips >= 3 &&
                painter.rounded_strokes >= 3 && painter.lines >= 2 &&
                painter.texts.size() == 5,
            "public controls must render through retained painter commands");
    require(painter.roles[0] == FontRole::control &&
                painter.roles[1] == FontRole::content,
            "group/control titles and field labels must preserve typography roles");
    require(window.hit_test(center(label)) == group,
            "noninteractive Label must not intercept its logical container");
    require((*panel).semantic_descriptor().role == SemanticRole::group &&
                (*panel).semantic_descriptor().name == "Public panel group" &&
                (*panel).semantic_descriptor().exposed,
            "a named public Panel must expose stock group semantics");
}

void test_button_pointer_and_keyboard_activation() {
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("button"), "Run");
    (*button).set_requested_bounds({10.0, 10.0, 90.0, 28.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(button, Size{120.0, 60.0});
    Window& window = *held_window;
    std::uint64_t clicks = 0;
    SubscriptionToken clicked = (*button).clicked().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ButtonBase&>(clicks));

    click(window, button);
    require(clicks == 1 && window.focused_control() == button,
            "matching pointer press/release must click and focus Button once");
    KeyEvent down{};
    down.action = KeyAction::down;
    down.physical_key = PhysicalKey::space;
    KeyEvent up = down;
    up.action = KeyAction::up;
    const bool operation_check_3 = window.dispatch_key(down);
    require(operation_check_3, "focused Button must activate once on normalized Space release");
    const bool operation_check_4 = window.dispatch_key(up);
    require(operation_check_4, "focused Button must activate once on normalized Space release");
    const bool operation_check_5 = clicks == 2;
    require(operation_check_5, "focused Button must activate once on normalized Space release");
    require(!(*button).pressed_visual(),
            "Button keyboard visual must clear after activation");
}

void test_button_focus_cue_tracks_input_modality() {
    std::shared_ptr<gui_forms::Panel> root =
        make_control<Panel>(StableId("focus-visible.root"));
    std::shared_ptr<gui_forms::Button> first =
        make_control<Button>(StableId("focus-visible.first"), "First");
    std::shared_ptr<gui_forms::Button> second =
        make_control<Button>(StableId("focus-visible.second"), "Second");
    ControlVisualRecipe focus_values[control_surface_state_count];
    for (std::size_t index = 0; index < control_surface_state_count; ++index) {
        focus_values[index].material =
            solid_material(Color::rgba(224U, 232U, 240U));
        focus_values[index].focus_ring = Color::rgba(40U, 125U, 155U);
        focus_values[index].focus_width = 2.0;
        focus_values[index].focus_offset = 3.0;
        focus_values[index].authored_focus_outline = true;
    }
    const ControlStateRecipes focus_recipes = ControlStateRecipes::from_parts(
        focus_values, control_surface_state_count);
    (*first).set_visual_recipes(focus_recipes);
    (*second).set_visual_recipes(focus_recipes);
    (*first).set_requested_bounds({10.0, 10.0, 80.0, 30.0});
    (*second).set_requested_bounds({100.0, 10.0, 80.0, 30.0});
    (*root).add_child(first);
    (*root).add_child(second);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 50.0});
    Window& window = *held_window;
    window.perform_layout();

    click(window, first);
    require(window.focused_control() == first &&
                !window.focus_cue_visible() && !(*first).focus_cue_visible() &&
                (*first).visual_outsets() == Insets{},
            "primary-pointer focus must suppress focus-visible without losing focus");

    KeyEvent tab{};
    tab.action = KeyAction::down;
    tab.physical_key = PhysicalKey::tab;
    const bool operation_check_6 = window.dispatch_key(tab);
    require(operation_check_6, "keyboard traversal must expose focus-visible on the moved focus");
    const bool operation_check_7 = window.focused_control() == second;
    require(operation_check_7, "keyboard traversal must expose focus-visible on the moved focus");
    const bool operation_check_8 = window.focus_cue_visible();
    require(operation_check_8, "keyboard traversal must expose focus-visible on the moved focus");
    const bool operation_check_9 = (*second).focus_cue_visible();
    require(operation_check_9, "keyboard traversal must expose focus-visible on the moved focus");
    const bool operation_check_10 = (*second).visual_outsets() == Insets{5.0, 5.0, 5.0, 5.0};
    require(operation_check_10, "keyboard traversal must expose focus-visible on the moved focus");

    const bool operation_check_11 = window.perform_semantic_action(
                "focus-visible.first", SemanticAction::focus);
    require(operation_check_11, "semantic focus navigation must retain a visible accessibility cue");
    const bool operation_check_12 = window.focused_control() == first;
    require(operation_check_12, "semantic focus navigation must retain a visible accessibility cue");
    const bool operation_check_13 = window.focus_cue_visible();
    require(operation_check_13, "semantic focus navigation must retain a visible accessibility cue");
    const bool operation_check_14 = (*first).focus_cue_visible();
    require(operation_check_14, "semantic focus navigation must retain a visible accessibility cue");
    const bool operation_check_15 = (*first).visual_outsets() == Insets{5.0, 5.0, 5.0, 5.0};
    require(operation_check_15, "semantic focus navigation must retain a visible accessibility cue");

    click(window, second);
    require(window.focused_control() == second && !window.focus_cue_visible() &&
                (*second).visual_outsets() == Insets{},
            "a later primary-pointer focus must return to pointer modality");
    const bool operation_check_16 = window.request_focus(first);
    require(operation_check_16, "programmatic focus must preserve the current modality deterministically");
    const bool operation_check_17 = window.focused_control() == first;
    require(operation_check_17, "programmatic focus must preserve the current modality deterministically");
    const bool operation_check_18 = !window.focus_cue_visible();
    require(operation_check_18, "programmatic focus must preserve the current modality deterministically");
    const bool operation_check_19 = (*first).visual_outsets() == Insets{};
    require(operation_check_19, "programmatic focus must preserve the current modality deterministically");
}

void test_button_authored_state_recipes_are_owned_and_retained() {
    const Color normal_color = Color::rgba(21, 42, 63);
    const Color hot_color = Color::rgba(31, 62, 93);
    const Color pressed_color = Color::rgba(11, 32, 53);
    const Color disabled_color = Color::rgba(91, 102, 113);
    ControlVisualRecipe recipe_values[control_surface_state_count];
    for (std::size_t index = 0; index < control_surface_state_count; ++index) {
        recipe_values[index].material = solid_material(normal_color);
        recipe_values[index].focus_ring = Color::rgba(40, 125, 155);
        recipe_values[index].focus_width = 2.0;
        recipe_values[index].focus_offset = 3.0;
        recipe_values[index].authored_focus_outline = true;
        recipe_values[index].default_width = 0.0;
        recipe_values[index].visual_offset = {};
        recipe_values[index].pressed_content_offset = {};
    }
    recipe_values[static_cast<std::size_t>(ControlSurfaceState::hot)].material =
        solid_material(hot_color);
    recipe_values[static_cast<std::size_t>(ControlSurfaceState::pressed)].material =
        solid_material(pressed_color);
    recipe_values[static_cast<std::size_t>(ControlSurfaceState::pressed)].visual_offset =
        {0.0, 1.0};
    recipe_values[static_cast<std::size_t>(ControlSurfaceState::disabled)].material =
        solid_material(disabled_color);

    const ControlStateRecipes recipes = ControlStateRecipes::from_parts(
        recipe_values, control_surface_state_count);
    recipe_values[static_cast<std::size_t>(ControlSurfaceState::normal)].material =
        solid_material(Color::rgba(255, 0, 0));

    std::shared_ptr<gui_forms::Button> button = make_control<Button>(
        StableId("button.authored-state-recipes"), "Authored");
    (*button).set_requested_bounds({0.0, 0.0, 120.0, 32.0});
    (*button).set_visual_recipes(recipes);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(button, Size{120.0, 32.0});
    Window& window = *held_window;
    window.perform_layout();

    RecordingPainter normal_painter{};
    (*button).on_paint(normal_painter, (*button).absolute_bounds());
    require(normal_painter.last_fill_color == normal_color,
            "Button must own its authored normal-state material");

    (*button).on_focus_changed(true);
    RecordingPainter focus_painter{};
    (*button).on_paint(focus_painter, (*button).absolute_bounds());
    require(focus_painter.last_rounded_stroke_bounds ==
                Rect{-4.0, -4.0, 128.0, 40.0} &&
                focus_painter.last_rounded_stroke_width == 2.0 &&
                (*button).visual_outsets() == Insets{5.0, 5.0, 5.0, 5.0},
            "authored outline offset must paint and invalidate outside the button box");
    (*button).on_focus_changed(false);

    ControlStateRecipes inset_recipes = recipes;
    for (ControlVisualRecipe& recipe : inset_recipes.values) {
        recipe.focus_offset = -3.0;
    }
    (*button).set_visual_recipes(inset_recipes);
    (*button).on_focus_changed(true);
    RecordingPainter inset_focus_painter{};
    (*button).on_paint(inset_focus_painter, (*button).absolute_bounds());
    require(inset_focus_painter.last_rounded_stroke_bounds ==
                Rect{2.0, 2.0, 116.0, 28.0} &&
                (*button).visual_outsets() == Insets{},
            "negative authored outline offset must remain inside the button box");
    (*button).on_focus_changed(false);
    (*button).set_visual_recipes(recipes);

    PointerEvent enter{};
    enter.action = PointerAction::enter;
    (*button).on_pointer(enter);
    RecordingPainter hot_painter{};
    (*button).on_paint(hot_painter, (*button).absolute_bounds());
    require(hot_painter.last_fill_color == hot_color,
            "Button hover must resolve the authored hot-state material");

    PointerEvent down{};
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = center(button);
    (*button).on_pointer(down);
    RecordingPainter pressed_painter{};
    (*button).on_paint(pressed_painter, (*button).absolute_bounds());
    require(pressed_painter.last_fill_color == pressed_color,
            "Button press must resolve the authored pressed-state material");
    require(pressed_painter.last_fill_bounds.y == 1.0 &&
                (*button).visual_outsets().bottom == 1.0,
            "authored pressed transform must move the whole visual without changing layout");

    (*button).enabled = false;
    RecordingPainter disabled_painter{};
    (*button).on_paint(disabled_painter, (*button).absolute_bounds());
    require(disabled_painter.last_fill_color == disabled_color,
            "Button disabled state must resolve the authored disabled material");

    bool rejected_count = false;
    try {
        static_cast<void>(ControlStateRecipes::from_parts(
            recipe_values, control_surface_state_count - 1U));
    } catch (const std::invalid_argument&) {
        rejected_count = true;
    }
    require(rejected_count,
            "authored state recipes must reject a partial retained state set");

    recipe_values[0].material.corner_radius = -1.0;
    bool rejected_invalid = false;
    try {
        static_cast<void>(ControlStateRecipes::from_parts(
            recipe_values, control_surface_state_count));
    } catch (const std::invalid_argument&) {
        rejected_invalid = true;
    }
    require(rejected_invalid,
            "authored state recipes must reject an invalid surface atomically");
}

void test_nested_authored_surfaces_are_control_background_layers() {
    const Color root_color = Color::rgba(18, 37, 52);
    const Color child_color = Color::rgba(232, 241, 244);
    std::shared_ptr<Control> root = make_control<Control>(
        StableId("authored-surface"));
    std::shared_ptr<Control> child = make_control<Control>(
        StableId("authored-surface.child"));
    (*root).set_authored_surface_material(solid_material(root_color));
    (*child).set_requested_bounds({10.0, 10.0, 60.0, 24.0});
    (*root).add_child(child);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{100.0, 60.0});
    Window& window = *held_window;
    window.perform_layout();

    RecordingPainter reveal_painter{};
    window.paint(reveal_painter, {0.0, 0.0, 100.0, 60.0});
    require(std::count(reveal_painter.fill_colors.begin(),
                       reveal_painter.fill_colors.end(), root_color) == 1 &&
                std::count(reveal_painter.fill_colors.begin(),
                           reveal_painter.fill_colors.end(), child_color) == 0,
            "a child without an authored surface must reveal its retained parent surface");

    (*child).set_authored_surface_material(solid_material(child_color));
    RecordingPainter owned_painter{};
    window.paint(owned_painter, {0.0, 0.0, 100.0, 60.0});
    require(std::count(owned_painter.fill_colors.begin(),
                       owned_painter.fill_colors.end(), root_color) == 1 &&
                std::count(owned_painter.fill_colors.begin(),
                           owned_painter.fill_colors.end(), child_color) == 1,
            "a child authored surface must paint as its background before child content");

    (*child).clear_authored_surface_material();
    SurfaceMaterial invalid = solid_material(child_color);
    invalid.corner_radius = -1.0;
    bool rejected = false;
    try {
        (*child).set_authored_surface_material(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    RecordingPainter restored_reveal_painter{};
    window.paint(restored_reveal_painter, {0.0, 0.0, 100.0, 60.0});
    require(rejected && !(*child).authored_surface_material() &&
                std::count(restored_reveal_painter.fill_colors.begin(),
                           restored_reveal_painter.fill_colors.end(),
                           root_color) == 1 &&
                std::count(restored_reveal_painter.fill_colors.begin(),
                           restored_reveal_painter.fill_colors.end(),
                           child_color) == 0,
            "invalid child surfaces must fail atomically and restore parent reveal");
}

// A control's own background colour (WinForms' BackColor) paints its
// bounds before its content; a child that sets none inherits it without
// painting it again; forced high contrast drops both.
void test_control_back_color_paints_background_once() {
    const Color root_color = Color::rgba(250, 236, 200);
    const Color child_color = Color::rgba(210, 236, 250);
    std::shared_ptr<Control> root = make_control<Control>(StableId("back-color"));
    std::shared_ptr<Control> child = make_control<Control>(StableId("back-color.child"));
    (*child).set_requested_bounds({10.0, 10.0, 60.0, 24.0});
    (*root).add_child(child);
    (*root).back_color = root_color;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{100.0, 60.0});
    Window& window = *held_window;
    window.perform_layout();

    RecordingPainter inherited_painter{};
    window.paint(inherited_painter, {0.0, 0.0, 100.0, 60.0});
    require(std::count(inherited_painter.fill_colors.begin(), inherited_painter.fill_colors.end(), root_color) == 1 &&
                (*child).ambient_back_color() == root_color,
            "an inherited background must paint once, on the control that sets it");

    (*child).back_color = child_color;
    RecordingPainter own_painter{};
    window.paint(own_painter, {0.0, 0.0, 100.0, 60.0});
    require(std::count(own_painter.fill_colors.begin(), own_painter.fill_colors.end(), root_color) == 1 &&
                std::count(own_painter.fill_colors.begin(), own_painter.fill_colors.end(), child_color) == 1,
            "a child's own background must paint over its bounds");

    PresentationSettings settings = window.presentation_settings();
    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    RecordingPainter contrast_painter{};
    window.paint(contrast_painter, {0.0, 0.0, 100.0, 60.0});
    require(std::count(contrast_painter.fill_colors.begin(), contrast_painter.fill_colors.end(), root_color) == 0 &&
                std::count(contrast_painter.fill_colors.begin(), contrast_painter.fill_colors.end(), child_color) == 0,
            "forced high contrast must drop authored background colours");
}

void test_mnemonics_and_dialog_buttons_are_retained_commands() {
    const MnemonicText escaped = parse_mnemonic_text("Save && E&xit");
    require(escaped.display_text == "Save & Exit" &&
                escaped.mnemonic == U'x' && is_mnemonic(U'X', "E&xit") &&
                is_mnemonic(U'a', "&Save &As") &&
                !is_mnemonic(U'&', "&&&") &&
                !is_mnemonic(U'e', "Save && Exit"),
            "mnemonic parsing must collapse escaped ampersands and ASCII-fold the marked scalar");

    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("dialog.root"));
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("dialog.label"), "&Name");
    std::shared_ptr<FocusSink> field =
        make_control<FocusSink>(StableId("dialog.field"));
    std::shared_ptr<gui_forms::Button> run = make_control<Button>(StableId("dialog.run"), "&Run");
    std::shared_ptr<gui_forms::Button> run_duplicate = make_control<Button>(
        StableId("dialog.run.duplicate"), "&Retry");
    std::shared_ptr<gui_forms::Button> accept = make_control<Button>(StableId("dialog.accept"), "&Apply");
    std::shared_ptr<gui_forms::Button> cancel = make_control<Button>(StableId("dialog.cancel"), "&Cancel");
    (*label).set_requested_bounds({4.0, 4.0, 60.0, 24.0});
    (*field).set_requested_bounds({70.0, 4.0, 90.0, 24.0});
    (*run).set_requested_bounds({4.0, 36.0, 70.0, 28.0});
    (*run_duplicate).set_requested_bounds({4.0, 68.0, 70.0, 28.0});
    (*accept).set_requested_bounds({80.0, 36.0, 70.0, 28.0});
    (*cancel).set_requested_bounds({156.0, 36.0, 70.0, 28.0});
    (*cancel).causes_validation = false;
    (*root).add_child(label);
    (*root).add_child(field);
    (*root).add_child(run);
    (*root).add_child(run_duplicate);
    (*root).add_child(accept);
    (*root).add_child(cancel);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{240.0, 104.0});
    Window& window = *held_window;
    window.perform_layout();

    std::uint64_t run_clicks{};
    std::uint64_t duplicate_clicks{};
    std::uint64_t accept_clicks{};
    std::uint64_t cancel_clicks{};
    SubscriptionToken run_token = (*run).clicked().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ButtonBase&>(run_clicks));
    SubscriptionToken duplicate_token = (*run_duplicate).clicked().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ButtonBase&>(duplicate_clicks));
    SubscriptionToken accept_token = (*accept).clicked().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ButtonBase&>(accept_clicks));
    SubscriptionToken cancel_token = (*cancel).clicked().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ButtonBase&>(cancel_clicks));

    window.set_accept_button(accept);
    window.set_cancel_button(cancel);
    require((*accept).default_button() && window.accept_button() == accept &&
                window.cancel_button() == cancel &&
                (*cancel).dialog_result() == DialogResult::cancel,
            "assigning a Window accept button must retain the target and publish its default cue");
    RecordingPainter mnemonic_painter{};
    (*run).on_paint(mnemonic_painter, (*run).client_rectangle());
    require(std::find(mnemonic_painter.texts.begin(), mnemonic_painter.texts.end(),
                      "Run") != mnemonic_painter.texts.end() &&
                (*run).semantic_descriptor().name == "Run" &&
                (*label).semantic_descriptor().name == "Name",
            "mnemonic markers must not leak into Button or Label paint and semantics");

    KeyEvent mnemonic{};
    mnemonic.action = KeyAction::down;
    mnemonic.physical_key = PhysicalKey::r;
    mnemonic.modifiers = Modifier::alt;
    (*run).causes_validation = false;
    const bool operation_check_20 = window.dispatch_key(mnemonic);
    require(operation_check_20, "Alt plus a marked character must invoke the first eligible retained command");
    const bool operation_check_21 = run_clicks == 1U;
    require(operation_check_21, "Alt plus a marked character must invoke the first eligible retained command");
    (*run_duplicate).causes_validation = false;
    const bool operation_check_22 = window.dispatch_key(mnemonic);
    require(operation_check_22, "duplicate mnemonics must advance through one stable retained arbitration ring");
    const bool operation_check_23 = duplicate_clicks == 1U;
    require(operation_check_23, "duplicate mnemonics must advance through one stable retained arbitration ring");
    const bool operation_check_24 = window.dispatch_key(mnemonic);
    require(operation_check_24, "duplicate mnemonics must advance through one stable retained arbitration ring");
    const bool operation_check_25 = run_clicks == 2U;
    require(operation_check_25, "duplicate mnemonics must advance through one stable retained arbitration ring");

    mnemonic.physical_key = PhysicalKey::n;
    const bool operation_check_26 = window.dispatch_key(mnemonic);
    require(operation_check_26, "a Label mnemonic must focus the next selectable retained control");
    const bool operation_check_27 = window.focused_control() == field;
    require(operation_check_27, "a Label mnemonic must focus the next selectable retained control");

    SubscriptionToken rejecting =
        (*field).validating().subscribe(CancelValidation());
    (*accept).set_dialog_result(DialogResult::yes);
    std::string dialog_trace{};
    SubscriptionToken accept_order = (*accept).clicked().subscribe(
        callbacks::AppendLiteral<ButtonBase&>(dialog_trace, "click\n"));
    SubscriptionToken cancel_order = (*cancel).clicked().subscribe(
        callbacks::AppendLiteral<ButtonBase&>(dialog_trace, "cancel-click\n"));
    SubscriptionToken result_order =
        window.dialog_result_changed().subscribe(TraceDialogResult(dialog_trace));
    KeyEvent enter{};
    enter.action = KeyAction::down;
    enter.physical_key = PhysicalKey::enter;
    const bool operation_check_28 = window.dispatch_key(enter);
    require(operation_check_28, "a rejected default command must be consumed without activation or focus theft");
    const bool operation_check_29 = accept_clicks == 0U;
    require(operation_check_29, "a rejected default command must be consumed without activation or focus theft");
    const bool operation_check_30 = window.focused_control() == field;
    require(operation_check_30, "a rejected default command must be consumed without activation or focus theft");
    (*accept).causes_validation = false;
    const bool operation_check_31 = window.dispatch_key(enter);
    require(operation_check_31, "CausesValidation false must run Click before publishing the retained dialog result");
    const bool operation_check_32 = accept_clicks == 1U;
    require(operation_check_32, "CausesValidation false must run Click before publishing the retained dialog result");
    const bool operation_check_33 = window.focused_control() == field;
    require(operation_check_33, "CausesValidation false must run Click before publishing the retained dialog result");
    const bool operation_check_34 = window.dialog_result() == DialogResult::yes;
    require(operation_check_34, "CausesValidation false must run Click before publishing the retained dialog result");
    const bool operation_check_35 = dialog_trace == "click\nresult:6\n";
    require(operation_check_35, "CausesValidation false must run Click before publishing the retained dialog result");

    window.set_dialog_result(DialogResult::none);
    dialog_trace.clear();
    KeyEvent escape = enter;
    escape.physical_key = PhysicalKey::escape;
    const bool operation_check_36 = window.dispatch_key(escape);
    require(operation_check_36, "Escape must invoke the retained cancel command and publish Cancel after Click");
    const bool operation_check_37 = cancel_clicks == 1U;
    require(operation_check_37, "Escape must invoke the retained cancel command and publish Cancel after Click");
    const bool operation_check_38 = window.dialog_result() == DialogResult::cancel;
    require(operation_check_38, "Escape must invoke the retained cancel command and publish Cancel after Click");
    const bool operation_check_39 = dialog_trace == "cancel-click\nresult:2\n";
    require(operation_check_39, "Escape must invoke the retained cancel command and publish Cancel after Click");

    bool invalid_result_rejected{};
    try {
        (*accept).set_dialog_result(static_cast<DialogResult>(8));
    } catch (const std::invalid_argument&) {
        invalid_result_rejected = true;
    }
    require(invalid_result_rejected,
            "DialogResult mutation must reject numeric gaps in the WinForms enum");

    std::shared_ptr<gui_forms::Panel> scope = make_control<Panel>(StableId("dialog.scope"));
    std::shared_ptr<FocusSink> scope_field =
        make_control<FocusSink>(StableId("dialog.scope.field"));
    (*scope).set_requested_bounds({228.0, 4.0, 10.0, 60.0});
    (*scope_field).set_requested_bounds({0.0, 0.0, 10.0, 10.0});
    (*scope).add_child(scope_field);
    (*root).add_child(scope);
    const FocusScopeId scope_id = window.begin_focus_scope(scope, scope_field);
    mnemonic.physical_key = PhysicalKey::r;
    const bool operation_check_40 = !window.dispatch_key(mnemonic);
    require(operation_check_40, "an active contained focus scope must suppress outer mnemonics and default commands");
    const bool operation_check_41 = run_clicks == 2U;
    require(operation_check_41, "an active contained focus scope must suppress outer mnemonics and default commands");
    const bool operation_check_42 = !window.dispatch_key(enter);
    require(operation_check_42, "an active contained focus scope must suppress outer mnemonics and default commands");
    const bool operation_check_43 = accept_clicks == 1U;
    require(operation_check_43, "an active contained focus scope must suppress outer mnemonics and default commands");
    require(window.end_focus_scope(scope_id),
            "dialog command focus-scope fixture must close deterministically");

    std::shared_ptr<gui_forms::Button> replacement = make_control<Button>(StableId("dialog.replacement"), "Other");
    (*replacement).set_requested_bounds({4.0, 68.0, 70.0, 10.0});
    (*root).add_child(replacement);
    window.set_accept_button(replacement);
    require(!(*accept).default_button() && (*replacement).default_button(),
            "moving the accept role must revoke the old default cue exactly once");
    (*replacement).dispose();
    require(!window.accept_button(),
            "disposing a dialog target must synchronously clear its retained Window role");

    window.set_dialog_result(DialogResult::none);
    (*accept).set_dialog_result(DialogResult::yes);
    SubscriptionToken replace_result_during_click =
        (*accept).clicked().subscribe(ReplaceButtonDialogResult(accept));
    const bool operation_check_44 = (*accept).perform_click();
    require(operation_check_44, "Button must publish the post-Click DialogResult chosen by application code");
    const bool operation_check_45 = window.dialog_result() == DialogResult::no;
    require(operation_check_45, "Button must publish the post-Click DialogResult chosen by application code");

    const DialogKeySnapshot snapshot = window.dialog_key_snapshot();
    require(snapshot.mnemonic_attempts == 5U && snapshot.mnemonics_handled == 4U &&
                snapshot.accept_attempts == 3U && snapshot.accept_handled == 1U &&
                snapshot.cancel_attempts == 1U && snapshot.cancel_handled == 1U &&
                snapshot.command_rejections == 1U &&
                snapshot.mnemonic_candidates == 7U &&
                snapshot.mnemonic_collisions == 3U &&
                snapshot.mnemonic_cycles == 2U,
            "dialog-key instrumentation must distinguish attempts, activations, and validation rejection");
}

void test_checkbox_state_and_click_order() {
    std::shared_ptr<gui_forms::CheckBox> check = make_control<CheckBox>(StableId("check"), "Precise");
    (*check).set_requested_bounds({0.0, 0.0, 140.0, 24.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(check, Size{140.0, 24.0});
    Window& window = *held_window;
    std::string order{};
    SubscriptionToken state =
        (*check).check_state_changed().subscribe(TraceCheckState(order));
    SubscriptionToken checked = (*check).checked_changed().subscribe(
        TraceBoolean(order, "checked:true\n", "checked:false\n"));
    SubscriptionToken clicked = (*check).clicked().subscribe(
        callbacks::AppendLiteral<ButtonBase&>(order, "click\n"));

    click(window, check);
    require((*check).checked() &&
                order == "state:checked\nchecked:true\nclick\n",
            "CheckBox must mutate, publish state, publish checked, then click");

    (*check).set_three_state(true);
    order.clear();
    (*check).on_activate();
    require((*check).check_state() == CheckState::indeterminate &&
                order == "state:other\nchecked:false\nclick\n",
            "three-state CheckBox must deterministically advance to indeterminate");
    order.clear();
    (*check).on_activate();
    require((*check).check_state() == CheckState::unchecked &&
                order == "state:other\nclick\n",
            "indeterminate CheckBox must return to unchecked without duplicate bool change");
}

void test_radio_group_scope_and_order() {
    std::shared_ptr<gui_forms::GroupBox> group = make_control<GroupBox>(StableId("radio.group"), "Mode");
    (*group).set_requested_bounds({0.0, 0.0, 360.0, 70.0});
    std::shared_ptr<gui_forms::RadioButton> first = make_control<RadioButton>(StableId("radio.first"), "First");
    std::shared_ptr<gui_forms::RadioButton> second = make_control<RadioButton>(StableId("radio.second"), "Second");
    std::shared_ptr<gui_forms::RadioButton> independent = make_control<RadioButton>(StableId("radio.other"), "Other group");
    (*first).set_requested_bounds({10.0, 24.0, 100.0, 24.0});
    (*second).set_requested_bounds({120.0, 24.0, 100.0, 24.0});
    (*independent).set_requested_bounds({230.0, 24.0, 120.0, 24.0});
    (*first).set_group_name("mode");
    (*second).set_group_name("mode");
    (*independent).set_group_name("independent");
    (*group).add_child(first);
    (*group).add_child(second);
    (*group).add_child(independent);
    (*first).set_checked(true);
    (*independent).set_checked(true);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(group, Size{360.0, 70.0});
    Window& window = *held_window;

    std::string order{};
    SubscriptionToken first_changed = (*first).checked_changed().subscribe(
        TraceBoolean(order, "first:on\n", "first:off\n"));
    SubscriptionToken second_changed = (*second).checked_changed().subscribe(
        TraceBoolean(order, "second:on\n", "second:off\n"));
    SubscriptionToken second_clicked = (*second).clicked().subscribe(
        callbacks::AppendLiteral<ButtonBase&>(order, "second:click\n"));
    click(window, second);
    require(!(*first).checked() && (*second).checked() && (*independent).checked() &&
                order == "first:off\nsecond:on\nsecond:click\n",
            "RadioButton must uncheck its group peer before checking/clicking itself");
}

// The colour LinkLabel's text takes when it paints.
Color painted_link_color(LinkLabel& link) {
    RecordingPainter painter;
    link.on_paint(painter, {});
    require(painter.text_colors.size() == 1U, "LinkLabel must draw its text once");
    const Color color = painter.text_colors.front();
    return color;
}

// The link families (owner decision W-Q5): LinkLabel's text takes the link
// family's colour, or visited_link's once visited, from its theme; the
// default theme keeps the compatibility style's link colours, and a style
// set on the control keeps its own.
void test_link_families() {
    std::shared_ptr<gui_forms::LinkLabel> link = make_control<LinkLabel>(StableId("link.family"), "Open");
    (*link).set_requested_bounds({0.0, 0.0, 120.0, 24.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(link, Size{120.0, 24.0});
    const BasicControlStyle defaults{};
    require(painted_link_color(*link) == defaults.link, "an unvisited link must keep the default link colour");
    (*link).visited = true;
    require(painted_link_color(*link) == defaults.visited_link,
            "a visited link must keep the default visited colour");
    (*link).enabled = false;
    // A disabled link is a disabled label part: the theme's own disabled text,
    // the text of its family in the disabled state (owner ruling, 2026-11-02).
    AnatomyStateSet disabled_states;
    disabled_states.insert(AnatomyState::disabled);
    const ControlVisualContext disabled_context = part_visual_context(*link, disabled_states);
    const Color disabled_ink =
        (*link).effective_theme().resolve(ControlVisualRole::visited_link, disabled_context).text;
    require(painted_link_color(*link) == disabled_ink, "a disabled link must take the theme's disabled ink");
    (*link).enabled = true;

    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "link-families";
    constexpr Color themed_link = Color::rgba(0, 102, 51);
    constexpr Color themed_visited = Color::rgba(128, 0, 64);
    const std::size_t normal = static_cast<std::size_t>(ControlSurfaceState::normal);
    definition.roles[static_cast<std::size_t>(ControlVisualRole::link)].ordinary[normal].text = themed_link;
    definition.roles[static_cast<std::size_t>(ControlVisualRole::visited_link)].ordinary[normal].text =
        themed_visited;
    (*link).set_theme_override(Theme::create(definition));
    require(painted_link_color(*link) == themed_visited, "a visited link must take the visited_link family's text");
    (*link).visited = false;
    require(painted_link_color(*link) == themed_link, "an unvisited link must take the link family's text");

    BasicControlStyle compatibility;
    compatibility.link = Color::rgba(10, 20, 30);
    (*link).set_style(compatibility);
    require(painted_link_color(*link) == compatibility.link,
            "a style set on the link must keep its own link colour");
}

void test_link_and_callback_disposal() {
    std::shared_ptr<gui_forms::LinkLabel> link = make_control<LinkLabel>(StableId("link"), "Show details");
    (*link).set_requested_bounds({0.0, 0.0, 120.0, 24.0});
    const std::unique_ptr<Window> held_link_window = std::make_unique<Window>(link, Size{120.0, 24.0});
    Window& link_window = *held_link_window;
    std::uint64_t clicks = 0;
    SubscriptionToken link_click = (*link).clicked().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ButtonBase&>(clicks));
    click(link_window, link);
    require((*link).visited.read() && clicks == 1,
            "LinkLabel must mark visited before publishing activation");

    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("dispose.root"));
    std::shared_ptr<gui_forms::CheckBox> check = make_control<CheckBox>(StableId("dispose.check"), "Dispose safely");
    (*check).set_requested_bounds({0.0, 0.0, 140.0, 24.0});
    (*root).add_child(check);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{140.0, 24.0});
    Window& window = *held_window;
    std::uint64_t later_callbacks = 0;
    SubscriptionToken dispose_on_state = (*check).check_state_changed().subscribe(
        callbacks::DisposeCapturedControl<CheckBox, CheckState>(check));
    SubscriptionToken checked = (*check).checked_changed().subscribe(
        callbacks::IncrementCounter<std::uint64_t, bool>(later_callbacks));
    SubscriptionToken clicked = (*check).clicked().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ButtonBase&>(later_callbacks));
    (*check).on_activate();
    require(!(*check).is_alive() && later_callbacks == 0 &&
                !window.find("dispose.check"),
            "state callback disposal must suppress later checked/click callbacks safely");
}

void test_wrong_thread_property_mutation_is_rejected() {
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("thread.label"), "Before");
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(label, Size{100.0, 24.0});
    Window& window = *held_window;
    bool rejected = false;
    std::thread worker(attempt_label_mutation, std::cref(label),
                       std::ref(rejected));
    worker.join();
    require(rejected && (*label).text.read() == "Before",
            "reusable control properties must retain core UI-thread enforcement");
}

void test_fixed_label_text_is_paint_only() {
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("label.telemetry"), "0 kB/s");
    (*label).set_requested_bounds({0.0, 0.0, 120.0, 24.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(label, Size{120.0, 24.0});
    Window& window = *held_window;
    window.perform_layout();
    RecordingPainter painter{};
    window.paint(painter, {0.0, 0.0, 120.0, 24.0});
    window.reset_activity_metrics();

    (*label).text = "999 kB/s";
    const DamageRegion damage = window.take_damage();
    require(!damage.empty(), "fixed label text must request repaint");
    window.paint(painter, damage.bounds());
    const MetricsSnapshot fixed = window.metrics_snapshot();
    require(fixed.measure_passes == 0U && fixed.arrange_passes == 0U &&
                fixed.paint_passes == 1U,
            "fixed label text must repaint without remeasuring its ancestors");

    (*label).auto_size = true;
    window.perform_layout();
    window.reset_activity_metrics();
    (*label).text = "content-sized telemetry";
    const DamageRegion auto_damage = window.take_damage();
    require(!auto_damage.empty(), "auto-sized label text must request repaint");
    window.paint(painter, auto_damage.bounds());
    const MetricsSnapshot sized = window.metrics_snapshot();
    require(sized.measure_passes > 0U && sized.arrange_passes > 0U,
            "auto-sized label text must continue to participate in layout");
}

class CountingLabelMetrics final : public TextMetricsProvider {
public:
    ResolvedTextLayout resolve_text_layout_utf8(
        const std::string_view text, const FontSpec font) override {
        ++calls;
        const ResolvedTextLayout result = estimate_text_layout_utf8(text, font);
        return result;
    }

    std::size_t calls{};
};

void test_fixed_label_measurement_does_not_resolve_unused_text() {
    // The provider outlives the Window's non-owning registration.
    CountingLabelMetrics provider{};
    const std::string source(65536U, 'x');
    const std::shared_ptr<Label> label = make_control<Label>(
        StableId("label.fixed-measurement"), source);
    (*label).set_requested_bounds({0.0, 0.0, 190.0, 108.0});
    (*label).text_wrapping = TextWrapping::word;
    (*label).maximum_lines = 7U;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(label, Size{190.0, 108.0});
    Window& window = *held_window;
    window.set_text_metrics_provider(&provider);
    provider.calls = 0U;
    const Size complete = (*label).measure({300.0, 200.0});
    const Size constrained = (*label).measure({80.0, 40.0});
    const Size empty = (*label).measure({0.0, 0.0});
    require(complete == Size{190.0, 108.0} && constrained == Size{80.0, 40.0} &&
                empty == Size{},
            "fixed label measurement must retain requested and available size semantics");
    require(provider.calls == 0U,
            "fixed label layout must not resolve text whose metrics cannot affect either dimension");

    // AutoSize does not change Label's existing explicit-bounds precedence.
    (*label).auto_size = true;
    window.set_text_scale(1.5);
    provider.calls = 0U;
    const Size scaled = (*label).measure({300.0, 200.0});
    require(scaled == complete && provider.calls == 0U,
            "explicit label dimensions remain authoritative with AutoSize and text scaling");

    (*label).text = "one two three four";
    (*label).set_requested_bounds({0.0, 0.0, 80.0, 0.0});
    provider.calls = 0U;
    const Size content_height = (*label).measure({300.0, 200.0});
    require(content_height.width == 80.0 && content_height.height > 0.0 && provider.calls > 0U,
            "content-derived label height must still resolve text");
    (*label).set_requested_bounds({0.0, 0.0, 0.0, 40.0});
    provider.calls = 0U;
    const Size content_width = (*label).measure({300.0, 200.0});
    require(content_width.width > 0.0 && content_width.height == 40.0 && provider.calls > 0U,
            "content-derived label width must still resolve text");
}

void test_label_multiline_wrapping_and_alignment() {
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("label.multiline"),
                                     "Retained labels wrap words\nand preserve breaks");
    (*label).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*label).text_wrapping = TextWrapping::word;
    (*label).vertical_alignment = VerticalAlignment::near;
    (*label).alignment = HorizontalAlignment::far;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(label, Size{120.0, 80.0});
    Window& window = *held_window;
    RecordingPainter painter{};
    window.paint(painter, {0.0, 0.0, 120.0, 80.0});
    require(painter.texts.size() == 4U && painter.texts.front() == "Retained labels" &&
                painter.texts[2] == "and preserve" && painter.texts.back() == "breaks",
            "Label must word-wrap and preserve explicit line breaks");
    require(painter.text_origins.front().y < painter.text_origins.back().y &&
                painter.text_origins.front().x > 2.0,
            "Label multiline alignment must position each line independently");

    bool rejected = false;
    try {
        (*label).line_spacing = 0.1;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*label).line_spacing.read() == 1.25,
            "Label must reject invalid line spacing without mutation");

    (*label).text = "画布工具可以使用键盘调整坐标并绘制连续线条";
    RecordingPainter cjk{};
    window.paint(cjk, {0.0, 0.0, 120.0, 80.0});
    require(cjk.texts.size() > 1, "unspaced CJK paragraphs wrap instead of overflowing");
    std::string joined{};
    for (const std::string& line : cjk.texts) { joined += line; }
    require(joined == (*label).text.read(), "CJK wrapping preserves every UTF-8 character");

    (*label).text = "Visual source";
    (*label).text_wrapping = TextWrapping::no_wrap;
    (*label).text_case_transform = TextCaseTransform::uppercase_ascii;
    RecordingPainter transformed_painter{};
    window.paint(transformed_painter, {0.0, 0.0, 120.0, 80.0});
    require(transformed_painter.texts.size() == 1U &&
                transformed_painter.texts[0] == "VISUAL SOURCE" &&
                (*label).semantic_descriptor().name == "Visual source",
            "Label text transforms must change retained display without rewriting semantic source text");
}

struct LabelGraphemeWidth final {
    double operator()(const std::string_view text) const {
        const TextStore store(text);
        const GraphemeIndex count = store.grapheme_count();
        const double width = static_cast<double>(count.value()) * 10.0;
        return width;
    }
};

struct CountingGraphemeWidth final {
    std::size_t& calls;
    double advance{};

    double operator()(const std::string_view text) const {
        ++calls;
        const TextStore store(text);
        const GraphemeIndex count = store.grapheme_count();
        const double width = static_cast<double>(count.value()) * advance;
        return width;
    }
};

void test_label_wrapping_reuses_widths_only_within_the_current_call() {
    const FontSpec font{};
    std::string source{};
    source.reserve(4096U);
    for (std::size_t index = 0U; index < 256U; ++index) { source.append("one two three x "); }
    std::size_t calls{};
    const TextWidthResolver first{CountingGraphemeWidth{calls, 10.0}};
    const std::vector<std::string> lines = label_lines(
        source, font, 90.0, TextWrapping::word, first, 7U);
    const std::vector<std::string> expected{
        "one two", "three x", "one two", "three x", "one two", "three x", "one two"};
    require(lines == expected, "reused widths must preserve the exact wrapped lines");
    require(calls <= 10U, "repeated short candidates must not be reshaped for each visible line");

    // A new invocation can use a different font/provider. It must not inherit
    // widths from the previous call, including for identical source bytes.
    calls = 0U;
    const TextWidthResolver second{CountingGraphemeWidth{calls, 20.0}};
    const std::vector<std::string> changed = label_lines(
        source, font, 90.0, TextWrapping::word, second, 7U);
    const std::vector<std::string> expected_changed{"one", "two", "thre", "e x", "one", "two", "thre"};
    require(changed == expected_changed && calls > 0U,
            "width reuse must not survive a new resolver invocation");

    // More distinct keys than the finite cache can retain, followed by a
    // repeated old key, exercise eviction without making it a content limit.
    std::string distinct{};
    distinct.reserve(512U);
    std::vector<std::string> expected_distinct{};
    expected_distinct.reserve(81U);
    for (std::size_t index = 0U; index < 80U; ++index) {
        const std::string number = std::to_string(index);
        std::string word("w");
        word.append(number);
        distinct.append(word);
        distinct.push_back(' ');
        expected_distinct.push_back(std::move(word));
    }
    distinct.append("w0");
    expected_distinct.emplace_back("w0");
    const std::vector<std::string> evicted = label_lines(
        distinct, font, 30.0, TextWrapping::word, first);
    require(evicted == expected_distinct,
            "evicting remembered widths must preserve unlimited distinct-word coverage");

    const std::string boundary(128U, 'a');
    const std::string beyond(129U, 'a');
    std::string long_source = boundary;
    long_source.push_back(' ');
    long_source.append(beyond);
    const std::vector<std::string> long_lines = label_lines(
        long_source, font, 1280.0, TextWrapping::word, first);
    const std::vector<std::string> expected_long{boundary, boundary, "a"};
    require(long_lines == expected_long,
            "long cache keys must fall through without clipping source or splitting graphemes differently");
}

void test_label_wrapping_reuses_live_text_without_retaining_paragraph_storage() {
    const FontSpec font{};
    const TextWidthResolver resolve_width{LabelGraphemeWidth{}};
    std::string padded(65536U, ' ');
    padded.reserve(131076U);
    padded.append("\tx\t");
    padded.append(65536U, ' ');
    const std::vector<std::string> compact =
        label_lines(padded, font, 30.0, TextWrapping::word, resolve_width);
    require(compact.size() == 1U && compact[0] == "x",
            "whitespace-heavy wrapped paragraph must retain only its visible word");
    // A broad capacity ceiling detects exporting the 128 KiB paragraph scratch;
    // it does not depend on a particular library's small-string capacity.
    require(compact[0].capacity() < 1024U,
            "short final label line must not own paragraph-sized scratch storage");

    const std::vector<std::string> repeated = label_lines(
        "ab abcdef\t  abcdef  z", font, 30.0, TextWrapping::word, resolve_width);
    const std::vector<std::string> expected_words{"ab", "abc", "def", "abc", "def", "z"};
    require(repeated == expected_words,
            "long-word wrapping must restore the prior line before publishing and reuse it for later words");

    // Each word has five graphemes: combining a, joined astronaut, b, c, d.
    const std::string combining = "a\xCC\x81";
    const std::string astronaut = "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x9A\x80";
    std::string first_line = combining;
    first_line.append(astronaut);
    first_line.push_back('b');
    std::string source = first_line;
    source.append("cd ");
    source.append(first_line);
    source.append("cd");
    const std::vector<std::string> grapheme_lines =
        label_lines(source, font, 30.0, TextWrapping::word, resolve_width);
    const std::vector<std::string> expected_graphemes{first_line, "cd", first_line, "cd"};
    require(grapheme_lines == expected_graphemes,
            "repeated long words must preserve combining and joined-emoji graphemes at wrap boundaries");

    const std::vector<std::string> explicit_breaks = label_lines(
        "  ab\t cd  \n\n  ef  ", font, 30.0, TextWrapping::word, resolve_width);
    const std::vector<std::string> expected_breaks{"ab", "cd", "", "ef"};
    require(explicit_breaks == expected_breaks,
            "scratch reuse must preserve explicit empty paragraphs and normalized word spacing");
}

void test_label_line_limit_bounds_hidden_wrapping_work() {
    std::string source{};
    source.reserve(65536U);
    for (std::size_t index = 0U; index < 4096U; ++index) source.append("one two three x ");
    const std::shared_ptr<Label> label = make_control<Label>(StableId("label.preview-cost"), source);
    (*label).set_requested_bounds({0.0, 0.0, 190.0, 108.0});
    (*label).font = FontSpec{FontRole::content, 13.0, 400, false};
    (*label).text_wrapping = TextWrapping::word;
    (*label).maximum_lines = 7U;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(label, Size{190.0, 108.0});
    Window& window = *held_window;
    window.perform_layout();
    RecordingPainter painter{};
    (*label).on_paint(painter, {0.0, 0.0, 190.0, 108.0});
    std::cout << "label preview cost: bytes=" << source.size()
              << " drawn_lines=" << painter.texts.size()
              << " width_queries=" << painter.text_measurements
              << " queried_bytes=" << painter.measured_text_bytes << '\n';
    require(painter.texts.size() == 7U, "line-limited preview must paint seven lines");
    require(painter.text_measurements < 200U && painter.measured_text_bytes < 4096U,
        "seven-line preview must stop measuring hidden words after its visible prefix");
}

void test_label_line_limit_preserves_unlimited_prefix() {
    const std::array<std::string_view, 8> inputs{
        "", "\n\nlast\n", "  ab\t cd  \n\n  ef  ", "ab abcdef abcdef z",
        "画布工具可以使用键盘调整坐标并绘制连续线条",
        "a\xCC\x81" "bcdef a\xCC\x81" "bcdef",
        "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x9A\x80" "abcd end",
        "one\r\ntwo\r\nthree"};
    const std::array<double, 3> widths{0.0, 30.0, 120.0};
    const std::array<TextWrapping, 2> modes{TextWrapping::no_wrap, TextWrapping::word};
    const FontSpec font{};
    const TextWidthResolver resolver{LabelGraphemeWidth{}};
    for (const TextWrapping mode : modes) {
        for (const double width : widths) {
            for (const std::string_view input : inputs) {
                const std::vector<std::string> complete = label_lines(input, font, width, mode, resolver);
                for (std::size_t limit = 1U; limit <= complete.size() + 1U; ++limit) {
                    std::vector<std::string> expected = complete;
                    expected.resize(std::min(limit, complete.size()));
                    const std::vector<std::string> bounded = label_lines(input, font, width, mode, resolver, limit);
                    require(bounded == expected,
                        "line limit must preserve the exact unlimited prefix including graphemes and empty paragraphs");
                }
            }
        }
    }
}

void test_owner_decoration_is_retained_and_owner_relative() {
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(
        StableId("decoration.owner"), "Owner");
    const MaterialBorder edge{Color::rgba(141U, 214U, 223U), 1.0};
    OwnerDecorationRecipe before_recipe{};
    before_recipe.layer = OwnerDecorationLayer::before_content;
    before_recipe.size = {10.0, 10.0};
    before_recipe.top = 14.0;
    before_recipe.left = 10.0;
    before_recipe.rotation_degrees = 45.0;
    before_recipe.border_edges = MaterialBorderEdges::from_parts(
        &edge, &edge, nullptr, nullptr);
    OwnerDecorationRecipe after_recipe = before_recipe;
    after_recipe.layer = OwnerDecorationLayer::after_content;
    after_recipe.left.reset();
    after_recipe.right = 10.0;
    const OwnerDecorationRecipe recipes[]{before_recipe, after_recipe};
    (*button).set_owned_decorations(recipes, 2U);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(button, Size{80.0, 40.0});
    Window& window = *held_window;
    RecordingPainter first{};
    window.paint(first, {0.0, 0.0, 80.0, 40.0});
    const std::vector<char> expected_order{'L', 'L', 'T', 'L', 'L'};
    require(first.lines == 4U && (*button).owned_decorations().size() == 2U &&
                first.text_line_order == expected_order,
            "owner decorations must paint in distinct before-content and after-content layers");

    window.resize({120.0, 40.0});
    RecordingPainter second{};
    window.paint(second, {0.0, 0.0, 120.0, 40.0});
    require(second.lines == 4U &&
                std::abs(second.line_starts[2].x -
                         first.line_starts[2].x - 40.0) < 0.000001,
            "right-anchored owner decoration must relax against live owner width");

    OwnerDecorationRecipe invalid = after_recipe;
    invalid.right.reset();
    bool rejected{};
    try {
        (*button).set_owned_decorations(&invalid, 1U);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*button).owned_decorations().size() == 2U,
            "invalid owner decoration geometry must be rejected atomically");
}

void test_basic_control_layout_customization_is_bounded_and_atomic() {
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("label.maximum-lines"),
                                     "one two three four five six seven eight");
    (*label).text_wrapping = TextWrapping::word;
    const Size unrestricted = (*label).measure({72.0, 200.0});
    (*label).maximum_lines = 2U;
    const Size limited = (*label).measure({72.0, 200.0});
    require((*label).maximum_lines.read() == 2U && limited.height < unrestricted.height,
            "Label MaximumLines must bound content-sized desired height");
    (*label).set_requested_bounds({0.0, 0.0, 72.0, 80.0});
    const std::unique_ptr<Window> held_label_window = std::make_unique<Window>(label, Size{72.0, 80.0});
    Window& label_window = *held_label_window;
    RecordingPainter label_painter{};
    label_window.paint(label_painter, {0.0, 0.0, 72.0, 80.0});
    require(label_painter.texts.size() == 2U,
            "Label MaximumLines must bound actual retained painting");

    bool rejected_lines{};
    try {
        (*label).maximum_lines = 4097U;
    } catch (const std::out_of_range&) {
        rejected_lines = true;
    }
    bool rejected_alignment{};
    try {
        (*label).alignment = static_cast<HorizontalAlignment>(255);
    } catch (const std::invalid_argument&) {
        rejected_alignment = true;
    }
    require(rejected_lines && rejected_alignment &&
                (*label).maximum_lines.read() == 2U &&
                (*label).alignment.read() == HorizontalAlignment::near,
            "Label bounded and enum properties must reject invalid values atomically");

    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("button.content-padding"), "Run");
    const Size default_size = (*button).measure({300.0, 200.0});
    (*button).content_padding = {20.0, 14.0, 20.0, 14.0};
    const Size padded_size = (*button).measure({300.0, 200.0});
    require(padded_size.width > default_size.width &&
                padded_size.height > default_size.height,
            "ButtonBase ContentPadding must participate in desired size");
    bool rejected_padding{};
    try {
        (*button).content_padding = {20.0, 14.0, 129.0, 14.0};
    } catch (const std::invalid_argument&) {
        rejected_padding = true;
    }
    require(rejected_padding &&
                (*button).content_padding.read() == Insets{20.0, 14.0, 20.0, 14.0},
            "ButtonBase ContentPadding must reject invalid insets atomically");

    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("panel.border-validation"));
    bool rejected_border{};
    try {
        (*panel).border_style = static_cast<BorderStyle>(255);
    } catch (const std::invalid_argument&) {
        rejected_border = true;
    }
    require(rejected_border && (*panel).border_style.read() == BorderStyle::none,
            "Panel must reject invalid border-style states atomically");
}

void test_label_inherits_theme_typography_until_explicitly_overridden() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "label-type-roles";
    definition.structure.typography.heading =
        {FontRole::control, 17.0, 650, false, 0.31};
    definition.structure.typography.title =
        {FontRole::control, 23.0, 700, false, 0.19};
    constexpr Color themed_text = Color::rgba(21, 47, 73);
    definition.roles[static_cast<std::size_t>(ControlVisualRole::panel)]
        .ordinary[static_cast<std::size_t>(ControlSurfaceState::normal)].text =
        themed_text;
    std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId("label.theme"), "Heading");
    (*label).set_theme_override(Theme::create(definition));
    (*label).text_style_role = TextStyleRole::heading;
    require(!(*label).font.read().has_value() &&
                (*label).resolved_font() == FontSpec{FontRole::control, 17.0, 650, false,
                                          0.31} &&
                !(*label).fore_color.read().has_value() &&
                (*label).foreground() == themed_text,
            "Label must resolve typography and text color from inherited theme roles");

    const FontSpec explicit_font{FontRole::content, 11.0, 500, true};
    (*label).font = explicit_font;
    (*label).text_style_role = TextStyleRole::title;
    require((*label).font.read().has_value() && (*label).resolved_font() == explicit_font,
            "explicit Label font must remain authoritative across role changes");
    (*label).font = std::nullopt;
    require(!(*label).font.read().has_value() &&
                (*label).resolved_font() == FontSpec{FontRole::control, 23.0, 700, false,
                                          0.19},
            "clearing Label font must restore inherited role typography");

    constexpr Color explicit_text = Color::rgba(91, 37, 19);
    (*label).fore_color = explicit_text;
    require((*label).foreground() == explicit_text,
            "explicit Label foreground must override theme text");
    (*label).fore_color = std::nullopt;
    require((*label).foreground() == themed_text,
            "clearing Label foreground must restore inherited theme text");
}

void test_picture_box_modes_registry_and_semantics() {
    std::shared_ptr<gui_forms::PictureBox> picture = make_control<PictureBox>(StableId("picture"));
    (*picture).set_requested_bounds({0.0, 0.0, 100.0, 100.0});
    (*picture).border_style = BorderStyle::line;
    (*picture).accessible_name = "Professional test card";
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(picture, Size{100.0, 100.0});
    Window& window = *held_window;
    const std::vector<std::byte> pixels(4U * 2U * 4U, std::byte{0xff});
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(
        4U, 2U, 16U, pixels);
    require(static_cast<bool>(loaded),
            "PictureBox test image must enter the window-owned registry");
    std::uint64_t changes{};
    SubscriptionToken changed = (*picture).image_changed().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ImageId>(changes));
    (*picture).image = loaded.image;
    window.perform_layout();
    require(changes == 1U && (*picture).has_valid_image() &&
                (*picture).image_size() == Size{4.0, 2.0},
            "PictureBox must publish an image change and resolve registry metadata");

    const std::array<std::pair<PictureBoxSizeMode, Rect>, 4> modes{{
        {PictureBoxSizeMode::normal, {1.0, 1.0, 4.0, 2.0}},
        {PictureBoxSizeMode::stretch_image, {1.0, 1.0, 98.0, 98.0}},
        {PictureBoxSizeMode::center_image, {48.0, 49.0, 4.0, 2.0}},
        {PictureBoxSizeMode::zoom, {1.0, 25.5, 98.0, 49.0}},
    }};
    for (const std::pair<PictureBoxSizeMode, Rect>& mode_and_bounds : modes) {
        (*picture).size_mode = mode_and_bounds.first;
        require((*picture).image_bounds() == mode_and_bounds.second,
                "PictureBox sizing mode must compute deterministic image geometry");
        RecordingPainter painter{};
        window.paint(painter, {0.0, 0.0, 100.0, 100.0});
        require(painter.images.size() == 1U &&
                    painter.images.front() == loaded.image &&
                    painter.image_destinations.front() == mode_and_bounds.second,
                "PictureBox must submit the resolved image and destination to Painter");
    }

    (*picture).size_mode = PictureBoxSizeMode::auto_size;
    require((*picture).measure({1000.0, 1000.0}) == Size{6.0, 4.0} &&
                (*picture).image_bounds() == Rect{1.0, 1.0, 4.0, 2.0},
            "PictureBox AutoSize must measure to intrinsic pixels plus its border");
    (*picture).image_opacity = 0.42;
    RecordingPainter opacity_painter{};
    window.paint(opacity_painter, {0.0, 0.0, 100.0, 100.0});
    require(opacity_painter.image_opacities.size() == 1U &&
                opacity_painter.image_opacities.front() == 0.42,
            "PictureBox must preserve renderer-neutral image opacity");

    const SemanticSnapshot semantics = window.semantic_snapshot();
    const std::string json = semantics.to_json();
    require(json.find("\"role\":\"image\"") != std::string::npos &&
                json.find("Professional test card") != std::string::npos &&
                json.find("4 x 2") != std::string::npos,
            "PictureBox must expose stable image semantics and intrinsic dimensions");

    bool rejected{};
    try {
        (*picture).image_opacity = 1.1;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*picture).image_opacity.read() == 0.42,
            "PictureBox must reject invalid opacity without mutation");
    require(window.remove_image(loaded.image) && !(*picture).has_valid_image(),
            "PictureBox must reject a removed generational image ID safely");
    RecordingPainter stale_painter{};
    window.paint(stale_painter, {0.0, 0.0, 100.0, 100.0});
    require(stale_painter.images.empty(),
            "PictureBox must never submit a stale ImageId to a renderer");
}

// The value of the node with `stable_id` in `nodes` or below them; empty
// (and `found` false) when no node has it.
std::string semantic_value_of(const std::vector<SemanticNode>& nodes, const std::string_view stable_id, bool& found) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) {
            found = true;
            return node.value;
        }
        std::string below = semantic_value_of(node.children, stable_id, found);
        if (found) return below;
    }
    return std::string{};
}

void test_public_drawing_metrics_and_control_tag() {
    std::weak_ptr<int> released_anchor{};
    {
        std::shared_ptr<gui_forms::Panel> tagged = make_control<Panel>(StableId("controls.tagged"));
        std::shared_ptr<int> anchor = std::make_shared<int>(42);
        released_anchor = anchor;
        (*tagged).set_tag(anchor);
        anchor.reset();
        const std::shared_ptr<int>* retained =
            std::any_cast<std::shared_ptr<int>>(&(*tagged).tag());
        require(retained != nullptr && **retained == 42,
                "Control Tag must retain exact type-erased application metadata");
        (*tagged).dispose();
    }
    require(released_anchor.expired(),
            "control disposal must release Tag-owned application lifetime");

    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("controls.diagnostics.root"));
    std::shared_ptr<gui_forms::DrawingSurface> drawing = make_control<DrawingSurface>(
        StableId("controls.diagnostics.drawing"));
    (*drawing).set_requested_bounds({0.0, 0.0, 120.0, 60.0});
    (*drawing).accessible_name = "Public owner drawing";
    (*drawing).background = Color::rgba(8, 9, 10);
    std::uint64_t callback_count = 0U;
    Rect callback_bounds{};
    Rect callback_damage{};
    (*drawing).set_paint_callback(
        RecordDrawingPaint(callback_count, callback_bounds, callback_damage));
    std::shared_ptr<gui_forms::MetricsView> metrics = make_control<MetricsView>(
        StableId("controls.diagnostics.metrics"), "Runtime proof");
    (*metrics).accent_width = 9.0;
    (*metrics).set_requested_bounds({0.0, 60.0, 240.0, 90.0});
    (*root).add_child(drawing);
    (*root).add_child(metrics);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{240.0, 150.0});
    Window& window = *held_window;
    window.perform_layout();
    RecordingPainter painter{};
    window.paint(painter, {0.0, 0.0, 240.0, 150.0});
    require(callback_count == 1U &&
                callback_bounds == Rect{0.0, 0.0, 120.0, 60.0} &&
                !callback_damage.empty() &&
                painter.fills + painter.gradients >= 4U,
            "DrawingSurface must invoke public owner paint with retained bounds and damage");
    require(!(*drawing).hit_test_visible.read() &&
                !(*drawing).hit_test_local({10.0, 10.0}) &&
                (*drawing).background.read() == Color::rgba(8, 9, 10) &&
                (*drawing).semantic_descriptor().role == SemanticRole::image &&
                (*drawing).semantic_descriptor().exposed,
            "DrawingSurface must default to input-transparent image semantics");
    const SemanticDescriptor metrics_semantics = (*metrics).semantic_descriptor();
    require(metrics_semantics.role == SemanticRole::group &&
                metrics_semantics.name == "Runtime proof" &&
                metrics_semantics.value.find("controls") != std::string::npos &&
                (*metrics).accent_width.read() == 9.0,
            "MetricsView must expose the structured runtime snapshot semantically");

    // The accessible value is the painted text, and a paint that changes it
    // advances the semantic generation: hosts keep a published tree until
    // the generation changes, so a value that moved without it would reach
    // them late (the macOS AX client saw "41 paints" against "42").
    const std::string_view metrics_id = "controls.diagnostics.metrics";
    const std::uint64_t generation_before = window.semantic_generation();
    bool found_before = false;
    const std::string value_before = semantic_value_of(window.semantic_snapshot().roots, metrics_id, found_before);
    bool found_again = false;
    const std::string value_again = semantic_value_of(window.semantic_snapshot().roots, metrics_id, found_again);
    require(found_before && found_again && value_again == value_before && !value_before.empty() &&
                window.semantic_generation() == generation_before,
            "MetricsView's published value must not move without a paint");
    (*metrics).invalidate(Dirty::paint);
    window.paint(painter, {0.0, 0.0, 240.0, 150.0});
    bool found_after = false;
    const std::string value_after = semantic_value_of(window.semantic_snapshot().roots, metrics_id, found_after);
    require(found_after && value_after != value_before && window.semantic_generation() != generation_before,
            "a paint that changes MetricsView's counters must advance the semantic generation");
    require(!window.needs_frame(),
            "MetricsView's semantic invalidation must not owe another paint");
}

void test_button_disclosure_semantics() {
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("button.disclosure"), "Options");
    (*button).expanded_state = false;
    SemanticDescriptor collapsed = (*button).semantic_descriptor();
    require(!has_semantic_state(collapsed.states, SemanticState::expanded) &&
                std::find(collapsed.actions.begin(), collapsed.actions.end(),
                          SemanticAction::expand) != collapsed.actions.end(),
            "collapsed disclosure button must publish an expand action");
    std::size_t activations{};
    SubscriptionToken clicked = (*button).clicked().subscribe(
        callbacks::IncrementCounter<std::size_t, ButtonBase&>(activations));
    const bool operation_check_46 = (*button).on_semantic_action(SemanticAction::expand, {});
    require(operation_check_46, "semantic expand must route through the button's shared activation");
    const bool operation_check_47 = activations == 1U;
    require(operation_check_47, "semantic expand must route through the button's shared activation");
    (*button).expanded_state = true;
    const SemanticDescriptor expanded = (*button).semantic_descriptor();
    require(has_semantic_state(expanded.states, SemanticState::expanded) &&
                std::find(expanded.actions.begin(), expanded.actions.end(),
                          SemanticAction::collapse) != expanded.actions.end(),
            "expanded disclosure button must publish state and collapse action");
}

void test_button_selected_state() {
    std::shared_ptr<Button> button = make_control<Button>(StableId("button.selected"), "Selected tab");
    require(!(*button).selected() &&
                !has_semantic_state((*button).semantic_descriptor().states,
                                    SemanticState::selected),
            "ordinary Button must begin without selected state");
    (*button).set_selected(true);
    require((*button).selected() &&
                has_semantic_state((*button).semantic_descriptor().states,
                                   SemanticState::selected),
            "retained Button selected state must publish through semantics");
}

void test_drop_down_button_routes_pointer_keyboard_and_semantics() {
    std::shared_ptr<Button> ordinary = make_control<Button>(StableId("button.drop-down.measure-control"),
                                         "View");
    const Size ordinary_desired = (*ordinary).measure({400.0, 100.0});
    std::shared_ptr<DropDownButton> auto_sized = make_control<DropDownButton>(
        StableId("button.drop-down.auto-size"), "View",
        DropDownButtonMode::menu);
    const Size drop_down_desired = (*auto_sized).measure({400.0, 100.0});
    require(drop_down_desired.width == ordinary_desired.width + 16.0,
            "auto-sized drop-down must reserve its disclosure width exactly once");

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("button.drop-down.root"));
    std::shared_ptr<DropDownButton> menu = make_control<DropDownButton>(
        StableId("button.drop-down.menu"), "View",
        DropDownButtonMode::menu);
    std::shared_ptr<DropDownButton> split = make_control<DropDownButton>(
        StableId("button.drop-down.split"), "Refresh",
        DropDownButtonMode::split);
    (*menu).set_requested_bounds({10.0, 10.0, 100.0, 32.0});
    (*split).set_requested_bounds({120.0, 10.0, 120.0, 32.0});
    (*root).add_child(menu);
    (*root).add_child(split);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{250.0, 52.0});
    Window& window = *held_window;
    window.perform_layout();

    std::size_t menu_requests{};
    std::size_t split_requests{};
    std::size_t split_clicks{};
    SubscriptionToken menu_requested = (*menu).drop_down_requested().subscribe(
        callbacks::IncrementCounter<std::size_t, DropDownButton&>(menu_requests));
    SubscriptionToken split_requested = (*split).drop_down_requested().subscribe(
        callbacks::IncrementCounter<std::size_t, DropDownButton&>(split_requests));
    SubscriptionToken split_clicked = (*split).clicked().subscribe(
        callbacks::IncrementCounter<std::size_t, ButtonBase&>(split_clicks));

    click(window, menu);
    require(menu_requests == 1U,
            "menu-mode drop-down button must route ordinary activation to disclosure");

    const Rect split_bounds = (*split).absolute_bounds();
    const Point primary{split_bounds.x + 12.0,
                        split_bounds.y + split_bounds.height * 0.5};
    const bool operation_check_48 = window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, primary});
    require(operation_check_48, "split drop-down primary region must preserve the ordinary Click path");
    const bool operation_check_49 = window.dispatch_pointer(
                {PointerAction::up, PointerButton::primary, primary});
    require(operation_check_49, "split drop-down primary region must preserve the ordinary Click path");
    const bool operation_check_50 = split_clicks == 1U;
    require(operation_check_50, "split drop-down primary region must preserve the ordinary Click path");
    const bool operation_check_51 = split_requests == 0U;
    require(operation_check_51, "split drop-down primary region must preserve the ordinary Click path");
    const Point disclosure{split_bounds.x + split_bounds.width - 3.0,
                           split_bounds.y + split_bounds.height * 0.5};
    const bool operation_check_52 = window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, disclosure});
    require(operation_check_52, "split drop-down trailing region must request its menu without primary Click");
    const bool operation_check_53 = window.dispatch_pointer(
                {PointerAction::up, PointerButton::primary, disclosure});
    require(operation_check_53, "split drop-down trailing region must request its menu without primary Click");
    const bool operation_check_54 = split_clicks == 1U;
    require(operation_check_54, "split drop-down trailing region must request its menu without primary Click");
    const bool operation_check_55 = split_requests == 1U;
    require(operation_check_55, "split drop-down trailing region must request its menu without primary Click");
    const bool operation_check_56 = window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, primary});
    require(operation_check_56, "split activation ownership must follow the pressed region rather than release drift");
    const bool operation_check_57 = window.dispatch_pointer(
                {PointerAction::up, PointerButton::primary, disclosure});
    require(operation_check_57, "split activation ownership must follow the pressed region rather than release drift");
    const bool operation_check_58 = split_clicks == 2U;
    require(operation_check_58, "split activation ownership must follow the pressed region rather than release drift");
    const bool operation_check_59 = split_requests == 1U;
    require(operation_check_59, "split activation ownership must follow the pressed region rather than release drift");

    const bool operation_check_60 = window.request_focus(menu);
    require(operation_check_60, "Alt+Down must request the focused drop-down menu");
    const bool operation_check_61 = window.dispatch_key({KeyAction::down, PhysicalKey::down,
                                     Modifier::alt});
    require(operation_check_61, "Alt+Down must request the focused drop-down menu");
    const bool operation_check_62 = menu_requests == 2U;
    require(operation_check_62, "Alt+Down must request the focused drop-down menu");
    const bool operation_check_63 = (*menu).on_semantic_action(SemanticAction::show_menu, {});
    require(operation_check_63, "semantic show-menu must share the validated disclosure route");
    const bool operation_check_64 = menu_requests == 3U;
    require(operation_check_64, "semantic show-menu must share the validated disclosure route");

    (*menu).set_drop_down_open(true);
    const SemanticDescriptor open = (*menu).semantic_descriptor();
    require(has_semantic_state(open.states, SemanticState::expanded) &&
                std::find(open.actions.begin(), open.actions.end(),
                          SemanticAction::show_menu) != open.actions.end() &&
                std::find(open.actions.begin(), open.actions.end(),
                          SemanticAction::collapse) != open.actions.end(),
            "open drop-down button must expose menu, expanded, and collapse semantics");
    std::size_t close_requests{};
    SubscriptionToken close_requested = (*menu).drop_down_close_requested().subscribe(
        callbacks::IncrementCounter<std::size_t, DropDownButton&>(close_requests));
    const bool operation_check_65 = (*menu).on_semantic_action(SemanticAction::collapse, {});
    require(operation_check_65, "semantic collapse must request closure from the overlay owner");
    const bool operation_check_66 = close_requests == 1U;
    require(operation_check_66, "semantic collapse must request closure from the overlay owner");

    RecordingPainter painter{};
    (*menu).on_paint(painter, (*menu).absolute_bounds());
    require(painter.lines >= 1U && painter.fills >= 4U,
            "open drop-down paint includes a filled indicator and bounded open edge");

    (*menu).enabled = false;
    const bool operation_check_67 = !(*menu).perform_drop_down();
    require(operation_check_67, "disabled drop-down button must retain discoverability without invocation");
    const bool operation_check_68 = menu_requests == 3U;
    require(operation_check_68, "disabled drop-down button must retain discoverability without invocation");

    bool width_rejected{};
    try {
        (*split).set_drop_down_width(8.0);
    } catch (const std::invalid_argument&) {
        width_rejected = true;
    }
    require(width_rejected && (*split).drop_down_width() == 16.0,
            "drop-down width must reject invalid geometry without mutation");
}

void test_bottom_disclosure_and_multiline_button_content() {
    std::shared_ptr<DropDownButton> button = make_control<DropDownButton>(StableId("ribbon.paste"), "Paste", DropDownButtonMode::split);
    (*button).text = "Clipboard image";
    (*button).font = {FontRole::control,24,400,false};
    const Size right_size = (*button).measure({500, 500});
    (*button).set_drop_down_edge(DropDownButtonEdge::bottom);
    const Size bottom_size = (*button).measure({500, 500});
    require(bottom_size.width == right_size.width - 16 && bottom_size.height == right_size.height + 16,
        "bottom disclosure must move its content reservation to the vertical axis");
    (*button).font = {FontRole::control,12,400,false};
    (*button).text = "Paste";
    (*button).set_requested_bounds({0, 0, 64, 80});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(button, Size{64, 80});
    Window& window = *held_window;
    window.perform_layout();
    std::size_t clicks = 0, menus = 0;
    SubscriptionToken click_token = (*button).clicked().subscribe(callbacks::IncrementCounter<std::size_t, ButtonBase&>(clicks));
    SubscriptionToken menu_token = (*button).drop_down_requested().subscribe(callbacks::IncrementCounter<std::size_t, DropDownButton&>(menus));
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, {32, 25}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, {32, 25}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, {32, 75}}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, {32, 75}}));
    require(clicks == 1 && menus == 1, "bottom split region must preserve primary and menu command ownership");
    (*button).text = "Edit\ncolors";
    (*button).use_mnemonic = false;
    (*button).text_alignment = ContentAlignment::middle_center;
    RecordingPainter painter{};
    (*button).on_paint(painter, {0,0,64,80});
    require(painter.texts.size() == 2 && painter.texts[0] == "Edit" && painter.texts[1] == "colors" &&
        painter.text_origins[1].y > painter.text_origins[0].y && painter.text_origins[0].x > painter.text_origins[1].x,
        "multiline button must paint separately measured and centered lines above disclosure");
    require(painter.text_origins[1].y < 64, "multiline label must fit above the disclosure strip");
}

void test_image_list_state_density_ownership_and_button_layout() {
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("button.images"), "Launch");
    (*button).set_requested_bounds({0.0, 0.0, 150.0, 36.0});
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(button, Size{150.0, 36.0});
    Window& window = *held_window;
    std::shared_ptr<gui_forms::ImageList> images = std::make_shared<ImageList>(window, Size{16.0, 16.0});
    const std::span<const std::byte, 70U> png =
        std::as_bytes(std::span{one_pixel_png});
    const ImageId normal_1x = (*images).add_png("Action", png, 1.0).image;
    const ImageId normal_2x = (*images).add_png("action", png, 2.0).image;
    const ImageId hot_1x =
        (*images).set_variant_png("ACTION", ImageVisualState::hot, 1.0, png).image;
    const ImageId disabled_1x = (*images).set_variant_png(
        "Action", ImageVisualState::disabled, 1.0, png).image;
    require(normal_1x.value != 0U && normal_2x.value != 0U &&
                hot_1x.value != 0U && disabled_1x.value != 0U &&
                (*images).count() == 1U && (*images).contains_key("aCtIoN") &&
                window.image_resource_snapshot().resource_count == 4U,
            "ImageList must own one case-insensitive key with independent density/state variants");

    std::uint64_t changes{};
    SubscriptionToken changed = (*images).changed().subscribe(
        callbacks::IncrementCounter<std::uint64_t,
                                    const ImageListChange&>(changes));
    (*button).set_image_list(images);
    (*button).set_image_key("ACTION");
    (*button).text_image_relation = TextImageRelation::image_before_text;
    (*button).text_alignment = ContentAlignment::middle_center;
    window.perform_layout();

    RecordingPainter normal{};
    (*button).on_paint(normal, {0.0, 0.0, 150.0, 36.0});
    require(normal.images.size() == 1U && normal.images.front() == normal_1x &&
                normal.image_destinations.front().width == 16.0 &&
                normal.text_origins.front().x >
                    normal.image_destinations.front().x + 16.0,
            "Button must lay out keyed ImageList content before text using logical image size");

    PointerEvent enter{};
    enter.action = PointerAction::enter;
    (*button).on_pointer(enter);
    RecordingPainter hot{};
    (*button).on_paint(hot, {0.0, 0.0, 150.0, 36.0});
    require(hot.images.size() == 1U && hot.images.front() == hot_1x,
            "Button hover must resolve a real ImageList hot-state raster");

    PointerEvent leave{};
    leave.action = PointerAction::leave;
    (*button).on_pointer(leave);
    window.set_scale(2.0);
    RecordingPainter dense{};
    (*button).on_paint(dense, {0.0, 0.0, 150.0, 36.0});
    require(dense.images.size() == 1U && dense.images.front() == normal_2x &&
                dense.image_destinations.front().width == 16.0,
            "ImageList must select a 2x source without changing logical Button geometry");

    (*button).enabled = false;
    window.set_scale(1.0);
    RecordingPainter disabled{};
    (*button).on_paint(disabled, {0.0, 0.0, 150.0, 36.0});
    require(disabled.images.size() == 1U &&
                disabled.images.front() == disabled_1x &&
                disabled.image_opacities.front() == 1.0,
            "Button disabled state must prefer an authored disabled raster over opacity fallback");

    std::array<std::byte, 8> malformed{};
    const std::uint64_t revision_before = (*images).revision();
    const ImageLoadResult rejected = (*images).set_variant_png(
        "Action", ImageVisualState::hot, 1.0, malformed);
    require(!rejected && (*images).revision() == revision_before &&
                (*images).resolve("action", ImageVisualState::hot, 1.0).image == hot_1x,
            "failed ImageList replacement must leave the prior variant and revision intact");

    (*images).set_key_name(0U, "Launch");
    require(changes == 1U && (*images).contains_key("LAUNCH") &&
                !(*images).contains_key("Action"),
            "ImageList key rename must be atomic, case-insensitive, and tokenized");

    std::shared_ptr<gui_forms::Panel> other_root = make_control<Panel>(StableId("button.images.other"));
    const std::unique_ptr<Window> held_other_window = std::make_unique<Window>(other_root, Size{10.0, 10.0});
    Window& other_window = *held_other_window;
    std::shared_ptr<gui_forms::ImageList> foreign = std::make_shared<ImageList>(other_window);
    bool rejected_foreign{};
    try {
        (*button).set_image_list(foreign);
    } catch (const std::invalid_argument&) {
        rejected_foreign = true;
    }
    require(rejected_foreign && (*button).image_list() == images,
            "Button must reject cross-Window ImageList assignment without mutation");

    (*button).set_image_key("Launch");
    (*images).dispose();
    require((*images).is_disposed() && (*images).empty() &&
                window.image_resource_snapshot().resource_count == 0U,
            "ImageList disposal must synchronously release every owned Window image");
}

} // namespace

void test_group_box_capability_hooks() {
    std::shared_ptr<UpperCaseGroupBox> group = make_control<UpperCaseGroupBox>(StableId("hooks.group"), "Mode");
    require((*group).text.read() == "Mode" && (*group).text_changes == 0,
            "construction stores the caption through GroupBox's own hooks");
    std::vector<std::string> captions;
    const SubscriptionToken caption_token = (*group).text_changed().subscribe(
        callbacks::PushBack<std::vector<std::string>, const std::string&>(captions));
    (*group).text = "options";
    require((*group).text.read() == "OPTIONS" && (*group).text_changes == 1 && captions.size() == 1U &&
                captions[0] == "OPTIONS",
            "coerce_text rewrites the caption; on_text_changed runs once and raises text_changed");
    (*group).text = "Options";
    require((*group).text_changes == 1 && captions.size() == 1U,
            "a write coerced to the stored caption changes nothing and raises nothing");
    (*group).set_property_value("text", BindingValue{std::string("table")});
    require((*group).text.read() == "TABLE" && (*group).text_changes == 2 && captions.size() == 2U,
            "a table write runs the same hooks");
    const std::optional<PropertyDescriptor> text_descriptor = (*group).property_descriptor("text");
    require(text_descriptor && (*text_descriptor).change_notifications,
            "GroupBox text reports change notifications (TextChanged)");
    static_cast<void>(caption_token);

    (*group).use_mnemonic = false;
    require(!(*group).use_mnemonic.read() && (*group).semantic_descriptor().access_key.empty(),
            "without use_mnemonic the caption names no access key");
    const FontSpec initial_font{FontRole::control, 12.0, 600, false, 0.24};
    bool refused = false;
    try {
        (*group).font = FontSpec{FontRole::control, 0.0, 600, false};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*group).font.read() == initial_font, "an invalid caption font is refused");

    std::shared_ptr<ScaledGroupBox> scaled =
        make_control<ScaledGroupBox>(StableId("hooks.scaled"), "Pad", Size{200.0, 100.0});
    require((*scaled).design_size.read() == Size{200.0, 100.0}, "the constructor's design size is stored");
    refused = false;
    try {
        (*scaled).design_size = Size{-1.0, 100.0};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*scaled).design_size.read() == Size{200.0, 100.0}, "a negative design size is refused");
    bool constructor_refused = false;
    try {
        static_cast<void>(make_control<ScaledPanel>(StableId("hooks.panel"), Size{0.0, 1.0}));
    } catch (const std::invalid_argument&) {
        constructor_refused = true;
    }
    require(constructor_refused, "a constructor's empty design size is refused");
}

void test_picture_box_property_capabilities() {
    std::shared_ptr<CountingPictureBox> picture = make_control<CountingPictureBox>(StableId("hooks.picture"));
    std::uint64_t changes{};
    const SubscriptionToken changed = (*picture).image_changed().subscribe(
        callbacks::IncrementCounter<std::uint64_t, ImageId>(changes));
    require(!(*picture).should_serialize_property("image"), "an empty image is not serialized");
    (*picture).image = ImageId{3U};
    require(changes == 1U && (*picture).image.read() == ImageId{3U} && (*picture).should_serialize_property("image"),
            "an image write raises image_changed and is serialized");
    (*picture).image = ImageId{3U};
    require(changes == 1U, "writing the current image raises nothing");
    require((*picture).reset_property("image") && (*picture).image.read() == ImageId{} && changes == 2U &&
                !(*picture).should_serialize_property("image"),
            "the table reset clears the image through clear_image");
    (*picture).set_property_value("image", BindingValue{ImageId{5U}});
    require((*picture).image.read() == ImageId{5U} && changes == 3U, "a table image write runs the same hooks");

    (*picture).size_mode = PictureBoxSizeMode::zoom;
    require((*picture).size_mode.read() == PictureBoxSizeMode::zoom && (*picture).size_mode_changes == 1,
            "a size mode write runs on_size_mode_changed");
    (*picture).size_mode = PictureBoxSizeMode::zoom;
    require((*picture).size_mode_changes == 1, "writing the current size mode runs nothing");

    (*picture).image_opacity = 0.5;
    bool refused = false;
    try {
        (*picture).image_opacity = 1.5;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*picture).image_opacity.read() == 0.5, "an opacity above one is refused");
    static_cast<void>(changed);
}

int main() {
    try {
        test_public_controls_render_with_role_policy();
        test_group_box_capability_hooks();
        test_button_pointer_and_keyboard_activation();
        test_button_focus_cue_tracks_input_modality();
        test_button_authored_state_recipes_are_owned_and_retained();
        test_control_back_color_paints_background_once();
        test_nested_authored_surfaces_are_control_background_layers();
        test_mnemonics_and_dialog_buttons_are_retained_commands();
        test_checkbox_state_and_click_order();
        test_radio_group_scope_and_order();
        test_link_and_callback_disposal();
        test_link_families();
        test_wrong_thread_property_mutation_is_rejected();
        test_fixed_label_text_is_paint_only();
        test_fixed_label_measurement_does_not_resolve_unused_text();
        test_label_multiline_wrapping_and_alignment();
        test_label_wrapping_reuses_widths_only_within_the_current_call();
        test_label_wrapping_reuses_live_text_without_retaining_paragraph_storage();
        test_label_line_limit_bounds_hidden_wrapping_work();
        test_label_line_limit_preserves_unlimited_prefix();
        test_owner_decoration_is_retained_and_owner_relative();
        test_basic_control_layout_customization_is_bounded_and_atomic();
        test_label_inherits_theme_typography_until_explicitly_overridden();
        test_picture_box_modes_registry_and_semantics();
        test_picture_box_property_capabilities();
        test_public_drawing_metrics_and_control_tag();
        test_button_disclosure_semantics();
        test_button_selected_state();
        test_drop_down_button_routes_pointer_keyboard_and_semantics();
        test_image_list_state_density_ownership_and_button_layout();
        test_bottom_disclosure_and_multiline_button_content();
        std::cout << "gui_forms_basic_controls_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_basic_controls_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
