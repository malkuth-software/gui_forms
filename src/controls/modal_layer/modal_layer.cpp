#include "gui_forms/controls/modal_layer/modal_layer.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace gui_forms {
namespace {

// The margin the content keeps from the client edges, in logical units.
constexpr double window_margin = 16.0;

constexpr std::array<PartField<ModalLayer::Parts>, 3> modal_layer_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ModalLayer::Parts::region},
    {.declaration = {.name = "scrim", .role = PartRole::scrim, .composed_from = {}},
     .member = &ModalLayer::Parts::scrim},
    {.declaration = {.name = "shadow", .role = PartRole::shadow, .composed_from = {}},
     .member = &ModalLayer::Parts::shadow},
}};

constexpr std::array<PartDeclaration, 3> modal_layer_parts = part_declarations(modal_layer_part_fields);

constexpr std::array<AnatomyState, 3> modal_layer_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
};

constexpr ControlAnatomy modal_layer_anatomy{
    .control_id = "modal_layer",
    .parts = modal_layer_parts,
    .states = modal_layer_states,
    .custom_states = {},
};

} // namespace

ModalLayer::ModalLayer(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    focusable = false;
    cursor = CursorKind::arrow;
}

ModalLayer::~ModalLayer() {
    overlay_revoked_.disconnect();
    overlay_.disconnect();
}

Size ModalLayer::measure(Size available) {
    return available;
}

void ModalLayer::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    content_bounds_ = Rect{};
    if (!content_ || !is_current_layout_child(content_)) return;
    const double margin = window_margin * effective_text_scale();
    const Size available{std::max(0.0, final_bounds.width - margin * 2.0),
                         std::max(0.0, final_bounds.height - margin * 2.0)};
    const Size wanted = (*content_).measure(available);
    const double width = std::min(wanted.width, available.width);
    const double height = std::min(wanted.height, available.height);
    content_bounds_ = Rect{std::floor((final_bounds.width - width) * 0.5),
                           std::floor((final_bounds.height - height) * 0.5), width, height};
    set_child_layout(content_, content_bounds_);
}

Rect ModalLayer::content_slot() const noexcept {
    return content_bounds_;
}

void ModalLayer::on_paint(Painter& painter, Rect) {
    const Rect slot = content_slot();
    if (slot.empty()) return;
    const AnatomyStateSet states = anatomy_states();
    PartPaint scrim{.role = PartRole::scrim, .bounds = client_rectangle(), .states = states,
                    .family = ControlVisualRole::window};
    scrim.key = PartKey{"scrim", {}, {}};
    paint_part(painter, *this, scrim);
    PartPaint shadow{.role = PartRole::shadow, .bounds = slot, .states = states,
                     .family = ControlVisualRole::tool_tip};
    paint_part(painter, *this, shadow);
}

bool ModalLayer::hit_test_local(Point) const {
    return true;
}

void ModalLayer::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::down || event.action == PointerAction::up ||
        event.action == PointerAction::wheel) {
        event.handled = true;
    }
}

void ModalLayer::on_key_bubble(KeyEvent& event) {
    if (event.action != KeyAction::down || event.physical_key != PhysicalKey::escape) return;
    if (!shown_ || !close_on_escape.read()) return;
    event.handled = true;
    static_cast<void>(close(DialogResult::cancel));
}

SemanticDescriptor ModalLayer::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::dialog;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.modal = true;
    if (close_on_escape.read()) descriptor.actions = {SemanticAction::close};
    descriptor.exposed = shown_;
    return descriptor;
}

bool ModalLayer::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::close && shown_ && close_on_escape.read()) {
        const bool closed = close(DialogResult::cancel);
        return closed;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

const ControlAnatomy& ModalLayer::anatomy() const noexcept {
    return modal_layer_anatomy;
}

void ModalLayer::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    const Rect slot = content_slot();
    if (!slot.empty()) record.scrim = PartGeometry::from_bounds(client_rectangle());
    record.shadow = PartGeometry::from_bounds(slot);
    write_parts(anatomy(), modal_layer_part_fields, record, geometry);
}

