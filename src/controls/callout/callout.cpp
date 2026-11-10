#include "gui_forms/controls/callout/callout.hpp"

#include "gui_forms/controls/button_base/button/button.hpp"
#include "gui_forms/controls/label/label.hpp"
#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_placement/anchored_overlay_placement.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// Geometry in logical units: the card's preferred width and its bounds, its
// padding, the close mark's side, the gaps between the rows, and the gap
// between the card and its target.
constexpr double preferred_width = 280.0;
constexpr double minimum_width = 160.0;
constexpr double card_padding = 12.0;
constexpr double close_side = 16.0;
constexpr double close_gap = 8.0;
constexpr double row_gap = 6.0;
constexpr double action_gap = 10.0;
// The beak's depth and the width of its base; the gap between its tip and
// the target.
constexpr double beak_depth = 8.0;
constexpr double beak_width = 16.0;
constexpr double target_gap = 2.0;
constexpr double title_size_increase = 1.0;

constexpr std::array<AnatomyState, 2> close_part_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<Callout::Parts>, 5> callout_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Callout::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &Callout::Parts::face},
    {.declaration = {.name = "beak", .role = PartRole::beak, .composed_from = {}},
     .member = &Callout::Parts::beak},
    {.declaration = {.name = "close", .role = PartRole::glyph, .composed_from = {}, .states = close_part_states},
     .member = &Callout::Parts::close},
    {.declaration = {.name = "actuator", .role = PartRole::actuator, .composed_from = "button"},
     .member = &Callout::Parts::actuator},
}};

constexpr std::array<PartDeclaration, 5> callout_parts = part_declarations(callout_part_fields);

constexpr std::array<AnatomyState, 3> callout_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
};

constexpr ControlAnatomy callout_anatomy{
    .control_id = "callout",
    .parts = callout_parts,
    .states = callout_states,
    .custom_states = {},
};

} // namespace

// The full-client layer a shown callout sits in. Without light_dismiss it
// is transparent to the pointer, so presses outside the card reach the
// window; with it, it takes every press outside the card and closes the
// callout. It places the card at its target on every arrange.
class Callout::Layer final : public Control {
public:
    Layer(StableId stable_id, Callout& callout) : Control(std::move(stable_id)), callout_(callout) {
        set_paint_plane(PaintPlane::overlay);
        focusable = false;
    }

    [[nodiscard]] Size measure(Size available) override { return available; }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        const std::vector<Control::Ptr> children = snapshot_layout_children();
        const Control::Ptr target = callout_.target();
        Rect anchor{};
        if (target) anchor = (*target).absolute_bounds();
        const Rect own = absolute_bounds();
        // The anchor in the layer's coordinates (the layer fills the client).
        anchor.x -= own.x;
        anchor.y -= own.y;
        const Size client{final_bounds.width, final_bounds.height};
        for (const Control::Ptr& child : children) {
            if (!is_current_layout_child(child)) continue;
            const Size wanted = (*child).measure(client);
            AnchoredOverlayPlacement placement{};
            placement.preferred_size = wanted;
            placement.horizontal_alignment = OverlayHorizontalAlignment::near;
            placement.vertical_preference =
                callout_.prefers_above.read() ? OverlayVerticalPreference::above : OverlayVerticalPreference::below;
            placement.gap = target_gap;
            const AnchoredOverlayPlacementResult resolved = resolve_anchored_overlay(anchor, client, placement);
            // The beak points at the target's centre, from the side the
            // callout was placed on (after any flip for room).
            const double target_centre = anchor.x + anchor.width * 0.5 - resolved.bounds.x;
            callout_.place_beak(resolved.placed_above, target_centre);
            set_child_layout(child, resolved.bounds);
        }
    }

    [[nodiscard]] bool hit_test_local(Point) const override { return callout_.light_dismiss.read(); }

    void on_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::down) {
            event.handled = true;
            callout_.on_press_outside();
        } else if (event.action == PointerAction::up || event.action == PointerAction::wheel) {
            event.handled = true;
        }
    }

private:
    // The callout owns the layer while it is shown, and the layer is
    // released before the callout is.
    Callout& callout_;
};

