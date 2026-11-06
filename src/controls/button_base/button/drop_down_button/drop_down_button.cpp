#include "gui_forms/controls/button_base/button/drop_down_button/drop_down_button.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

DropDownButton::DropDownButton(StableId stable_id, std::string text,
                               const DropDownButtonMode mode)
    : Button(std::move(stable_id), std::move(text)), mode_(mode) {
    if (mode_ != DropDownButtonMode::menu &&
        mode_ != DropDownButtonMode::split) {
        throw std::invalid_argument("drop-down button mode is unknown");
    }
    content_padding = {6.0, 4.0, 22.0, 4.0};
}

void DropDownButton::set_drop_down_mode(const DropDownButtonMode mode) {
    require_mutable();
    if (mode != DropDownButtonMode::menu && mode != DropDownButtonMode::split) {
        throw std::invalid_argument("drop-down button mode is unknown");
    }
    if (mode_ == mode) return;
    mode_ = mode;
    pointer_drop_down_ = false;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void DropDownButton::set_drop_down_width(const double width) {
    require_mutable();
    if (!std::isfinite(width) || width < 12.0 || width > 64.0) {
        throw std::invalid_argument(
            "drop-down button width must be finite and between 12 and 64");
    }
    if (drop_down_width_ == width) return;
    Insets padding = content_padding.read();
    if (edge_ == DropDownButtonEdge::bottom) padding.bottom = std::max(0.0, padding.bottom - drop_down_width_) + width;
    else padding.right = std::max(0.0, padding.right - drop_down_width_) + width;
    content_padding = padding;
    drop_down_width_ = width;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void DropDownButton::set_drop_down_edge(const DropDownButtonEdge edge) {
    require_mutable();
    if (edge != DropDownButtonEdge::right && edge != DropDownButtonEdge::bottom) {
        throw std::invalid_argument("drop-down button edge is unknown");
    }
    if (edge_ == edge) return;
    Insets padding = content_padding.read();
    if (edge_ == DropDownButtonEdge::right) {
        padding.right = std::max(0.0, padding.right - drop_down_width_);
        padding.bottom += drop_down_width_;
    } else {
        padding.bottom = std::max(0.0, padding.bottom - drop_down_width_);
        padding.right += drop_down_width_;
    }
    content_padding = padding;
    edge_ = edge;
    pointer_drop_down_ = false;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void DropDownButton::set_drop_down_open(const bool open) {
    require_mutable();
    if (drop_down_open_ == open) return;
    drop_down_open_ = open;
    invalidate(Dirty::paint | Dirty::semantics);
}

bool DropDownButton::perform_drop_down() {
    if (!prepare_command_activation()) return false;
    drop_down_requested_.emit(*this);
    return true;
}

namespace {

constexpr std::array<AnatomyState, 1> drop_down_segment_states{{AnatomyState::pressed}};

constexpr std::array<PartField<DropDownButton::Parts>, 8> drop_down_button_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &DropDownButton::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &DropDownButton::Parts::face},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &DropDownButton::Parts::icon},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &DropDownButton::Parts::label},
    {.declaration = {.name = "drop_down", .role = PartRole::actuator, .composed_from = {},
                     .states = drop_down_segment_states},
     .member = &DropDownButton::Parts::drop_down},
    {.declaration = {.name = "disclosure", .role = PartRole::disclosure, .composed_from = {}},
     .member = &DropDownButton::Parts::disclosure},
    {.declaration = {.name = "preselect", .role = PartRole::preselect, .composed_from = {}},
     .member = &DropDownButton::Parts::preselect},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &DropDownButton::Parts::focus},
}};

constexpr std::array<StateField<DropDownButton::States>, 11> drop_down_button_state_fields{{
    {.state = AnatomyState::normal, .member = &DropDownButton::States::normal},
    {.state = AnatomyState::hot, .member = &DropDownButton::States::hot},
    {.state = AnatomyState::pressed, .member = &DropDownButton::States::pressed},
    {.state = AnatomyState::preselect, .member = &DropDownButton::States::preselect},
    {.state = AnatomyState::selected, .member = &DropDownButton::States::selected},
    {.state = AnatomyState::enabled, .member = &DropDownButton::States::enabled},
    {.state = AnatomyState::disabled, .member = &DropDownButton::States::disabled},
    {.state = AnatomyState::focused, .member = &DropDownButton::States::focused},
    {.state = AnatomyState::focus_visible, .member = &DropDownButton::States::focus_visible},
    {.state = AnatomyState::open, .member = &DropDownButton::States::open},
    {.state = AnatomyState::closed, .member = &DropDownButton::States::closed},
}};

