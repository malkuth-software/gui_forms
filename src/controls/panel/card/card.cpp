#include "gui_forms/controls/panel/card/card.hpp"
#include "card_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

bool valid_card_extent(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 4096.0;
}

bool valid_layout(const CardLayout& layout) noexcept {
    const double values[] = {layout.padding.left, layout.padding.top,
                             layout.padding.right, layout.padding.bottom,
                             layout.section_gap, layout.header_extent,
                             layout.footer_extent};
    return std::all_of(std::begin(values), std::end(values),
                       valid_card_extent);
}

CardLayout themed_card_layout(const Theme& theme) noexcept {
    const ThemeStructureTokens& structure = theme.structure();
    return {{structure.spacing.large, structure.spacing.medium,
             structure.spacing.large, structure.spacing.medium},
            structure.spacing.medium,
            structure.geometry.control_height,
            structure.geometry.control_height};
}

} // namespace

Card::Card(StableId stable_id) : Panel(std::move(stable_id)) {
    visual_role = ControlVisualRole::card;
}

Control::Ptr Card::replace_section(Control::Ptr& slot,
                                   Control::Ptr replacement) {
    require_mutable();
    if (replacement &&
        (replacement == header_ || replacement == body_ || replacement == footer_)) {
        if (replacement == slot) return {};
        throw std::logic_error("one control cannot occupy two card sections");
    }
    if (replacement && (*replacement).parent()) {
        throw std::logic_error("card section control already has a parent");
    }
    Control::Ptr previous = slot;
    if (replacement) add_child(replacement);
    if (slot) previous = remove_child((*slot).runtime_id());
    slot = std::move(replacement);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
    return previous;
}

Control::Ptr Card::set_header(Control::Ptr control) {
    return replace_section(header_, std::move(control));
}

Control::Ptr Card::set_body(Control::Ptr control) {
    return replace_section(body_, std::move(control));
}

Control::Ptr Card::set_footer(Control::Ptr control) {
    return replace_section(footer_, std::move(control));
}

CardLayout Card::effective_card_layout() const noexcept {
    return uses_theme_layout_ ? themed_card_layout(effective_theme()) : layout_;
}

CardLayout Card::read_view(const decltype(card_layout)&) const {
    return layout_;
}

