#include "gui_forms/controls/popup_container_box.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/controls/anchored_overlay_layer.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <stxlib/result.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

struct PopupContainerBox::State final {
    std::shared_ptr<AnchoredOverlayLayer> layer_{};
    Event<const std::string&> text_changed_;
};

stxlib::Unique<PopupContainerBox::State> PopupContainerBox::make_state() {
    stxlib::OperationResult<stxlib::Unique<State>> made = stxlib::Unique<State>::make();
    if (made.failed()) throw std::bad_alloc();
    stxlib::Unique<State> state = std::move(made).take_value();
    return state;
}

Event<const std::string&>& PopupContainerBox::text_changed() noexcept {
    return (*state_).text_changed_;
}

namespace {

// The field's metrics, in logical units: the drop-down button's width, its
// arrow's side, the text's inset and the natural size.
constexpr double button_width = 22.0;
constexpr double disclosure_side = 10.0;
constexpr double text_inset = 6.0;
constexpr double natural_width = 200.0;
constexpr double natural_height = 26.0;

constexpr std::array<PartField<PopupContainerBox::Parts>, 6> popup_container_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &PopupContainerBox::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}, .states = {}},
     .member = &PopupContainerBox::Parts::well},
    {.declaration = {.name = "label", .role = PartRole::content, .composed_from = {}, .states = {}},
     .member = &PopupContainerBox::Parts::label},
    {.declaration = {.name = "actuator", .role = PartRole::actuator, .composed_from = {}, .states = {}},
     .member = &PopupContainerBox::Parts::actuator},
    {.declaration = {.name = "glyph", .role = PartRole::disclosure, .composed_from = {}, .states = {}},
     .member = &PopupContainerBox::Parts::glyph},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &PopupContainerBox::Parts::focus},
}};

constexpr std::array<PartDeclaration, 6> popup_container_parts = part_declarations(popup_container_part_fields);

constexpr std::array<AnatomyState, 9> popup_container_states{
    AnatomyState::normal,  AnatomyState::hot,     AnatomyState::enabled,
    AnatomyState::disabled, AnatomyState::focused, AnatomyState::focus_visible,
    AnatomyState::open,    AnatomyState::closed,  AnatomyState::pressed,
};

constexpr ControlAnatomy popup_container_anatomy{
    .control_id = "popup_container_box",
    .parts = popup_container_parts,
    .states = popup_container_states,
    .custom_states = {},
    .item_systems = {},
};

// The events PopupContainerBox adds to those of Control, in declaration
// order.
constexpr std::array<InteractionDeclaration, 3> popup_container_interactions{{
    {.name = "popup_opened", .payload = "", .mutable_payload = false},
    {.name = "popup_closed", .payload = "PopupClosed", .mutable_payload = false},
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
}};

bool valid_extent(double extent) noexcept {
    const bool valid = std::isfinite(extent) && extent >= 0.0 && extent <= PopupContainerBox::maximum_popup_extent;
    return valid;
}

} // namespace

PopupContainerBox::PopupContainerBox(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    cursor = stxlib::Optional<CursorKind>::of(CursorKind::arrow);
}

PopupContainerBox::~PopupContainerBox() {
    release_overlay();
}

// -- content ----------------------------------------------------------------------------------------

void PopupContainerBox::set_popup_content(Control::Ptr content) {
    require_mutable();
    if (shown_) throw std::logic_error("PopupContainerBox content cannot change while the popup is open");
    if (content && ((*content).parent() || (*content).attached() || content.get() == this)) {
        throw std::invalid_argument("PopupContainerBox content must be a control outside any window");
    }
    content_ = std::move(content);
    invalidate(Dirty::semantics);
}

void PopupContainerBox::set_popup_size(Size size) {
    require_mutable();
    if (!valid_extent(size.width) || !valid_extent(size.height)) {
        throw std::invalid_argument("PopupContainerBox popup size must be finite, not negative and within bounds");
    }
    popup_size_ = size;
}

// -- popup ------------------------------------------------------------------------------------------