Callout::Callout(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    focusable = true;
    cursor = CursorKind::arrow;
    const std::string prefix((*this).stable_id().value());
    title_label_ = make_control<Label>(StableId(prefix + ".title"));
    (*title_label_).text_wrapping = TextWrapping::word;
    (*title_label_).vertical_alignment = VerticalAlignment::near;
    text_label_ = make_control<Label>(StableId(prefix + ".text"));
    (*text_label_).text_wrapping = TextWrapping::word;
    (*text_label_).vertical_alignment = VerticalAlignment::near;
}

// add_child needs the shared owner, which make_control has made by now.
void Callout::initialize_control_tree() {
    add_child(title_label_);
    add_child(text_label_);
    sync_children();
}

Callout::~Callout() {
    action_token_.disconnect();
    overlay_revoked_.disconnect();
    overlay_.disconnect();
}

double Callout::card_width(Size available) const noexcept {
    const Rect requested = requested_bounds();
    const double wanted = requested.width > 0.0 ? requested.width : preferred_width;
    const double width = std::max(minimum_width, std::min(wanted, available.width));
    return width;
}

void Callout::sync_children() {
    FontSpec title_font = text_font(effective_theme().structure().typography.control);
    title_font.weight = 600;
    title_font.size += title_size_increase;
    (*title_label_).font = title_font;
    (*title_label_).text = title.read();
    set_child_layout_collapsed(title_label_, title.read().empty());
    (*text_label_).text = text.read();
    set_child_layout_collapsed(text_label_, text.read().empty());
}

void Callout::measure_rows(double width) {
    const double inner_width = std::max(0.0, width - card_padding * 2.0);
    const double title_width = std::max(0.0, inner_width - close_side - close_gap);
    const double text_width = title.read().empty() ? title_width : inner_width;
    rows_ = Rows{};
    if (!title.read().empty()) rows_.title = std::ceil((*title_label_).measure(Size{title_width, 1.0e6}).height);
    if (!text.read().empty()) rows_.text = std::ceil((*text_label_).measure(Size{text_width, 1.0e6}).height);
    if (action_) {
        const Size wanted = (*action_).measure(Size{inner_width, 1.0e6});
        rows_.action = Size{std::min(inner_width, std::ceil(wanted.width)), std::ceil(wanted.height)};
    }
}

Callout::Placement Callout::placement(Size size) const {
    Placement placed;
    // The card fills the callout but the beak's strip, above it when the
    // beak points up, else below.
    const double top = beak_down_ ? 0.0 : beak_depth;
    const double card_height = std::max(0.0, size.height - beak_depth);
    placed.face = Rect{0.0, top, size.width, card_height};
    // The beak keeps clear of the card's corners; it overlaps the card by a
    // unit so that it covers the card's edge across its base.
    const double lowest = card_padding + beak_width * 0.5;
    const double highest = std::max(lowest, size.width - card_padding - beak_width * 0.5);
    const double centre = std::clamp(beak_centre_, lowest, highest);
    const double beak_y = beak_down_ ? top + card_height - 1.0 : 0.0;
    placed.beak = Rect{centre - beak_width * 0.5, beak_y, beak_width, beak_depth + 1.0};
    placed.close = Rect{size.width - card_padding - close_side, top + card_padding, close_side, close_side};
    const double inner_width = std::max(0.0, size.width - card_padding * 2.0);
    // The title leaves room for the close mark; the text below a title
    // need not.
    const double title_width = std::max(0.0, inner_width - close_side - close_gap);
    double y = top + card_padding;
    if (!title.read().empty()) {
        placed.title = Rect{card_padding, y, title_width, rows_.title};
        y += placed.title.height + row_gap;
    }
    if (!text.read().empty()) {
        const double text_width = title.read().empty() ? title_width : inner_width;
        placed.text = Rect{card_padding, y, text_width, rows_.text};
        y += placed.text.height + row_gap;
    }
    y = std::max(y, top + card_padding + close_side + row_gap);
    if (action_) {
        placed.action =
            Rect{size.width - card_padding - rows_.action.width, y - row_gap + action_gap, rows_.action.width,
                 rows_.action.height};
    }
    return placed;
}