constexpr std::array<PartDeclaration, 8> drop_down_button_parts =
    part_declarations(drop_down_button_part_fields);
constexpr std::array<AnatomyState, 11> drop_down_button_states =
    state_declarations(drop_down_button_state_fields);

constexpr ControlAnatomy drop_down_button_anatomy{
    .control_id = "drop_down_button",
    .parts = drop_down_button_parts,
    .states = drop_down_button_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& DropDownButton::anatomy() const noexcept {
    return drop_down_button_anatomy;
}

Rect DropDownButton::drop_down_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const bool bottom = edge_ == DropDownButtonEdge::bottom;
    const double extent = std::min(drop_down_width_, bottom ? bounds.height : bounds.width);
    const double start = (bottom ? bounds.height : bounds.width) - extent;
    if (bottom) {
        const Rect segment{0.0, start, bounds.width, extent};
        return segment;
    }
    const Rect segment{start, 0.0, extent, bounds.height};
    return segment;
}

void DropDownButton::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Button::Parts base = button_parts();
    Parts record;
    record.region = base.region;
    record.face = base.face;
    record.icon = base.icon;
    record.label = base.label;
    record.preselect = base.preselect;
    record.focus = base.focus;
    const Rect bounds = local_bounds();
    if (bounds.width > 0.0 && bounds.height > 0.0) {
        const Rect segment = drop_down_bounds();
        const bool bottom = edge_ == DropDownButtonEdge::bottom;
        // The 7 x 4 arrow at rest, as on_paint draws it before the pressed or
        // open offset.
        const double center_x = bottom ? bounds.width * 0.5 : segment.x + segment.width * 0.5;
        const double center_y = bottom ? segment.y + segment.height * 0.5 : bounds.height * 0.5;
        const Rect arrow{std::floor(center_x) - 3.0, std::floor(center_y) - 1.0, 7.0, 4.0};
        record.drop_down = PartGeometry::from_bounds(segment);
        record.disclosure = PartGeometry::from_bounds(arrow);
        // A press that began on the drop-down segment presses that part.
        if (pressed_visual() && pointer_drop_down_) {
            record.drop_down.states.insert(AnatomyState::pressed);
        }
    }
    write_parts(anatomy(), drop_down_button_part_fields, record, geometry);
}

AnatomyStateSet DropDownButton::anatomy_states() const {
    AnatomyStateSet states = Button::anatomy_states();
    states.erase(AnatomyState::open);
    states.erase(AnatomyState::closed);
    if (drop_down_open_) {
        states.insert(AnatomyState::open);
    } else {
        states.insert(AnatomyState::closed);
    }
    return states;
}

DropDownButton::Parts DropDownButton::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), drop_down_button_part_fields, published.parts(), record);
    return record;
}

DropDownButton::States DropDownButton::states() const {
    States record;
    read_states(drop_down_button_state_fields, anatomy_states(), record);
    return record;
}

void DropDownButton::on_paint(Painter& painter, const Rect local_damage) {
    Button::on_paint(painter, local_damage);
    const Rect bounds = local_bounds();
    if (bounds.width <= 0.0 || bounds.height <= 0.0) return;

    const bool bottom = edge_ == DropDownButtonEdge::bottom;
    const double extent = std::min(drop_down_width_, bottom ? bounds.height : bounds.width);
    const double start = (bottom ? bounds.height : bounds.width) - extent;
    // The split's divider, the arrow and the open seam are parts the theme's
    // engine draws in the button's anatomy states; a style override inks the
    // arrow (T4-1).
    const AnatomyStateSet states = anatomy_states();
    std::optional<Color> ink{};
    if (has_style_override()) ink = style().text;
    if (mode_ == DropDownButtonMode::split && start > 0.0 &&
        (hovered_visual() || pressed_visual() || drop_down_open_)) {
        PartPaint divider;
        divider.family = ControlVisualRole::button;
        divider.role = PartRole::divider;
        divider.states = states;
        divider.bounds = bottom ? Rect{3.0, start - 0.5, std::max(0.0, bounds.width - 6.0), 1.0}
                                : Rect{start - 0.5, 3.0, 1.0, std::max(0.0, bounds.height - 6.0)};
        paint_part(painter, *this, divider);
    }
    const double center_x = bottom ? bounds.width * 0.5 : start + extent * 0.5;
    const double center_y = (bottom ? start + extent * 0.5 : bounds.height * 0.5) +
        (pressed_visual() || drop_down_open_ ? 1.0 : 0.0);
    // The 7 x 4 arrow. The arrow mark draws half its square's side in rows,
    // so the square is 8 units, placed to put the rows where they were.
    PartPaint disclosure;
    disclosure.family = ControlVisualRole::button;
    disclosure.role = PartRole::disclosure;
    disclosure.mark = PartMark::arrow_down;
    disclosure.states = states;
    disclosure.bounds = {std::floor(center_x) - 4.0, std::floor(center_y) - 4.0, 8.0, 8.0};
    disclosure.authored_foreground = ink;
    disclosure.key = PartKey{"disclosure", {}, {}};
    paint_part(painter, *this, disclosure);
    if (drop_down_open_) {
        PartPaint seam;
        seam.family = ControlVisualRole::button;
        seam.role = PartRole::seam;
        seam.states = states;
        seam.bounds = {1.0, std::max(0.0, bounds.height - 3.0), std::max(0.0, bounds.width - 2.0), 2.0};
        paint_part(painter, *this, seam);
    }
}

