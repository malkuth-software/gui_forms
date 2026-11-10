#include "gui_forms/gui_forms.hpp"
#include "support/professional_theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Copying a ThemeDefinition copies every role recipe (some 100 KB, held out
// of line), so it is passed by reference (theme_types.hpp). Theme::create
// must keep reading it so: when the recipes were inline, its by-value
// parameter put a 105 KB copy on every caller's frame, and with the
// sanitizer's unshared slots this test overflowed a 1 MB stack.
static_assert(std::is_same_v<decltype(&Theme::create),
                             std::shared_ptr<const Theme> (*)(const ThemeDefinition&)>,
              "Theme::create must take its definition by const reference");
// The role recipes are held out of line, so the definition itself stays a
// small value (568 bytes with clang64 on Windows x64; 105,392 with the
// recipes inline).
static_assert(sizeof(ThemeDefinition) <= 2048U, "ThemeDefinition must hold its role recipes out of line");

// The professional definition, held on the heap for a test to edit.
std::unique_ptr<ThemeDefinition> professional_definition() {
    std::unique_ptr<ThemeDefinition> definition(
        new ThemeDefinition(windows_professional_theme_definition()));
    return definition;
}

Color first_fill(const ControlVisualRecipe& recipe) {
    const MaterialFillLayer& fill = recipe.material.fills.front();
    return fill.kind == MaterialFillKind::solid ? fill.color
                                                : fill.stops.front().color;
}

class ThemePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color color) override {
        last_fill = color;
        fills.push_back(color);
    }
    void fill_rounded_rect(Rect, double, Color color) override {
        last_fill = color;
        fills.push_back(color);
    }
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color color, double) override {
        last_stroke = color;
    }
    void fill_linear_gradient(Rect, Point, Point,
                              std::span<const GradientStop> stops) override {
        ++gradients;
        first_gradient = stops.front().color;
        last_gradient = stops.back().color;
    }
    void fill_radial_gradient(Rect, Point, Size,
                              std::span<const GradientStop>) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color color) override {
        text = color;
    }
    void draw_image(ImageId, Rect, double) override {}

    unsigned gradients{};
    Color first_gradient{};
    Color last_gradient{};
    Color last_fill{};
    Color last_stroke{};
    Color text{};
    std::vector<Color> fills;
};

bool contains_color(const ThemePainter& painter, Color color) {
    return std::find(painter.fills.begin(), painter.fills.end(), color) !=
           painter.fills.end();
}

class ObserveCommittedTheme final {
public:
    explicit ObserveCommittedTheme(unsigned& changes) : changes_(changes) {}

    void operator()(const Theme& theme) const {
        require(theme.id() == "test-atomic",
                "theme event must observe the committed replacement");
        ++changes_;
    }

private:
    unsigned& changes_;
};

void set_solid_recipe(ThemeDefinition& definition, ControlVisualRole role,
                      ControlSurfaceState state, bool selected, Color color) {
    ControlRoleRecipes& recipes =
        definition.roles[static_cast<std::size_t>(role)];
    ControlVisualRecipe& recipe =
        (selected ? recipes.selected : recipes.ordinary)
            [static_cast<std::size_t>(state)];
    recipe.material.fills = {MaterialFillLayer::solid(color)};
}

void test_default_theme_has_complete_state_matrix() {
    const std::shared_ptr<const Theme> theme = default_theme();
    require(theme && (*theme).id() == "gui-forms" && theme == gui_forms_theme(),
            "the default theme is GUI.Forms' own (T4 phase 6)");
    for (std::size_t role = 0; role < control_visual_role_count; ++role) {
        for (std::size_t state = 0; state < control_surface_state_count; ++state) {
            for (bool selected : {false, true}) {
                for (bool high_contrast : {false, true}) {
                    ControlVisualContext context;
                    context.surface = static_cast<ControlSurfaceState>(state);
                    context.selected = selected;
                    context.high_contrast = high_contrast;
                    const ControlVisualRecipe& recipe = (*theme).resolve(
                        static_cast<ControlVisualRole>(role), context);
                    require(valid_surface_material(recipe.material),
                            "every role/state combination must own a valid material");
                }
            }
        }
    }
    ControlVisualContext normal;
    ControlVisualContext hot;
    hot.surface = ControlSurfaceState::hot;
    require(first_fill((*theme).resolve(ControlVisualRole::button, normal)) !=
                first_fill((*theme).resolve(ControlVisualRole::button, hot)),
            "button hot state must be visually distinct from normal");
    const ThemeStructureTokens& structure = (*theme).structure();
    require(structure.spacing.micro <= structure.spacing.section &&
                structure.geometry.splitter_width <=
                    structure.geometry.splitter_hit_width &&
                structure.typography.control.role == FontRole::control &&
                structure.typography.field.role == FontRole::content &&
                structure.motion.quick <= structure.motion.standard,
            "default theme must expose ordered renderer-neutral structural tokens");
}