Size Callout::measure(Size available) {
    const double width = card_width(available);
    measure_rows(width);
    const Placement placed = placement(Size{width, 0.0});
    // The rows' bottoms are measured from the card's top, which lies below
    // the beak's strip when the beak points up.
    const double top = placed.face.y;
    double bottom = top + card_padding + close_side;
    if (!placed.title.empty()) bottom = std::max(bottom, placed.title.y + placed.title.height);
    if (!placed.text.empty()) bottom = std::max(bottom, placed.text.y + placed.text.height);
    if (!placed.action.empty()) bottom = std::max(bottom, placed.action.y + placed.action.height);
    const double height = bottom - top + card_padding + beak_depth;
    const Size measured{width, std::min(available.height, height)};
    return measured;
}

void Callout::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    measure_rows(final_bounds.width);
    const Placement placed = placement(Size{final_bounds.width, final_bounds.height});
    if (!title.read().empty()) set_child_layout(title_label_, placed.title);
    if (!text.read().empty()) set_child_layout(text_label_, placed.text);
    if (action_) set_child_layout(action_, placed.action);
}

void Callout::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const AnatomyStateSet states = anatomy_states();
    PartPaint shadow{.role = PartRole::shadow, .bounds = placed.face, .states = states,
                     .family = ControlVisualRole::tool_tip};
    paint_part(painter, *this, shadow);
    PartPaint face{.role = PartRole::face, .bounds = placed.face, .states = states,
                   .family = ControlVisualRole::tool_tip};
    paint_part(painter, *this, face);
    // The beak over the card's edge, towards the target.
    PartPaint beak{.role = PartRole::beak, .bounds = placed.beak, .states = states,
                   .family = ControlVisualRole::tool_tip};
    beak.mark = beak_down_ ? PartMark::arrow_down : PartMark::arrow_up;
    beak.key = PartKey{"beak", {}, {}};
    paint_part(painter, *this, beak);
    AnatomyStateSet close_states = states;
    if (hot_close_) close_states.insert(AnatomyState::hot);
    if (pressed_close_) close_states.insert(AnatomyState::pressed);
    PartPaint close_mark{.role = PartRole::glyph, .bounds = placed.close, .states = close_states,
                         .family = ControlVisualRole::tool_tip};
    close_mark.mark = PartMark::close;
    close_mark.key = PartKey{"close", {}, {}};
    paint_part(painter, *this, close_mark);
}

bool Callout::hit_test_local(Point local) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const bool inside = placed.face.contains(local) || placed.beak.contains(local);
    return inside;
}

void Callout::place_beak(bool pointing_down, double target_centre) {
    const bool moved = pointing_down != beak_down_ || target_centre != beak_centre_;
    if (!moved) return;
    const bool side_changed = pointing_down != beak_down_;
    beak_down_ = pointing_down;
    beak_centre_ = target_centre;
    // A new side moves the rows; a new centre only the beak.
    invalidate(side_changed ? Dirty::layout | Dirty::paint : Dirty::paint);
}

Insets Callout::visual_outsets() const noexcept {
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

// The close mark acts on release over it, as a button does.
void Callout::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const bool over = placed.close.contains(local);
    if (event.action == PointerAction::move || event.action == PointerAction::leave) {
        const bool hot = event.action == PointerAction::move && over;
        if (hot != hot_close_) {
            hot_close_ = hot;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action == PointerAction::wheel) {
        event.handled = true;
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        event.handled = true;
        if (!over) return;
        pressed_close_ = true;
        set_pointer_capture(true);
        invalidate(Dirty::paint);
        return;
    }
    if (event.action == PointerAction::up) {
        event.handled = true;
        if (!pressed_close_) return;
        pressed_close_ = false;
        set_pointer_capture(false);
        invalidate(Dirty::paint);
        if (over) finish(CalloutCloseReason::close_button);
    }
}

void Callout::on_key(KeyEvent& event) {
    on_key_bubble(event);
}

void Callout::on_key_bubble(KeyEvent& event) {
    if (event.action != KeyAction::down || event.physical_key != PhysicalKey::escape || !shown_) return;
    event.handled = true;
    finish(CalloutCloseReason::escape_key);
}

SemanticDescriptor Callout::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::dialog;
    descriptor.name = accessible_name.read().empty() ? title.read() : accessible_name.read();
    descriptor.description = accessible_description.read().empty() ? text.read() : accessible_description.read();
    descriptor.modal = false;
    descriptor.actions = {SemanticAction::close};
    descriptor.exposed = true;
    return descriptor;
}