bool DropDownButton::point_in_drop_down(const Point window_point) const noexcept {
    if (mode_ == DropDownButtonMode::menu) return true;
    const Point local = point_from_window(window_point);
    const Rect bounds = local_bounds();
    if (edge_ == DropDownButtonEdge::bottom) {
        return local.y >= std::max(0.0, bounds.height - drop_down_width_) &&
               local.y <= bounds.height && local.x >= 0.0 && local.x <= bounds.width;
    }
    return local.x >= std::max(0.0, bounds.width - drop_down_width_) &&
           local.x <= bounds.width && local.y >= 0.0 && local.y <= bounds.height;
}

void DropDownButton::on_pointer(PointerEvent& event) {
    if (event.button == PointerButton::primary &&
        event.action == PointerAction::down) {
        pointer_drop_down_ = point_in_drop_down(event.position);
    } else if (event.action == PointerAction::leave && !pressed_visual()) {
        pointer_drop_down_ = false;
    }
    ButtonBase::on_pointer(event);
}

void DropDownButton::on_key(KeyEvent& event) {
    if (event.action == KeyAction::down && !event.repeat &&
        event.physical_key == PhysicalKey::down &&
        has_modifier(event.modifiers, Modifier::alt)) {
        event.handled = perform_drop_down();
        return;
    }
    pointer_drop_down_ = false;
    ButtonBase::on_key(event);
}

SemanticDescriptor DropDownButton::semantic_descriptor() const {
    SemanticDescriptor descriptor = Button::semantic_descriptor();
    // Split mode keeps a primary click beside the menu arrow; menu mode is
    // one button that opens the menu.
    descriptor.role = mode_ == DropDownButtonMode::split ? SemanticRole::split_button
                                                         : SemanticRole::menu_button;
    descriptor.has_popup = true;
    descriptor.actions.push_back(SemanticAction::show_menu);
    descriptor.actions.push_back(drop_down_open_ ? SemanticAction::collapse
                                                 : SemanticAction::expand);
    if (drop_down_open_) descriptor.states |= SemanticState::expanded;
    return descriptor;
}

bool DropDownButton::on_semantic_action(const SemanticAction action,
                                        const std::string_view value) {
    if (action == SemanticAction::show_menu ||
        (action == SemanticAction::expand && !drop_down_open_)) {
        return perform_drop_down();
    }
    if (action == SemanticAction::collapse && drop_down_open_) {
        drop_down_close_requested_.emit(*this);
        return true;
    }
    return Button::on_semantic_action(action, value);
}

void DropDownButton::on_activate() {
    if (mode_ == DropDownButtonMode::menu || pointer_drop_down_) {
        pointer_drop_down_ = false;
        static_cast<void>(perform_drop_down());
        return;
    }
    pointer_drop_down_ = false;
    ButtonBase::on_activate();
}

namespace {

// The events DropDownButton adds to those of Button, in declaration order.
constexpr std::array<InteractionDeclaration, 2> drop_down_button_interactions{{
    {.name = "drop_down_requested", .payload = "DropDownButton", .mutable_payload = true},
    {.name = "drop_down_close_requested", .payload = "DropDownButton", .mutable_payload = true},
}};

} // namespace

void DropDownButton::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Button::append_interactions(interactions);
    interactions.insert(interactions.end(), drop_down_button_interactions.begin(), drop_down_button_interactions.end());
}

} // namespace gui_forms