// The popup is the content's measured size on each axis the application
// left at zero, and at least as wide as the field.
bool PopupContainerBox::show_popup() {
    State& state = *state_;
    require_mutable();
    Window* const host = attached_window();
    if (shown_ || host == nullptr || !content_ || !effectively_enabled()) return false;
    const Control::Ptr retained = shared_from_this();
    const Rect field = absolute_bounds();
    Size size = popup_size_;
    if (size.width <= 0.0 || size.height <= 0.0) {
        const Size desired = (*content_).measure(Size{maximum_popup_extent, maximum_popup_extent});
        if (!is_alive()) return false;
        if (size.width <= 0.0) size.width = std::min(maximum_popup_extent, std::ceil(desired.width));
        if (size.height <= 0.0) size.height = std::min(maximum_popup_extent, std::ceil(desired.height));
    }
    size.width = std::max(size.width, field.width);
    AnchoredOverlayPlacement placement{};
    placement.preferred_size = size;
    placement.gap = 1.0;
    const std::string prefix(stable_id().value());
    state.layer_ = make_control<AnchoredOverlayLayer>(StableId(prefix + ".popup.layer"), retained, placement);
    (*state.layer_).set_content(content_);
    overlay_ = (*host).open_overlay(retained, state.layer_);
    dismissal_ = (*state.layer_).dismiss_requested().subscribe(
        *this, Delegate<OverlayDismissReason>::bind<PopupContainerBox, &PopupContainerBox::on_dismiss>(*this));
    if (Event<>* revoked = overlay_.closed_event()) {
        overlay_revoked_ = (*revoked).subscribe(
            *this, Delegate<>::bind<PopupContainerBox, &PopupContainerBox::on_overlay_revoked>(*this));
    }
    shown_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    // The content takes the focus when it can (its focus target, or
    // itself); otherwise the field keeps it.
    static_cast<void>((*host).request_focus(content_));
    if (!is_alive()) return true;
    publish_change(popup_opened_);
    return true;
}

bool PopupContainerBox::close_popup(PopupCloseReason reason) {
    if (!shown_) return false;
    const Control::Ptr retained = shared_from_this();
    const bool return_focus = focus_within_popup();
    shown_ = false;
    release_overlay();
    invalidate(Dirty::paint | Dirty::semantics);
    Window* const host = attached_window();
    if (return_focus && host != nullptr) static_cast<void>((*host).request_focus(retained));
    if (!is_alive()) return true;
    const PopupClosed closed{reason};
    publish_change(popup_closed_, closed);
    return true;
}

// Whether the window's focus is in the popup: the content or a control in
// it.
bool PopupContainerBox::focus_within_popup() const {
    const Window* const host = window();
    if (host == nullptr || !(*state_).layer_) return false;
    for (Control::Ptr at = (*host).focused_control(); at; at = (*at).parent()) {
        if (at.get() == (*state_).layer_.get()) return true;
    }
    return false;
}

void PopupContainerBox::release_overlay() noexcept {
    State& state = *state_;
    dismissal_.disconnect();
    overlay_revoked_.disconnect();
    if (state.layer_) (*state.layer_).clear_content();
    overlay_.disconnect();
    state.layer_.reset();
}

void PopupContainerBox::on_dismiss(OverlayDismissReason reason) {
    const PopupCloseReason closed =
        reason == OverlayDismissReason::escape_key ? PopupCloseReason::escape : PopupCloseReason::click_away;
    static_cast<void>(close_popup(closed));
}

// The window closed the overlay itself (as when it closes): the popup is
// cancelled.
void PopupContainerBox::on_overlay_revoked() {
    State& state = *state_;
    if (!shown_) return;
    shown_ = false;
    dismissal_.disconnect();
    overlay_revoked_.disconnect();
    if (state.layer_) (*state.layer_).clear_content();
    state.layer_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
    const PopupClosed closed{PopupCloseReason::cancelled};
    publish_change(popup_closed_, closed);
}

void PopupContainerBox::on_detached_from_window() noexcept {
    shown_ = false;
    release_overlay();
    hot_ = false;
    focused_ = false;
}

// -- layout and painting ------------------------------------------------------------------------------

FontSpec PopupContainerBox::field_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.field);
    return font;
}