bool Callout::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::close && shown_) {
        finish(CalloutCloseReason::close_button);
        return true;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

const ControlAnatomy& Callout::anatomy() const noexcept {
    return callout_anatomy;
}

AnatomyStateSet Callout::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    return states;
}

void Callout::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.face = PartGeometry::from_bounds(placed.face);
    record.beak = PartGeometry::from_bounds(placed.beak);
    record.close = PartGeometry::from_bounds(placed.close);
    if (hot_close_) record.close.states.insert(AnatomyState::hot);
    if (pressed_close_) record.close.states.insert(AnatomyState::pressed);
    if (action_) record.actuator = PartGeometry::from_bounds(placed.action);
    write_parts(anatomy(), callout_part_fields, record, geometry);
}

Callout::Parts Callout::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), callout_part_fields, published.parts(), record);
    return record;
}

// -- showing -------------------------------------------------------------------

bool Callout::show(const Control::Ptr& target) {
    require_mutable();
    Window* const window = target ? (*target).attached_window() : nullptr;
    if (shown_ || window == nullptr || parent()) return false;
    Window& host = *window;
    const Size client = host.client_size();
    std::shared_ptr<Layer> layer = make_control<Layer>(StableId(std::string(stable_id().value()) + ".layer"), *this);
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    target_ = target;
    (*layer).add_child(shared_from_this());
    overlay_ = host.open_overlay(target, layer);
    layer_ = layer;
    shown_ = true;
    if (Event<>* revoked = overlay_.closed_event()) {
        overlay_revoked_ = (*revoked).subscribe(*this, Delegate<>::bind<Callout, &Callout::on_overlay_revoked>(*this));
    }
    FocusScopeOptions options{};
    options.contain_focus = false;
    options.restore_focus = true;
    const Control::Ptr preferred = action_ ? Control::Ptr(action_) : shared_from_this();
    scope_ = host.begin_focus_scope(shared_from_this(), preferred, options);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
    return true;
}

bool Callout::close() {
    require_mutable();
    if (!shown_) return false;
    finish(CalloutCloseReason::programmatic);
    return true;
}

void Callout::on_overlay_revoked() {
    finish(CalloutCloseReason::owner_unavailable);
}

void Callout::on_press_outside() {
    if (light_dismiss.read()) finish(CalloutCloseReason::click_away);
}

void Callout::on_action(ButtonBase&) {
    if (!shown_) return;
    action_clicked_.emit();
    // The handler may have closed the callout already.
    if (shown_) finish(CalloutCloseReason::action);
}

void Callout::finish(CalloutCloseReason reason) {
    if (!shown_) return;
    shown_ = false;
    hot_close_ = false;
    pressed_close_ = false;
    Window* const host = layer_ ? (*layer_).attached_window() : nullptr;
    if (host != nullptr && scope_) static_cast<void>((*host).end_focus_scope(scope_));
    scope_ = {};
    overlay_revoked_.disconnect();
    const Control::Ptr layer = layer_;
    layer_.reset();
    if (layer) static_cast<void>((*layer).remove_child(runtime_id()));
    overlay_.disconnect();
    target_.reset();
    closed_.emit(reason);
}

void Callout::on_dispose() noexcept {
    // A disposed callout closes without raising closed.
    shown_ = false;
    action_token_.disconnect();
    overlay_revoked_.disconnect();
    Window* const host = layer_ ? (*layer_).attached_window() : nullptr;
    if (scope_ && host != nullptr) {
        try {
            static_cast<void>((*host).end_focus_scope(scope_));
        } catch (...) {
            // Disposal must not throw; the window ends the scope with the overlay.
        }
    }
    scope_ = {};
    overlay_.disconnect();
    layer_.reset();
    target_.reset();
    Control::on_dispose();
}

