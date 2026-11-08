#include "gui_forms/controls/toast/toast.hpp"

#include "gui_forms/controls/button_base/button/button.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The toast's metrics, in logical units: the space inside the face around
// everything, the icon's and the close mark's side, the gap between
// neighbouring parts, and the widest the message runs before it is cut.
constexpr double inset = 10.0;
constexpr double icon_side = 16.0;
constexpr double close_side = 16.0;
constexpr double gap = 8.0;
constexpr double maximum_message_width = 420.0;

// The severity's icon colour until T4's floor gives the status colours
// roles; the same provisional colours as InfoBanner's keyline.
Color severity_color(NoticeSeverity severity) noexcept {
    switch (severity) {
    case NoticeSeverity::information: return Color::rgba(38, 114, 185);
    case NoticeSeverity::warning: return Color::rgba(196, 132, 0);
    case NoticeSeverity::success: return Color::rgba(46, 139, 87);
    case NoticeSeverity::error: return Color::rgba(196, 43, 28);
    default: break;
    }
    return Color::rgba(38, 114, 185);
}

PartMark severity_mark(NoticeSeverity severity) noexcept {
    switch (severity) {
    case NoticeSeverity::information: return PartMark::information;
    case NoticeSeverity::warning: return PartMark::warning;
    case NoticeSeverity::success: return PartMark::success;
    case NoticeSeverity::error: return PartMark::error;
    default: break;
    }
    return PartMark::information;
}

constexpr std::array<AnatomyState, 2> close_part_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<Toast::Parts>, 6> toast_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Toast::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &Toast::Parts::face},
    {.declaration = {.name = "icon", .role = PartRole::glyph, .composed_from = {}},
     .member = &Toast::Parts::icon},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &Toast::Parts::label},
    {.declaration = {.name = "actuator", .role = PartRole::actuator, .composed_from = "button"},
     .member = &Toast::Parts::actuator},
    {.declaration = {.name = "close", .role = PartRole::glyph, .composed_from = {}, .states = close_part_states},
     .member = &Toast::Parts::close},
}};

constexpr std::array<PartDeclaration, 6> toast_parts = part_declarations(toast_part_fields);

constexpr std::array<AnatomyState, 3> toast_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
};

// One custom state per severity, in NoticeSeverity's order, so a
// severity's number is its state's index (`:family(notice):state(error)`).
constexpr std::array<CustomStateDeclaration, 4> toast_custom_states{{
    {.name = "information"},
    {.name = "warning"},
    {.name = "success"},
    {.name = "error"},
}};

constexpr ControlAnatomy toast_anatomy{
    .control_id = "toast",
    .parts = toast_parts,
    .states = toast_states,
    .custom_states = toast_custom_states,
};

// The event Toast adds to those of Control.
constexpr std::array<InteractionDeclaration, 1> toast_interactions{{
    {.name = "action_invoked", .payload = "", .mutable_payload = false},
}};

ToastContent checked_content(ToastContent content) {
    if (content.message.empty() || !validate_utf8(content.message).valid() ||
        !validate_utf8(content.action_text).valid()) {
        throw std::invalid_argument("Toast message must be non-empty and its texts valid UTF-8");
    }
    return content;
}

} // namespace

Toast::Toast(StableId stable_id, ToastContent content)
    : Control(std::move(stable_id)), content_(checked_content(std::move(content))) {
    set_focusable(false);
}

void Toast::initialize_control_tree() {
    if (content_.action_text.empty() || action_) return;
    std::string id(stable_id().value());
    id += ".action";
    std::shared_ptr<Button> button = make_control<Button>(StableId(id), content_.action_text);
    (*button).visual_style = ButtonVisualStyle::command;
    on((*button).clicked(), *this, &Toast::on_action_clicked);
    add_child(button);
    action_ = std::move(button);
}

bool Toast::dismiss(ToastDismissReason reason) {
    Window* const host = attached_window();
    if (host == nullptr) return false;
    const bool dismissed = (*host).dismiss_toast(*this, reason);
    return dismissed;
}

bool Toast::close() {
    require_mutable();
    const bool dismissed = dismiss(ToastDismissReason::closed);
    return dismissed;
}

// The action's handlers run first; the toast is dismissed after them,
// which takes it out of its window.
void Toast::on_action_clicked(ButtonBase&) {
    const Control::Ptr retained = shared_from_this();
    publish_change(action_invoked_);
    if (!is_alive()) return;
    static_cast<void>(dismiss(ToastDismissReason::action));
}

FontSpec Toast::message_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.control);
    return font;
}

Toast::Placement Toast::placement(Size size) const {
    Placement placed;
    placed.face = Rect{0.0, 0.0, size.width, size.height};
    const Size message = resolve_text_layout_utf8(content_.message, message_font()).logical_size;
    const double message_width = std::ceil(std::min(message.width, maximum_message_width));
    const double message_height = std::ceil(message.height);
    double tallest = std::max(icon_side, message_height);
    if (action_) tallest = std::max(tallest, action_size_.height);
    double trailing_natural = inset;
    if (content_.closable) trailing_natural += close_side + gap;
    if (action_) trailing_natural += action_size_.width + gap;
    const double text_x = inset + icon_side + gap;
    placed.natural = Size{text_x + message_width + trailing_natural, inset + tallest + inset};

    const double centre = size.height * 0.5;
    placed.icon = Rect{inset, std::floor(centre - icon_side * 0.5), icon_side, icon_side};
    double trailing = size.width - inset;
    if (content_.closable) {
        placed.close = Rect{trailing - close_side, std::floor(centre - close_side * 0.5), close_side, close_side};
        trailing = placed.close.x - gap;
    }
    if (action_ && action_size_.width > 0.0) {
        const double width = std::min(action_size_.width, std::max(0.0, trailing - text_x));
        placed.actuator = Rect{trailing - width, std::floor(centre - action_size_.height * 0.5), width,
                               action_size_.height};
        trailing = placed.actuator.x - gap;
    }
    placed.label = Rect{text_x, std::floor(centre - message_height * 0.5), std::max(0.0, trailing - text_x),
                        message_height};
    return placed;
}