ModalLayer::Parts ModalLayer::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), modal_layer_part_fields, published.parts(), record);
    return record;
}

bool ModalLayer::show(const Control::Ptr& owner, Control::Ptr content) {
    require_mutable();
    Window* const window = owner ? (*owner).attached_window() : nullptr;
    if (shown_ || window == nullptr || parent() || !content || (*content).parent()) return false;
    Window& host = *window;
    const Size client = host.client_size();
    set_requested_bounds({0.0, 0.0, client.width, client.height});
    content_ = std::move(content);
    add_child(content_);
    overlay_ = host.open_overlay(owner, shared_from_this());
    shown_ = true;
    if (Event<>* revoked = overlay_.closed_event()) {
        overlay_revoked_ =
            (*revoked).subscribe(*this, Delegate<>::bind<ModalLayer, &ModalLayer::on_overlay_revoked>(*this));
    }
    scope_ = host.begin_focus_scope(content_);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
    return true;
}

bool ModalLayer::close(DialogResult result) {
    require_mutable();
    if (!shown_) return false;
    ModalLayerClosingEvent request{result, false};
    closing_.emit(request);
    // A handler may have closed the layer itself.
    if (!shown_) return true;
    if (request.cancel) return false;
    finish(result);
    return true;
}

void ModalLayer::on_overlay_revoked() {
    finish(DialogResult::cancel);
}

void ModalLayer::finish(DialogResult result) {
    if (!shown_) return;
    shown_ = false;
    Window* const host = attached_window();
    if (host != nullptr && scope_) static_cast<void>((*host).end_focus_scope(scope_));
    scope_ = {};
    overlay_revoked_.disconnect();
    const Control::Ptr content = content_;
    content_.reset();
    content_bounds_ = Rect{};
    if (content) static_cast<void>(remove_child((*content).runtime_id()));
    // Closing the overlay detaches the layer; the caller keeps it.
    overlay_.disconnect();
    result_ = result;
    closed_.emit(result);
}

void ModalLayer::on_dispose() noexcept {
    // A disposed layer closes without raising closing or closed.
    shown_ = false;
    overlay_revoked_.disconnect();
    Window* const host = attached_window();
    if (scope_ && host != nullptr) {
        try {
            static_cast<void>((*host).end_focus_scope(scope_));
        } catch (...) {
            // Disposal must not throw; the window ends the scope with the overlay.
        }
    }
    scope_ = {};
    content_.reset();
    overlay_.disconnect();
    Control::on_dispose();
}

// property_codemod begin ModalLayer (tools/property_families/modal_layer.json; generated, do not edit)

namespace {

PropertyDescriptor modal_layer_close_on_escape_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "close_on_escape";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether Escape asks to close the layer.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_modal_layer_close_on_escape() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        modal_layer_close_on_escape_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, ModalLayer::modal_layer_property_count>& ModalLayer::modal_layer_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"close_on_escape", &describe_modal_layer_close_on_escape,
         &read_cell_entry<ModalLayer, decltype(&ModalLayer::close_on_escape), &ModalLayer::close_on_escape>,
         &write_cell_entry<ModalLayer, decltype(&ModalLayer::close_on_escape), &ModalLayer::close_on_escape>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ModalLayer's property table names a property twice");
    static const std::array<PropertyEntry, modal_layer_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ModalLayer::property_table() const noexcept {
    const std::array<PropertyEntry, modal_layer_property_count>& entries = modal_layer_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ModalLayer::prepare_write(const decltype(close_on_escape)&, bool&) {
    require_mutable();
}

void ModalLayer::written(const decltype(close_on_escape)&) {
    invalidate(Dirty::semantics);
}

// property_codemod end ModalLayer

namespace {

// The events ModalLayer adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> modal_layer_interactions{{
    {.name = "closing", .payload = "ModalLayerClosingEvent", .mutable_payload = true},
    {.name = "closed", .payload = "DialogResult", .mutable_payload = false},
}};

} // namespace

void ModalLayer::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), modal_layer_interactions.begin(), modal_layer_interactions.end());
}

} // namespace gui_forms