// The drop-down button sits at the trailing end (the left, right to left).
PopupContainerBox::Placement PopupContainerBox::placement() const {
    Placement placed;
    const Rect bounds = client_rectangle();
    const double button = std::min(button_width, bounds.width);
    placed.well = bounds;
    placed.actuator = Rect{std::max(0.0, bounds.width - button), 1.0, std::max(0.0, button - 1.0),
                           std::max(0.0, bounds.height - 2.0)};
    placed.glyph = Rect{std::floor(placed.actuator.x + (placed.actuator.width - disclosure_side) * 0.5),
                        std::floor((bounds.height - disclosure_side) * 0.5), disclosure_side, disclosure_side};
    placed.label = Rect{text_inset, 0.0, std::max(0.0, bounds.width - button - text_inset - 3.0), bounds.height};
    if (effective_layout_direction() == LayoutDirection::right_to_left) {
        placed.actuator.x = bounds.width - placed.actuator.x - placed.actuator.width;
        placed.glyph.x = bounds.width - placed.glyph.x - placed.glyph.width;
        placed.label.x = bounds.width - placed.label.x - placed.label.width;
    }
    return placed;
}

Size PopupContainerBox::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : natural_width;
    const double height = requested.height > 0.0 ? requested.height : natural_height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void PopupContainerBox::on_paint(Painter& painter, Rect) {
    const Placement placed = placement();
    const AnatomyStateSet states = anatomy_states();
    PartPaint well{.role = PartRole::well, .bounds = placed.well, .states = states,
                   .family = ControlVisualRole::editor};
    paint_part(painter, *this, well);
    PartPaint drop_down{.role = PartRole::actuator, .bounds = placed.actuator, .states = states,
                        .family = ControlVisualRole::choice};
    paint_part(painter, *this, drop_down);
    PartPaint disclosure{.role = PartRole::disclosure, .bounds = placed.glyph, .states = states,
                         .family = ControlVisualRole::choice};
    disclosure.mark = shown_ ? PartMark::arrow_up : PartMark::arrow_down;
    paint_part(painter, *this, disclosure);
    PartPaint content{.role = PartRole::content, .bounds = placed.label, .states = states,
                      .family = ControlVisualRole::editor};
    content.text = lend_paint_text(text.read());
    content.font = field_font();
    paint_part(painter, *this, content);
    PartPaint ring{.role = PartRole::focus, .bounds = placed.well, .states = states,
                   .family = ControlVisualRole::editor};
    paint_part(painter, *this, ring);
}

// -- input ------------------------------------------------------------------------------------------

void PopupContainerBox::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

void PopupContainerBox::on_mouse_move(MouseEventArgs& arguments) {
    if (eligible_for_input() && !hot_) {
        hot_ = true;
        invalidate(Dirty::paint);
    }
    Control::on_mouse_move(arguments);
}

void PopupContainerBox::on_mouse_leave() {
    if (hot_) {
        hot_ = false;
        invalidate(Dirty::paint);
    }
    Control::on_mouse_leave();
}

// A press on the field opens the popup, or cancels an open one.
void PopupContainerBox::on_mouse_down(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.button == PointerButton::primary) {
        arguments.handled = true;
        const Control::Ptr retained = shared_from_this();
        Window* const host = attached_window();
        if (host != nullptr && !shown_) static_cast<void>((*host).request_focus(retained));
        if (!is_alive()) return;
        if (shown_) {
            static_cast<void>(close_popup(PopupCloseReason::cancelled));
        } else {
            static_cast<void>(show_popup());
        }
        if (!is_alive()) return;
    }
    Control::on_mouse_down(arguments);
}

// F4, Alt+Down and Alt+Up open the popup, or cancel an open one; Escape
// closes it.
void PopupContainerBox::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled || !eligible_for_input()) return;
    const std::uint32_t key = arguments.physical_key;
    const bool alt = arguments.modifiers == Modifier::alt;
    const bool plain = arguments.modifiers == Modifier::none;
    // Escape reaches the field when the content took no focus.
    if (plain && key == PhysicalKey::escape && shown_) {
        arguments.handled = true;
        static_cast<void>(close_popup(PopupCloseReason::escape));
        return;
    }
    if (!(plain && key == PhysicalKey::f4) && !(alt && (key == PhysicalKey::down || key == PhysicalKey::up))) {
        return;
    }
    arguments.handled = true;
    if (shown_) {
        static_cast<void>(close_popup(PopupCloseReason::cancelled));
        return;
    }
    static_cast<void>(show_popup());
}