void test_structural_tokens_are_validated_atomically() {
    std::unique_ptr<ThemeDefinition> invalid = professional_definition();
    (*invalid).id = "invalid-structure";
    (*invalid).structure.spacing.large = (*invalid).structure.spacing.xsmall;
    bool rejected{};
    try {
        static_cast<void>(Theme::create(*invalid));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "unordered structural spacing tokens must be rejected");

    invalid = professional_definition();
    (*invalid).structure.typography.title.size =
        std::numeric_limits<double>::quiet_NaN();
    rejected = false;
    try {
        static_cast<void>(Theme::create(*invalid));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected,
            "invalid structural typography must be rejected before publication");
}

// A text shadow (E9.6, CSS `text-shadow`) is recipe data: a theme carries
// it to the resolved recipe, and one past its bounds is refused whole.
void test_text_shadow_is_bounded_recipe_data() {
    ControlTextShadow shadow{};
    shadow.offset = Point{0.0, 1.0};
    shadow.blur_radius = 2.0;
    shadow.color = Color::rgba(0, 0, 0, 96);
    std::unique_ptr<ThemeDefinition> definition = professional_definition();
    (*definition).id = "text-shadow";
    ControlVisualRecipe& recipe =
        (*definition).roles[static_cast<std::size_t>(ControlVisualRole::button)]
            .ordinary[static_cast<std::size_t>(ControlSurfaceState::normal)];
    recipe.text_shadow = shadow;
    require(valid_control_visual_recipe(recipe), "a bounded text shadow is valid");
    const std::shared_ptr<const Theme> theme = Theme::create(*definition);
    const ControlVisualRecipe& resolved = (*theme).resolve(ControlVisualRole::button, ControlVisualContext{});
    require(resolved.text_shadow == std::optional<ControlTextShadow>{shadow},
            "the theme carries the shadow to the resolved recipe");

    ControlVisualRecipe blurred = recipe;
    (*blurred.text_shadow).blur_radius = maximum_text_shadow_blur + 1.0;
    require(!valid_control_visual_recipe(blurred), "a blur past its bound is refused");
    ControlVisualRecipe moved = recipe;
    (*moved.text_shadow).offset = Point{maximum_text_shadow_offset + 1.0, 0.0};
    require(!valid_control_visual_recipe(moved), "an offset past its bound is refused");
    ControlVisualRecipe unknown = recipe;
    (*unknown.text_shadow).offset = Point{std::numeric_limits<double>::quiet_NaN(), 0.0};
    require(!valid_control_visual_recipe(unknown), "a non-finite offset is refused");

    recipe.text_shadow = (*blurred.text_shadow);
    bool rejected{};
    try {
        static_cast<void>(Theme::create(*definition));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "a theme with an unbounded text shadow is refused");
}

void test_window_theme_replacement_and_control_inheritance_are_atomic() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("theme.root"));
    std::shared_ptr<gui_forms::Button> child = make_control<Button>(StableId("theme.child"), "Apply");
    (*child).set_requested_bounds({20.0, 20.0, 90.0, 28.0});
    (*root).add_child(child);
    Window window(root, {160.0, 80.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());

    std::unique_ptr<ThemeDefinition> custom_definition = professional_definition();
    (*custom_definition).id = "test-atomic";
    ControlVisualRecipe& custom_normal = (*custom_definition)
        .roles[static_cast<std::size_t>(ControlVisualRole::button)]
        .ordinary[static_cast<std::size_t>(ControlSurfaceState::normal)];
    custom_normal.material.fills = {
        MaterialFillLayer::solid(Color::rgba(10, 20, 30))};
    const std::shared_ptr<const Theme> custom =
        Theme::create(*custom_definition);

    unsigned changes{};
    SubscriptionToken token = window.theme_changed().subscribe(
        ObserveCommittedTheme(changes));
    window.set_theme(custom);
    window.set_theme(custom);
    require(changes == 1U && (*child).effective_theme().id() == "test-atomic" &&
                !window.take_damage().empty(),
            "window replacement must be atomic, silent on identity, and inherited");

    const std::shared_ptr<const Theme> local = test_support::professional_theme();
    (*root).set_theme_override(local);
    require((*child).effective_theme().id() == "windows-professional",
            "ancestor override must be inherited by descendants");
    (*root).clear_theme_override();
    require((*child).effective_theme().id() == "test-atomic",
            "clearing override must restore window inheritance");

    bool rejected{};
    try {
        window.set_theme({});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && window.theme().id() == "test-atomic" && changes == 1U,
            "null replacement must fail without changing the active theme");
}