// The natural size within the space offered; a requested size wins on
// either axis. The action button is measured first.
Size Toast::measure(Size available) {
    const Rect requested = requested_bounds();
    action_size_ = Size{};
    const Control::Ptr retained = action_;
    if (retained && is_current_layout_child(retained)) {
        const Size desired = (*retained).measure(available);
        if (!is_alive()) return Size{};
        action_size_ = desired;
    }
    const Placement placed = placement(Size{});
    double width = placed.natural.width;
    double height = placed.natural.height;
    if (requested.width > 0.0) width = requested.width;
    if (requested.height > 0.0) height = requested.height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

// The action button is measured again against the arranged size, since a
// parent that places the toast by its requested bounds never measures it.
void Toast::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Control::Ptr retained = action_;
    if (!retained || !is_current_layout_child(retained)) return;
    const Rect client = client_rectangle();
    const Size desired = (*retained).measure(Size{client.width, client.height});
    if (!is_alive()) return;
    action_size_ = desired;
    const Placement placed = placement(Size{client.width, client.height});
    set_child_layout(retained, placed.actuator);
}

void Toast::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const AnatomyStateSet states = anatomy_states();
    // The overlay shadow tooltips cast (RB-37), until T4 gives notices a
    // shadow of their own; then the notice face.
    PartPaint shadow{.role = PartRole::shadow, .bounds = placed.face, .states = states,
                     .family = ControlVisualRole::tool_tip};
    paint_part(painter, *this, shadow);
    PartPaint face{.role = PartRole::face, .bounds = placed.face, .states = states, .family = ControlVisualRole::notice};
    paint_part(painter, *this, face);
    PartPaint icon{.role = PartRole::glyph, .bounds = placed.icon, .states = states, .family = ControlVisualRole::notice};
    icon.mark = severity_mark(content_.severity);
    icon.authored_foreground = severity_color(content_.severity);
    paint_part(painter, *this, icon);
    PartPaint label{.role = PartRole::label, .bounds = placed.label, .states = states,
                    .family = ControlVisualRole::notice};
    label.text = content_.message;
    label.font = message_font();
    paint_part(painter, *this, label);
    if (content_.closable) {
        AnatomyStateSet close_states = states;
        if (hot_close_) close_states.insert(AnatomyState::hot);
        if (pressed_close_) close_states.insert(AnatomyState::pressed);
        PartPaint close{.role = PartRole::glyph, .bounds = placed.close, .states = close_states,
                        .family = ControlVisualRole::notice};
        close.mark = PartMark::close;
        paint_part(painter, *this, close);
    }
}

Insets Toast::visual_outsets() const noexcept {
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

// The close mark acts on release over it, as a button does.
void Toast::on_pointer(PointerEvent& event) {
    if (!eligible_for_input() || !content_.closable) return;
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const bool over = placed.close.contains(event.position);
    if (event.action == PointerAction::move || event.action == PointerAction::leave) {
        const bool hot = event.action == PointerAction::move && over;
        if (hot != hot_close_) {
            hot_close_ = hot;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down && over) {
        pressed_close_ = true;
        set_pointer_capture(true);
        invalidate(Dirty::paint);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::up && pressed_close_) {
        pressed_close_ = false;
        set_pointer_capture(false);
        invalidate(Dirty::paint);
        event.handled = true;
        if (over) static_cast<void>(dismiss(ToastDismissReason::closed));
    }
}

SemanticDescriptor Toast::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::live_region;
    descriptor.name = accessible_name.read().empty() ? content_.message : accessible_name.read();
    descriptor.description = accessible_description.read();
    if (content_.closable) descriptor.actions = {SemanticAction::close};
    // A polite live region spoken as it appears (owner ruling, 2026-11-08):
    // UI Automation's live-region event, AppKit's announcement request and
    // AT-SPI's object:announcement (semantic_announcement_due).
    descriptor.announces_on_appearance = true;
    descriptor.exposed = true;
    return descriptor;
}

bool Toast::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::close && content_.closable) {
        const bool dismissed = dismiss(ToastDismissReason::closed);
        return dismissed;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

const ControlAnatomy& Toast::anatomy() const noexcept {
    return toast_anatomy;
}

AnatomyStateSet Toast::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    states.insert(CustomStateIndex{static_cast<std::uint8_t>(content_.severity)});
    return states;
}

void Toast::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.face = PartGeometry::from_bounds(placed.face);
    record.icon = PartGeometry::from_bounds(placed.icon);
    record.label = PartGeometry::from_bounds(placed.label);
    if (action_) record.actuator = PartGeometry::from_bounds(placed.actuator);
    if (content_.closable) {
        record.close = PartGeometry::from_bounds(placed.close);
        if (hot_close_) record.close.states.insert(AnatomyState::hot);
        if (pressed_close_) record.close.states.insert(AnatomyState::pressed);
    }
    write_parts(anatomy(), toast_part_fields, record, geometry);
}

Toast::Parts Toast::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), toast_part_fields, published.parts(), record);
    return record;
}

void Toast::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), toast_interactions.begin(), toast_interactions.end());
}

} // namespace gui_forms