void Card::write_view(const decltype(card_layout)&, CardLayout incoming) {
    require_mutable();
    if (!valid_layout(incoming)) {
        throw std::invalid_argument("card layout values must be finite and bounded");
    }
    if (!uses_theme_layout_ && layout_ == incoming) return;
    layout_ = incoming;
    uses_theme_layout_ = false;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void Card::reset_card_layout_to_theme() {
    require_mutable();
    if (uses_theme_layout_) return;
    uses_theme_layout_ = true;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void Card::prepare_write(const decltype(interactive)&, bool&) {
    require_mutable();
}

// A card that stops being interactive drops its interaction visuals.
void Card::written(const decltype(interactive)&) {
    focusable = interactive.read();
    if (!interactive.read()) {
        hovered_ = false;
        pressed_ = false;
        focused_ = false;
        keyboard_key_ = 0U;
    }
    invalidate(Dirty::style | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

Size Card::measure(Size available) {
    const CardLayout layout = effective_card_layout();
    const double horizontal = layout.padding.left + layout.padding.right;
    const double vertical = layout.padding.top + layout.padding.bottom;
    const double inner_width = std::max(0.0, available.width - horizontal);
    double width{};
    double height = vertical;
    unsigned sections{};
    const Control::Ptr retained_header = header_;
    const Control::Ptr retained_body = body_;
    const Control::Ptr retained_footer = footer_;
    if (retained_header && is_current_layout_child(retained_header)) {
        const Size desired = (*retained_header).measure(
            {inner_width, layout.header_extent});
        if (!is_alive()) return {};
        if (header_ != retained_header ||
            !is_current_layout_child(retained_header)) {
            invalidate(Dirty::measure | Dirty::arrange);
        } else {
            width = std::max(width, desired.width);
            height += std::max(layout.header_extent, desired.height);
            ++sections;
        }
    }
    if (retained_body && is_current_layout_child(retained_body)) {
        const Size desired = (*retained_body).measure(
            {inner_width, available.height});
        if (!is_alive()) return {};
        if (body_ != retained_body || !is_current_layout_child(retained_body)) {
            invalidate(Dirty::measure | Dirty::arrange);
        } else {
            width = std::max(width, desired.width);
            height += desired.height;
            ++sections;
        }
    }
    if (retained_footer && is_current_layout_child(retained_footer)) {
        const Size desired = (*retained_footer).measure(
            {inner_width, layout.footer_extent});
        if (!is_alive()) return {};
        if (footer_ != retained_footer ||
            !is_current_layout_child(retained_footer)) {
            invalidate(Dirty::measure | Dirty::arrange);
        } else {
            width = std::max(width, desired.width);
            height += std::max(layout.footer_extent, desired.height);
            ++sections;
        }
    }
    if (sections > 1U) height += layout.section_gap * (sections - 1U);
    const Rect requested = requested_bounds();
    if (requested.width > 0.0) width = requested.width - horizontal;
    if (requested.height > 0.0) height = requested.height;
    return {std::min(available.width, std::max(0.0, width + horizontal)),
            std::min(available.height, std::max(0.0, height))};
}

void Card::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const CardLayout layout = effective_card_layout();
    const double left = layout.padding.left;
    const double width = std::max(
        0.0, final_bounds.width - layout.padding.left - layout.padding.right);
    double top = layout.padding.top;
    double bottom = std::max(top, final_bounds.height - layout.padding.bottom);
    if (header_) {
        const double extent = std::min(layout.header_extent,
                                       std::max(0.0, bottom - top));
        set_child_layout(header_, {left, top, width, extent});
        top += extent + layout.section_gap;
    }
    if (footer_) {
        const double extent = std::min(layout.footer_extent,
                                       std::max(0.0, bottom - top));
        bottom -= extent;
        set_child_layout(footer_, {left, bottom, width, extent});
        bottom -= layout.section_gap;
    }
    if (body_) {
        set_child_layout(body_, {left, top, width, std::max(0.0, bottom - top)});
    }
}

ControlVisualContext Card::current_context() const noexcept {
    return visual_context(hovered_, pressed_, selected.read(), focused_);
}

void Card::on_paint(Painter& painter, Rect local_damage) {
    if (has_background_override() || has_style_override()) {
        Panel::on_paint(painter, local_damage);
        return;
    }
    const Rect arranged = committed_arranged_bounds();
    const Rect bounds{0.0, 0.0, arranged.width, arranged.height};
    // The card's face and focus ring are parts the theme's engine draws, hot,
    // pressed and selected as the card is.
    AnatomyStateSet states{};
    if (hovered_) states.insert(AnatomyState::hot);
    if (pressed_) states.insert(AnatomyState::pressed);
    if (selected.read()) states.insert(AnatomyState::selected);
    PartPaint face;
    face.family = ControlVisualRole::card;
    face.role = PartRole::face;
    face.bounds = bounds;
    face.states = states;
    face.key = PartKey{"face", {}, {}};
    paint_part(painter, *this, face);
    if (focused_ && bounds.width > 8.0 && bounds.height > 8.0) {
        PartPaint ring;
        ring.family = ControlVisualRole::card;
        ring.role = PartRole::focus;
        ring.bounds = {2.5, 2.5, bounds.width - 5.0, bounds.height - 5.0};
        ring.states = states;
        ring.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, ring);
    }
}

Insets Card::visual_outsets() const noexcept {
    if (has_background_override() || has_style_override()) {
        return Panel::visual_outsets();
    }
    return surface_material_visual_outsets(
        effective_theme().resolve(ControlVisualRole::card,
                                  current_context()).material);
}

bool Card::hit_test_local(Point local_point) const {
    return interactive.read() && Control::hit_test_local(local_point);
}

void Card::on_pointer(PointerEvent& event) {
    if (!interactive.read()) return;
    if (event.action == PointerAction::enter || event.action == PointerAction::leave) {
        const bool next = event.action == PointerAction::enter;
        if (hovered_ != next) {
            hovered_ = next;
            invalidate(Dirty::style | Dirty::paint);
        }
    } else if (event.button == PointerButton::primary &&
               event.action == PointerAction::down) {
        pressed_ = true;
        invalidate(Dirty::style | Dirty::paint);
        event.handled = true;
    } else if (event.button == PointerButton::primary &&
               event.action == PointerAction::up) {
        pressed_ = false;
        invalidate(Dirty::style | Dirty::paint);
        event.handled = true;
    }
}

void Card::on_key(KeyEvent& event) {
    if (!interactive.read()) return;
    const bool activation = event.physical_key == PhysicalKey::space ||
                            event.physical_key == PhysicalKey::enter;
    if (!activation) return;
    if (event.action == KeyAction::down && !event.repeat && !pressed_) {
        keyboard_key_ = event.physical_key;
        pressed_ = true;
        invalidate(Dirty::style | Dirty::paint);
        event.handled = true;
    } else if (event.action == KeyAction::up && pressed_ &&
               keyboard_key_ == event.physical_key) {
        keyboard_key_ = 0U;
        pressed_ = false;
        invalidate(Dirty::style | Dirty::paint);
        event.handled = true;
        on_activate();
    }
}

void Card::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused_) {
        pressed_ = false;
        keyboard_key_ = 0U;
    }
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
}