void test_button_routes_hover_and_status_through_theme_recipes() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("theme.states.root"));
    std::shared_ptr<gui_forms::Button> button = make_control<Button>(StableId("theme.states.button"), "Run");
    (*button).set_requested_bounds({20.0, 20.0, 100.0, 30.0});
    (*button).default_button = true;
    (*root).add_child(button);
    Window window(root, {160.0, 80.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());

    ThemePainter painter;
    window.paint(painter, {0.0, 0.0, 160.0, 80.0});
    const Color normal = painter.first_gradient;

    PointerEvent move;
    move.action = PointerAction::move;
    move.position = {30.0, 30.0};
    static_cast<void>(window.dispatch_pointer(move));
    require((*button).hovered_visual(),
            "window hover routing must update themed button state");
    window.paint(painter, window.take_damage().bounds());
    require(painter.first_gradient != normal,
            "hover must select the hot theme recipe");

    (*button).set_visual_status(ControlVisualStatus::invalid);
    const ControlVisualContext invalid = (*button).visual_context(
        {.hovered = true, .defaulted = true});
    require(invalid.surface == ControlSurfaceState::invalid,
            "invalid state must take precedence over hover");
    (*button).enabled = false;
    require((*button).visual_context({.hovered = true}).surface == ControlSurfaceState::disabled,
            "disabled state must take precedence over validation state");
    (*button).enabled = true;
    (*button).set_visual_status(ControlVisualStatus::normal);
    window.set_active(false);
    require((*button).visual_context().surface == ControlSurfaceState::deactivated,
            "inactive windows must resolve deactivated recipes");

    PresentationSettings settings = window.presentation_settings();
    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    const ControlVisualContext high = (*button).visual_context();
    require(high.high_contrast,
            "presentation accommodation must select high-contrast recipes");
}

void test_editor_selection_menu_and_progress_roles_reach_stock_controls() {
    constexpr Color editor_color = Color::rgba(17, 31, 47);
    constexpr Color selection_color = Color::rgba(53, 79, 101);
    constexpr Color menu_color = Color::rgba(71, 97, 119);
    constexpr Color progress_track = Color::rgba(89, 113, 137);
    constexpr Color progress_fill = Color::rgba(107, 131, 157);
    std::unique_ptr<ThemeDefinition> held_definition = professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "stock-role-routing";
    set_solid_recipe(definition, ControlVisualRole::editor,
                     ControlSurfaceState::normal, false, editor_color);
    set_solid_recipe(definition, ControlVisualRole::selection,
                     ControlSurfaceState::normal, true, selection_color);
    // A hot menu item rises in the built-in dithered bulge (owner: the soft
    // dither is the menu hover); a selected item keeps the plate, which
    // draws the family's selected record.
    set_solid_recipe(definition, ControlVisualRole::menu_item,
                     ControlSurfaceState::normal, true, menu_color);
    set_solid_recipe(definition, ControlVisualRole::menu_item,
                     ControlSurfaceState::hot, true, menu_color);
    set_solid_recipe(definition, ControlVisualRole::progress,
                     ControlSurfaceState::normal, false, progress_track);
    set_solid_recipe(definition, ControlVisualRole::progress,
                     ControlSurfaceState::normal, true, progress_fill);

    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("theme.stock.root"));
    std::shared_ptr<gui_forms::TextBox> editor = make_control<TextBox>(StableId("theme.stock.editor"), "alpha");
    (*editor).set_requested_bounds({8.0, 8.0, 140.0, 28.0});
    std::shared_ptr<gui_forms::ListBox> list = make_control<ListBox>(StableId("theme.stock.list"));
    (*list).set_requested_bounds({8.0, 42.0, 140.0, 58.0});
    (*list).items = {"one", "two"};
    (*list).select_index(0U);
    std::shared_ptr<gui_forms::ComboBox> combo = make_control<ComboBox>(StableId("theme.stock.combo"));
    (*combo).set_requested_bounds({156.0, 8.0, 136.0, 28.0});
    (*combo).items = {"one", "two"};
    (*combo).selected_index = 0U;
    std::shared_ptr<gui_forms::ProgressBar> progress = make_control<ProgressBar>(StableId("theme.stock.progress"));
    (*progress).set_requested_bounds({156.0, 46.0, 136.0, 20.0});
    (*progress).value = 50.0;
    (*progress).visual_style = ProgressBarVisualStyle::continuous;
    std::shared_ptr<gui_forms::MenuStrip> menu = make_control<MenuStrip>(StableId("theme.stock.menu"));
    (*menu).set_requested_bounds({8.0, 108.0, 284.0, 28.0});
    std::shared_ptr<gui_forms::Command> menu_command = std::make_shared<Command>("theme.stock.open", "Open");
    (*menu).set_items({{"file", "File",
                      {{"open", MenuItemKind::command, menu_command, "Open", {}}}}});
    (*menu).selected_item_id = "file";
    (*root).add_child(editor);
    (*root).add_child(list);
    (*root).add_child(combo);
    (*root).add_child(progress);
    (*root).add_child(menu);
    Window window(root, {300.0, 144.0});
    window.set_theme(Theme::create(definition));
    window.perform_layout();
    PointerEvent hover{PointerAction::move, PointerButton::none,
                       {20.0, 120.0}};
    static_cast<void>(window.dispatch_pointer(hover));

    ThemePainter painter;
    window.paint(painter, {0.0, 0.0, 300.0, 144.0});
    if (!contains_color(painter, editor_color)) std::cerr << "missing: editor\n";
    if (!contains_color(painter, selection_color)) std::cerr << "missing: selection\n";
    if (!contains_color(painter, menu_color)) std::cerr << "missing: menu\n";
    if (!contains_color(painter, progress_track)) std::cerr << "missing: progress track\n";
    if (!contains_color(painter, progress_fill)) std::cerr << "missing: progress fill\n";
    require(contains_color(painter, editor_color) &&
                contains_color(painter, selection_color) &&
                contains_color(painter, menu_color) &&
                contains_color(painter, progress_track) &&
                contains_color(painter, progress_fill),
            "editor, selection, menu-item, and progress recipes must reach stock paint");
}