// -- property hooks --------------------------------------------------------------

void Callout::prepare_write(const decltype(title)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("Callout title must be valid UTF-8");
}

void Callout::written(const decltype(title)&) {
    sync_children();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void Callout::prepare_write(const decltype(text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("Callout text must be valid UTF-8");
}

void Callout::written(const decltype(text)&) {
    sync_children();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void Callout::prepare_write(const decltype(action_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("Callout action text must be valid UTF-8");
}

void Callout::written(const decltype(action_text)&) {
    const std::string& caption = action_text.read();
    if (caption.empty()) {
        if (action_) {
            action_token_.disconnect();
            static_cast<void>(remove_child((*action_).runtime_id()));
            action_.reset();
        }
    } else if (action_) {
        (*action_).text = caption;
    } else {
        action_ = make_control<Button>(StableId(std::string(stable_id().value()) + ".action"), caption);
        (*action_).visual_style = ButtonVisualStyle::accent;
        add_child(action_);
        action_token_ =
            (*action_).clicked().subscribe(*this, Delegate<ButtonBase&>::bind<Callout, &Callout::on_action>(*this));
    }
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

// property_codemod begin Callout (tools/property_families/callout.json; generated, do not edit)

namespace {

PropertyDescriptor callout_title_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "title";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The bold first line.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_callout_title() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        callout_title_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor callout_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The explanation under the title.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_callout_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        callout_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor callout_action_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "action_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The action button's text; empty for none.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_callout_action_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        callout_action_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor callout_light_dismiss_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "light_dismiss";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether a press outside closes the callout.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_callout_light_dismiss() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        callout_light_dismiss_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor callout_prefers_above_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "prefers_above";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Layout";
    descriptor.description = "Whether the callout opens above its target.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::arrange;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_callout_prefers_above() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        callout_prefers_above_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, Callout::callout_property_count>& Callout::callout_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 5U> own{{
        {"title", &describe_callout_title,
         &read_cell_entry<Callout, decltype(&Callout::title), &Callout::title>,
         &write_cell_entry<Callout, decltype(&Callout::title), &Callout::title>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"text", &describe_callout_text,
         &read_cell_entry<Callout, decltype(&Callout::text), &Callout::text>,
         &write_cell_entry<Callout, decltype(&Callout::text), &Callout::text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"action_text", &describe_callout_action_text,
         &read_cell_entry<Callout, decltype(&Callout::action_text), &Callout::action_text>,
         &write_cell_entry<Callout, decltype(&Callout::action_text), &Callout::action_text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"light_dismiss", &describe_callout_light_dismiss,
         &read_cell_entry<Callout, decltype(&Callout::light_dismiss), &Callout::light_dismiss>,
         &write_cell_entry<Callout, decltype(&Callout::light_dismiss), &Callout::light_dismiss>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"prefers_above", &describe_callout_prefers_above,
         &read_cell_entry<Callout, decltype(&Callout::prefers_above), &Callout::prefers_above>,
         &write_cell_entry<Callout, decltype(&Callout::prefers_above), &Callout::prefers_above>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Callout's property table names a property twice");
    static const std::array<PropertyEntry, callout_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> Callout::property_table() const noexcept {
    const std::array<PropertyEntry, callout_property_count>& entries = callout_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void Callout::prepare_write(const decltype(light_dismiss)&, bool&) {
    require_mutable();
}

void Callout::written(const decltype(light_dismiss)&) {
    invalidate(Dirty::semantics);
}

void Callout::prepare_write(const decltype(prefers_above)&, bool&) {
    require_mutable();
}

void Callout::written(const decltype(prefers_above)&) {
    invalidate(Dirty::arrange);
}

// property_codemod end Callout

namespace {

// The events Callout adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> callout_interactions{{
    {.name = "closed", .payload = "CalloutCloseReason", .mutable_payload = false},
    {.name = "action_clicked", .payload = "", .mutable_payload = false},
}};

} // namespace

void Callout::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), callout_interactions.begin(), callout_interactions.end());
}

} // namespace gui_forms