void Card::on_activate() {
    if (!interactive.read()) return;
    if (selection_behavior.read() == CardSelectionBehavior::select_on_activation) {
        selected = true;
    } else if (selection_behavior.read() ==
               CardSelectionBehavior::toggle_on_activation) {
        selected = !selected.read();
    }
    if (is_alive()) activated_.emit(*this);
}

SemanticDescriptor Card::semantic_descriptor() const {
    SemanticDescriptor descriptor = Panel::semantic_descriptor();
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = interactive.read() || !descriptor.name.empty() ||
                         !descriptor.description.empty();
    if (interactive.read()) {
        descriptor.role = SemanticRole::list_item;
        descriptor.actions = {SemanticAction::focus, SemanticAction::press,
                              SemanticAction::select};
        if (selected.read()) descriptor.states |= SemanticState::selected;
        if (visual_status() == ControlVisualStatus::pending) {
            descriptor.states |= SemanticState::busy;
        } else if (visual_status() == ControlVisualStatus::invalid) {
            descriptor.states |= SemanticState::invalid;
        }
    }
    return descriptor;
}

bool Card::on_semantic_action(SemanticAction action, std::string_view value) {
    if (interactive.read() && action == SemanticAction::press) {
        on_activate();
        return true;
    }
    if (interactive.read() && action == SemanticAction::select) {
        selected = true;
        return true;
    }
    return Panel::on_semantic_action(action, value);
}

namespace {

constexpr ControlAnatomy card_anatomy{
    .control_id = "card",
    .parts = panel_detail::parts,
    .states = card_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& Card::anatomy() const noexcept {
    return card_anatomy;
}

void Card::publish_part_geometry(std::span<PartGeometry> geometry) const {
    if (has_background_override() || has_style_override()) {
        Panel::publish_part_geometry(geometry);
        return;
    }
    Parts record;
    record.scrollable = scrollable_parts();
    record.face = PartGeometry::from_bounds(client_rectangle());
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), panel_detail::extra_fields, record, geometry);
}

AnatomyStateSet Card::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (pressed_) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
    }
    if (hovered_) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::hot);
    }
    if (selected.read()) {
        states.insert(AnatomyState::selected);
    }
    return states;
}

Card::States Card::states() const {
    States record;
    read_states(card_detail::state_fields, anatomy_states(), record);
    return record;
}

namespace {

// The events Card adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 2> card_interactions{{
    {.name = "activated", .payload = "Card", .mutable_payload = true},
    {.name = "selected_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void Card::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), card_interactions.begin(), card_interactions.end());
}

// property_codemod begin Card (tools/property_families/card.json; generated, do not edit)

void Card::prepare_write(const decltype(selected)&, bool&) {
    require_mutable();
}

void Card::written(const decltype(selected)&) {
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
    publish_change(selected_changed_, selected.read());
}

void Card::prepare_write(const decltype(selection_behavior)&, CardSelectionBehavior& incoming) {
    require_mutable();
    switch (incoming) {
    case CardSelectionBehavior::manual:
    case CardSelectionBehavior::select_on_activation:
    case CardSelectionBehavior::toggle_on_activation:
        break;
    default:
        throw std::invalid_argument("card selection behavior is invalid");
    }
}

void Card::written(const decltype(selection_behavior)&) {
    invalidate(Dirty::semantics);
}

// property_codemod end Card

} // namespace gui_forms