// WCAG 2 relative luminance and contrast ratio, for the accent family's
// text check (T4 phase 1's contrast report found white text on near-white
// accent plates).
double wcag_channel(std::uint8_t value) {
    const double encoded = static_cast<double>(value) / 255.0;
    if (encoded <= 0.04045) return encoded / 12.92;
    return std::pow((encoded + 0.055) / 1.055, 2.4);
}

double wcag_luminance(Color colour) {
    const double luminance = 0.2126 * wcag_channel(colour.red) + 0.7152 * wcag_channel(colour.green) +
                             0.0722 * wcag_channel(colour.blue);
    return luminance;
}

double wcag_ratio(Color first, Color second) {
    const double a = wcag_luminance(first);
    const double b = wcag_luminance(second);
    const double ratio = (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    return ratio;
}

// windows-professional's accent family's text passes 4.5:1 on every gradient stop of its
// resting, hot, pressed, pending and invalid plates, selected or not.
void test_accent_text_contrast() {
    const std::shared_ptr<const Theme> theme = test_support::professional_theme();
    const std::array<ControlSurfaceState, 5> states{
        ControlSurfaceState::normal, ControlSurfaceState::hot, ControlSurfaceState::pressed,
        ControlSurfaceState::pending, ControlSurfaceState::invalid};
    const std::array<bool, 2> selections{false, true};
    for (const bool selected : selections) {
        for (const ControlSurfaceState state : states) {
            ControlVisualContext context{};
            context.surface = state;
            context.selected = selected;
            const ControlVisualRecipe& recipe = (*theme).resolve(ControlVisualRole::accent_button, context);
            for (const MaterialFillLayer& fill : recipe.material.fills) {
                if (fill.kind == MaterialFillKind::solid) {
                    require(wcag_ratio(recipe.text, fill.color) >= 4.5, "accent text passes 4.5:1 on its plate");
                }
                for (const GradientStop& stop : fill.stops) {
                    require(wcag_ratio(recipe.text, stop.color) >= 4.5, "accent text passes 4.5:1 on its plate");
                }
            }
        }
    }
}

} // namespace

int main() {
    try {
        test_default_theme_has_complete_state_matrix();
        test_structural_tokens_are_validated_atomically();
        test_text_shadow_is_bounded_recipe_data();
        test_window_theme_replacement_and_control_inheritance_are_atomic();
        test_button_routes_hover_and_status_through_theme_recipes();
        test_editor_selection_menu_and_progress_roles_reach_stock_controls();
        test_accent_text_contrast();
        std::cout << "gui_forms_theme_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_theme_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