// -- semantics --------------------------------------------------------------------------------------

SemanticDescriptor PopupContainerBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::combo_box;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.value = text.read();
    descriptor.has_popup = true;
    if (content_) descriptor.controlled_panel = std::string((*content_).stable_id().value());
    if (shown_) descriptor.states |= SemanticState::expanded;
    descriptor.states |= SemanticState::read_only;
    descriptor.actions = {SemanticAction::focus, shown_ ? SemanticAction::collapse : SemanticAction::expand};
    descriptor.exposed = true;
    return descriptor;
}

bool PopupContainerBox::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::expand && !shown_) {
        const bool opened = show_popup();
        return opened;
    }
    if (action == SemanticAction::collapse && shown_) {
        const bool closed = close_popup(PopupCloseReason::cancelled);
        return closed;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

// -- anatomy ----------------------------------------------------------------------------------------

const ControlAnatomy& PopupContainerBox::anatomy() const noexcept {
    return popup_container_anatomy;
}

AnatomyStateSet PopupContainerBox::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (hot_) states.insert(AnatomyState::hot);
    states.insert(shown_ ? AnatomyState::open : AnatomyState::closed);
    if (shown_) states.insert(AnatomyState::pressed);
    if (focused_) states.insert(AnatomyState::focused);
    const Window* const host = window();
    if (focused_ && host != nullptr && (*host).focus_cue_visible()) states.insert(AnatomyState::focus_visible);
    return states;
}

void PopupContainerBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Placement placed = placement();
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.well = PartGeometry::from_bounds(placed.well);
    record.label = PartGeometry::from_bounds(placed.label);
    record.actuator = PartGeometry::from_bounds(placed.actuator);
    record.glyph = PartGeometry::from_bounds(placed.glyph);
    record.focus = PartGeometry::from_bounds(placed.well);
    write_parts(anatomy(), popup_container_part_fields, record, geometry);
}

PopupContainerBox::Parts PopupContainerBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), popup_container_part_fields, published.parts(), record);
    return record;
}

void PopupContainerBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), popup_container_interactions.begin(), popup_container_interactions.end());
}

// property_codemod begin PopupContainerBox (tools/property_families/popup_container_box.json; generated, do not edit)

namespace {

PropertyDescriptor popup_container_box_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The text the field shows.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_popup_container_box_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        popup_container_box_text_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

} // namespace

namespace {

// The table's text connector: text_changed_ lives in State, so the entry
// reaches it through the public accessor rather than a member pointer.
SubscriptionToken connect_popup_container_box_text_changed(Component& owner, Component& subscriber, std::function<void()> changed) {
    PopupContainerBox& box = static_cast<PopupContainerBox&>(owner);
    const detail::EventChangeConnector connector(box.text_changed());
    SubscriptionToken token = connector(subscriber, std::move(changed));
    return token;
}

} // namespace

const std::array<PropertyEntry, PopupContainerBox::popup_container_box_property_count>& PopupContainerBox::popup_container_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"text", &describe_popup_container_box_text,
         &read_cell_entry<PopupContainerBox, decltype(&PopupContainerBox::text), &PopupContainerBox::text>,
         &write_cell_entry<PopupContainerBox, decltype(&PopupContainerBox::text), &PopupContainerBox::text>,
         nullptr, nullptr, nullptr,
         &connect_popup_container_box_text_changed},
    }};
    static_assert(property_names_distinct(own), "PopupContainerBox's property table names a property twice");
    static const std::array<PropertyEntry, popup_container_box_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> PopupContainerBox::property_table() const noexcept {
    const std::array<PropertyEntry, popup_container_box_property_count>& entries = popup_container_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void PopupContainerBox::prepare_write(const decltype(text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("PopupContainerBox text must be valid UTF-8");
}

void PopupContainerBox::written(const decltype(text)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    on_text_changed();
}

void PopupContainerBox::on_text_changed() {
    publish_change((*state_).text_changed_, text.read());
}

// property_codemod end PopupContainerBox

} // namespace gui_forms
