#include "connected_controls_lab.hpp"

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::connected_controls_lab {
namespace {

constexpr Color ink = Color::rgba(29, 45, 75);
constexpr Color muted = Color::rgba(92, 108, 130);

class LabRoot final : public Panel {
public:
    explicit LabRoot(StableId id) : Panel(std::move(id)) {}

    void retain(SubscriptionToken token) {
        subscriptions_.push_back(std::move(token));
    }

private:
    std::vector<SubscriptionToken> subscriptions_;
};

template <typename Parent, typename Child>
void add_at(const std::shared_ptr<Parent>& parent,
            const std::shared_ptr<Child>& child, const Rect bounds,
            const AnchorStyles anchor = AnchorStyles::top | AnchorStyles::left) {
    (*child).set_requested_bounds(bounds);
    (*child).anchor = anchor;
    (*parent).add_child(child);
}

std::shared_ptr<Label> label(std::string id, std::string text, FontSpec font,
                             const Color color) {
    std::shared_ptr<Label> result = make_control<Label>(
        StableId(std::move(id)), std::move(text));
    (*result).font = font;
    (*result).fore_color = color;
    return result;
}

// The pearl gradient's stops, border and text for one state.
struct PearlColours final {
    Color top;
    Color middle;
    Color bottom;
    Color border;
    Color text;
};

PearlColours pearl_colours(const ControlSurfaceState state, const bool selected) {
    PearlColours colours{Color::rgba(255, 255, 255), Color::rgba(239, 245, 250),
                         Color::rgba(201, 214, 229), Color::rgba(118, 143, 167), ink};
    if (selected) {
        colours.top = Color::rgba(239, 248, 255);
        colours.middle = Color::rgba(204, 228, 249);
        colours.bottom = Color::rgba(162, 198, 234);
        colours.border = Color::rgba(57, 117, 178);
    }
    switch (state) {
    case ControlSurfaceState::hot:
        colours.top = Color::rgba(255, 255, 255);
        colours.middle = Color::rgba(225, 241, 252);
        colours.bottom = Color::rgba(183, 215, 239);
        colours.border = Color::rgba(72, 126, 169);
        return colours;
    case ControlSurfaceState::pressed:
        colours.top = Color::rgba(174, 198, 217);
        colours.middle = Color::rgba(207, 223, 235);
        colours.bottom = Color::rgba(250, 253, 255);
        colours.border = Color::rgba(61, 94, 121);
        return colours;
    case ControlSurfaceState::pending:
        colours.top = Color::rgba(255, 249, 218);
        colours.middle = Color::rgba(250, 239, 184);
        colours.bottom = Color::rgba(238, 218, 130);
        colours.border = Color::rgba(157, 119, 31);
        return colours;
    case ControlSurfaceState::invalid:
        colours.top = Color::rgba(255, 242, 244);
        colours.middle = Color::rgba(249, 219, 225);
        colours.bottom = Color::rgba(235, 186, 196);
        colours.border = Color::rgba(157, 61, 78);
        colours.text = Color::rgba(112, 44, 58);
        return colours;
    case ControlSurfaceState::disabled:
    case ControlSurfaceState::deactivated:
        colours.top = Color::rgba(244, 246, 248);
        colours.middle = Color::rgba(231, 235, 238);
        colours.bottom = Color::rgba(211, 216, 220);
        colours.border = Color::rgba(154, 163, 171);
        colours.text = Color::rgba(117, 126, 134);
        return colours;
    case ControlSurfaceState::normal:
    case ControlSurfaceState::count:
        // The resting colours (count is no state).
        return colours;
    }
    // A state outside the enumeration takes the resting colours.
    return colours;
}

ControlVisualRecipe pearl_recipe(const ControlSurfaceState state,
                                 const bool selected) {
    ControlVisualRecipe result;
    const PearlColours colours = pearl_colours(state, selected);
    const Color top = colours.top;
    const Color middle = colours.middle;
    const Color bottom = colours.bottom;
    const Color border = colours.border;
    result.text = colours.text;
    result.material.fills = {
        MaterialFillLayer::linear_css_angle(180.0, {
            {0.0, top}, {0.48, middle}, {0.52, middle}, {1.0, bottom},
        }),
    };
    result.material.shadows = {
        {{0.0, 2.0}, 3.0, 0.0, Color::rgba(52, 77, 99, 48), false},
        {{0.0, 1.0}, 1.0, 0.0, Color::rgba(255, 255, 255, 210), true},
    };
    result.material.border = MaterialBorder{border, 1.0};
    result.material.keylines = {
        {MaterialEdge::top, Color::rgba(255, 255, 255, 224), 1.0, 1.0},
        {MaterialEdge::bottom, Color::rgba(102, 127, 150, 110), 1.0, 1.0},
    };
    result.material.corner_radius = 5.0;
    result.focus_ring = Color::rgba(41, 79, 145);
    result.focus_width = 1.0;
    result.default_ring = Color::rgba(25, 70, 137);
    result.default_width = 2.0;
    result.pressed_content_offset = {1.0, 1.0};
    return result;
}

std::shared_ptr<const Theme> pearl_theme() {
    ThemeDefinition definition = windows_professional_theme_definition();
    definition.id = "connected-controls-office-pearl";
    for (const ControlVisualRole role : {
             ControlVisualRole::button,
             ControlVisualRole::command_button,
             ControlVisualRole::choice}) {
        ControlRoleRecipes& recipes =
            definition.roles[static_cast<std::size_t>(role)];
        for (std::size_t index = 0U; index < control_surface_state_count;
             ++index) {
            const ControlSurfaceState state =
                static_cast<ControlSurfaceState>(index);
            recipes.ordinary[index] = pearl_recipe(state, false);
            recipes.selected[index] = pearl_recipe(state, true);
        }
    }
    std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

template <typename ButtonType>
std::shared_ptr<ButtonType> segment(std::string id, std::string text,
                                    const Rect bounds,
                                    const std::shared_ptr<LabRoot>& root) {
    std::shared_ptr<ButtonType> result = make_control<ButtonType>(
        StableId(std::move(id)), std::move(text));
    (*result).font = {FontRole::control, 10.0, 600, false, 0.08};
    (*result).margin = Insets{};
    add_at(root, result, bounds);
    return result;
}

struct SelectTarget final {
    std::weak_ptr<VisualInspectorView> view;
    std::weak_ptr<VisualInspectorOverlay> overlay;
    std::string target;

    void operator()(ButtonBase&) const {
        if (const std::shared_ptr<VisualInspectorView> strong = view.lock()) {
            (*strong).target_stable_id = target;
        }
        if (const std::shared_ptr<VisualInspectorOverlay> strong = overlay.lock()) {
            (*strong).target_stable_id = target;
        }
    }
};

struct ToggleDropDown final {
    std::weak_ptr<DropDownButton> button;
    void operator()(DropDownButton&) const {
        if (const std::shared_ptr<DropDownButton> strong = button.lock()) {
            (*strong).set_drop_down_open(!(*strong).drop_down_open());
        }
    }
};

bool demonstrate_atomic_rejection(
    const std::array<std::shared_ptr<Button>, 2U>& controls) {
    (*controls[0]).set_connection_topology(
        ConnectedControlTopology{ConnectedControlAxis::vertical, 0U, 2U});
    const std::optional<ConnectedControlTopology> first_before =
        (*controls[0]).connection_topology();
    const std::optional<ConnectedControlTopology> second_before =
        (*controls[1]).connection_topology();
    try {
        const std::array<std::shared_ptr<ButtonBase>, 2U> group{
            controls[0], controls[1]};
        connect_button_group(group, ConnectedControlAxis::horizontal);
    } catch (const std::invalid_argument&) {
        const bool unchanged = (*controls[0]).connection_topology() == first_before &&
                               (*controls[1]).connection_topology() == second_before;
        (*controls[0]).set_connection_topology(std::nullopt);
        return unchanged;
    }
    return false;
}

} // namespace

std::unique_ptr<Window> make_connected_controls_lab() {
    std::shared_ptr<LabRoot> root = make_control<LabRoot>(
        StableId("connected-controls.root"));
    (*root).set_background(Color::rgba(225, 231, 237));

    std::shared_ptr<Control> header = make_control<Control>(
        StableId("connected-controls.header"));
    SurfaceMaterial header_material;
    header_material.fills = {MaterialFillLayer::linear_css_angle(92.0, {
        {0.0, Color::rgba(23, 52, 127)},
        {0.62, Color::rgba(58, 104, 203)},
        {1.0, Color::rgba(217, 104, 114)},
    })};
    header_material.keylines = {
        {MaterialEdge::bottom, Color::rgba(204, 239, 255, 190), 1.0, 0.0},
    };
    (*header).set_authored_surface_material(std::move(header_material));
    add_at(root, header, {0.0, 0.0, 1400.0, 58.0},
           AnchorStyles::top | AnchorStyles::left | AnchorStyles::right);
    add_at(header,
        label("connected-controls.header.title", "Connected Control Painting",
              {FontRole::control, 19.0, 700, false, 0.14},
              Color::rgba(255, 255, 255)),
        {22.0, 4.0, 560.0, 30.0});
    add_at(header,
        label("connected-controls.header.detail",
              "Explicit topology · one seam · separate focus, semantics, commands, and hit targets",
              {FontRole::content, 10.0, 400, false},
              Color::rgba(229, 242, 255)),
        {24.0, 35.0, 760.0, 20.0});

    add_at(root,
        label("connected-controls.baseline.title", "STANDALONE BASELINE",
              {FontRole::control, 9.0, 700, false, 0.14}, muted),
        {24.0, 78.0, 260.0, 20.0});
    std::shared_ptr<Button> isolated = segment<Button>(
        "connected-controls.isolated", "Isolated", {24.0, 101.0, 148.0, 40.0}, root);

    add_at(root,
        label("connected-controls.two.title", "TWO · PUSH + SPLIT",
              {FontRole::control, 9.0, 700, false, 0.14}, muted),
        {24.0, 158.0, 300.0, 20.0});
    std::shared_ptr<Button> two_primary = segment<Button>(
        "connected-controls.two.primary", "Move", {24.0, 181.0, 142.0, 42.0}, root);
    std::shared_ptr<DropDownButton> two_split = segment<DropDownButton>(
        "connected-controls.two.split", "Copy", {166.0, 181.0, 164.0, 42.0}, root);
    (*two_split).drop_down_mode = DropDownButtonMode::split;
    const std::array<std::shared_ptr<ButtonBase>, 2U> two_group{
        two_primary, two_split};
    connect_button_group(two_group, ConnectedControlAxis::horizontal);

    add_at(root,
        label("connected-controls.three.title", "THREE · DEFAULT / CHECKED / DISABLED",
              {FontRole::control, 9.0, 700, false, 0.14}, muted),
        {24.0, 239.0, 410.0, 20.0});
    std::shared_ptr<Button> three_default = segment<Button>(
        "connected-controls.three.default", "Default", {24.0, 262.0, 130.0, 42.0}, root);
    (*three_default).default_button = true;
    std::shared_ptr<CheckBox> three_checked = segment<CheckBox>(
        "connected-controls.three.checked", "Checked", {154.0, 262.0, 132.0, 42.0}, root);
    (*three_checked).appearance = CheckBoxAppearance::button;
    (*three_checked).checked = true;
    std::shared_ptr<Button> three_disabled = segment<Button>(
        "connected-controls.three.disabled", "Disabled", {286.0, 262.0, 138.0, 42.0}, root);
    (*three_disabled).enabled = false;
    const std::array<std::shared_ptr<ButtonBase>, 3U> three_group{
        three_default, three_checked, three_disabled};
    connect_button_group(three_group, ConnectedControlAxis::horizontal);

    add_at(root,
        label("connected-controls.five.title", "FIVE · MIXED COMMAND STATES",
              {FontRole::control, 9.0, 700, false, 0.14}, muted),
        {24.0, 320.0, 330.0, 20.0});
    std::array<std::shared_ptr<ButtonBase>, 5U> five_group;
    for (std::size_t index = 0U; index < 4U; ++index) {
        std::shared_ptr<Button> item = segment<Button>(
            "connected-controls.five." + std::to_string(index),
            std::array<const char*, 4U>{"Cut", "Copy", "Paste", "Delete"}[index],
            {24.0 + static_cast<double>(index) * 112.0, 343.0, 112.0, 42.0}, root);
        if (index == 1U) (*item).selected = true;
        if (index == 2U) (*item).enabled = false;
        if (index == 3U) (*item).set_visual_status(ControlVisualStatus::pending);
        five_group[index] = item;
    }
    std::shared_ptr<DropDownButton> five_menu = segment<DropDownButton>(
        "connected-controls.five.menu", "More", {472.0, 343.0, 128.0, 42.0}, root);
    (*five_menu).drop_down_mode = DropDownButtonMode::menu;
    five_group[4] = five_menu;
    connect_button_group(five_group, ConnectedControlAxis::horizontal);

    add_at(root,
        label("connected-controls.vertical.title", "VERTICAL · THREE",
              {FontRole::control, 9.0, 700, false, 0.14}, muted),
        {642.0, 78.0, 220.0, 20.0});
    std::array<std::shared_ptr<ButtonBase>, 3U> vertical_group;
    for (std::size_t index = 0U; index < vertical_group.size(); ++index) {
        std::shared_ptr<Button> item = segment<Button>(
            "connected-controls.vertical." + std::to_string(index),
            std::array<const char*, 3U>{"Details", "List", "Icons"}[index],
            {642.0, 101.0 + static_cast<double>(index) * 44.0, 170.0, 44.0}, root);
        if (index == 2U) (*item).selected = true;
        vertical_group[index] = item;
    }
    connect_button_group(vertical_group, ConnectedControlAxis::vertical);

    add_at(root,
        label("connected-controls.notes",
              "Hover and press any segment. Tab traverses every member independently.\n"
              "The split disclosure keeps its own internal boundary; group seams stay singular.",
              {FontRole::content, 10.0, 400, false}, ink),
        {642.0, 250.0, 250.0, 70.0});

    std::array<std::shared_ptr<Button>, 2U> rejection_controls{
        segment<Button>("connected-controls.rejection.a", "A",
                        {642.0, 343.0, 76.0, 38.0}, root),
        segment<Button>("connected-controls.rejection.b", "B",
                        {718.0, 343.0, 76.0, 38.0}, root),
    };
    const bool rejected_atomically = demonstrate_atomic_rejection(rejection_controls);
    const std::array<std::shared_ptr<ButtonBase>, 2U> rejection_group{
        rejection_controls[0], rejection_controls[1]};
    connect_button_group(rejection_group, ConnectedControlAxis::horizontal);
    add_at(root,
        label("connected-controls.rejection.status",
              rejected_atomically
                  ? "REJECTED ATOMICALLY · mixed-axis request changed 0 members"
                  : "ERROR · invalid mixed-axis request mutated the group",
              {FontRole::monospace, 9.0, 700, false},
              rejected_atomically ? Color::rgba(53, 112, 73)
                                  : Color::rgba(145, 40, 54)),
        {642.0, 389.0, 330.0, 34.0});

    std::shared_ptr<VisualInspectorView> inspector =
        make_control<VisualInspectorView>(
            StableId("connected-controls.inspector"),
            "connected-controls.three.checked");
    add_at(root, inspector, {930.0, 76.0, 440.0, 610.0},
           AnchorStyles::top | AnchorStyles::bottom |
               AnchorStyles::left | AnchorStyles::right);
    std::shared_ptr<VisualInspectorOverlay> overlay =
        make_control<VisualInspectorOverlay>(
            StableId("connected-controls.overlay"),
            "connected-controls.three.checked");

    const std::array<std::pair<const char*, const char*>, 7U> targets{{
        {"Isolated", "connected-controls.isolated"},
        {"Two lead", "connected-controls.two.primary"},
        {"Split tail", "connected-controls.two.split"},
        {"Default", "connected-controls.three.default"},
        {"Checked", "connected-controls.three.checked"},
        {"Disabled", "connected-controls.three.disabled"},
        {"Vertical", "connected-controls.vertical.1"},
    }};
    for (std::size_t index = 0U; index < targets.size(); ++index) {
        const double x = 24.0 + static_cast<double>(index % 4U) * 142.0;
        const double y = 430.0 + static_cast<double>(index / 4U) * 38.0;
        std::shared_ptr<Button> selector = segment<Button>(
            "connected-controls.target." + std::to_string(index),
            targets[index].first, {x, y, 132.0, 30.0}, root);
        (*selector).visual_style = ButtonVisualStyle::command;
        (*root).retain((*selector).clicked().subscribe(
            *root, SelectTarget{inspector, overlay, targets[index].second}));
    }

    (*root).retain((*two_split).drop_down_requested().subscribe(
        *root, ToggleDropDown{two_split}));
    (*root).retain((*five_menu).drop_down_requested().subscribe(
        *root, ToggleDropDown{five_menu}));

    add_at(root, overlay, {0.0, 0.0, 1400.0, 760.0},
           AnchorStyles::top | AnchorStyles::bottom |
               AnchorStyles::left | AnchorStyles::right);

    std::unique_ptr<Window> window = std::make_unique<Window>(
        root, Size{1400.0, 760.0});
    (*window).set_theme(pearl_theme());
    (*window).perform_layout();
    (*window).set_accept_button(three_default);
    static_cast<void>((*window).request_focus(three_default));
    return window;
}

} // namespace gui_forms::connected_controls_lab
