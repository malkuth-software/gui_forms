#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/components/help_provider/help_provider.hpp"
#include "gui_forms/window.hpp"
#include "tool_tips/tool_tip_presenter.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "../dispatcher/state/dispatcher_state.hpp"
#include "../display/chunk/display_chunk.hpp"
#include "../display/recording_painter/recording_painter.hpp"
#include "../display/replay/replay_display_chunk.hpp"
#include "../scheduler/request/scheduled_frame_request.hpp"
#include "accelerator/accelerator_attachment.hpp"
#include "popup/popup_attachment.hpp"
#include "../theme/theme_compiler/control_inline_style.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace gui_forms {

namespace {

// Whether GUI_FORMS_TRACE_LAYOUT_INVALIDATION was set when the first layout
// mark asked. Read once: a layout mark is a hot path, and getenv walks the
// environment on every call. The trace is a diagnostic switch for a whole
// run, so a variable set later in the process is not seen.
bool layout_invalidation_traced() noexcept {
    static const bool traced = std::getenv("GUI_FORMS_TRACE_LAYOUT_INVALIDATION") != nullptr;
    return traced;
}

constexpr std::uint32_t maximum_layout_passes = 4;
constexpr std::uint32_t maximum_semantic_snapshot_passes = 4;

[[nodiscard]] std::optional<char32_t> physical_mnemonic(
    std::uint32_t physical_key) noexcept {
    // USB HID keyboard usages are contiguous for A-Z and for 1-9,0.
    if (physical_key >= 0x04U && physical_key <= 0x1DU) {
        return U'a' + static_cast<char32_t>(physical_key - 0x04U);
    }
    if (physical_key >= 0x1EU && physical_key <= 0x26U) {
        return U'1' + static_cast<char32_t>(physical_key - 0x1EU);
    }
    if (physical_key == 0x27U) return U'0';
    return std::nullopt;
}

bool contains_control(const Control::Ptr& root, const Control::Ptr& candidate) {
    if (!root || !candidate) {
        return false;
    }
    if (root == candidate) {
        return true;
    }
    for (const std::shared_ptr<gui_forms::Control>& child : (*root).children()) {
        if (contains_control(child, candidate)) {
            return true;
        }
    }
    return false;
}

bool single_allowed_effect(DragEffect effect, DragEffect allowed) noexcept {
    return effect != DragEffect::none &&
           (effect == DragEffect::copy || effect == DragEffect::move ||
            effect == DragEffect::link) &&
           has_drag_effect(allowed, effect);
}

double bounded_visual_outset(double value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0, 16384.0) : 0.0;
}

// Along one axis at `scale`: whether every device pixel that painting
// clipped to [from, to) can touch lies among the pixels an opaque fill of
// [cover_from, cover_to) paints. Painting rounds each edge to the nearest
// device pixel, halves upward (owner decision L-Q8, detail::snap_device_edge);
// an edge within a 64th of a pixel of a half is taken both ways, which
// absorbs the rounding of translated coordinates. No pixel lies beyond
// `extent`.
bool cover_spans_axis(const double cover_from, const double cover_to, const double from, const double to,
                      const double scale, const double extent) noexcept {
    constexpr double tolerance = 1.0 / 64.0;
    const double first = std::max(0.0, std::floor(from * scale + 0.5 - tolerance));
    const double end = std::min(std::ceil(extent * scale), std::floor(to * scale + 0.5 + tolerance));
    if (end <= first) return true;
    const double covered_first = std::floor(cover_from * scale + 0.5 + tolerance);
    const double covered_end = std::floor(cover_to * scale + 0.5 - tolerance);
    const bool spans = covered_first <= first && end <= covered_end;
    return spans;
}

// Whether an opaque fill of `cover` paints every device pixel that painting
// clipped to `damage` can touch, in a client area of `client` at `scale`.
bool cover_spans(const Rect cover, const Rect damage, const double scale, const Size client) noexcept {
    if (!cover.finite() || !damage.finite() || !std::isfinite(scale) || scale <= 0.0) return false;
    const bool across = cover_spans_axis(cover.left(), cover.right(), damage.left(), damage.right(), scale,
                                         client.width);
    const bool down = cover_spans_axis(cover.top(), cover.bottom(), damage.top(), damage.bottom(), scale,
                                       client.height);
    return across && down;
}

[[nodiscard]] bool current_semantic_identity(
    const Control::Ptr& control, const Control* expected_parent,
    const SurfaceRoot* owner) noexcept {
    return control && (*control).is_alive() &&
           (*control).attached_surface_root() == owner &&
           (*control).parent().get() == expected_parent;
}

// A virtual child published without a runtime ID takes the one its stable ID
// gives, so every node of a snapshot has a nonzero runtime ID; hosts keep
// zero for the window's own object.
void assign_missing_virtual_runtime_ids(std::vector<SemanticNode>& nodes) {
    for (SemanticNode& node : nodes) {
        if (node.runtime_id == 0U) {
            node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        }
        assign_missing_virtual_runtime_ids(node.children);
    }
}

// A virtual child's stable ID, and the panel it names as controlled, follow
// the StableId segment rule like a control's: hosts and theme selectors
// address them the same way. Throws std::invalid_argument naming the
// publishing control.
void require_valid_virtual_ids(const Control& owner, const std::vector<SemanticNode>& nodes) {
    for (const SemanticNode& node : nodes) {
        const bool valid = valid_stable_id(node.stable_id) &&
                           (node.controlled_panel.empty() || valid_stable_id(node.controlled_panel));
        if (!valid) {
            const std::string_view shown = valid_stable_id(node.stable_id) ? std::string_view(node.controlled_panel)
                                                                           : std::string_view(node.stable_id);
            throw std::invalid_argument("GUI.Forms control \"" + std::string(owner.stable_id().value()) +
                                        "\" published a semantic node ID \"" +
                                        std::string(shown.substr(0U, 80U)) +
                                        "\" that is not '.'-separated segments of lowercase ASCII letters and "
                                        "digits joined by single '-'");
        }
        require_valid_virtual_ids(owner, node.children);
    }
}

// Focus held by controls that the semantic tree does not publish as nodes of
// their own and that therefore belong to the node of their nearest exposed
// ancestor: a composite's inner editor, splitter or other part.
struct HiddenFocus final {
    // A hidden control that can take focus now (focusable, visible, enabled).
    bool focusable{};
    // The focused control is a hidden one.
    bool focused{};
};

// Records `control` itself as a hidden control.
void note_hidden_control(const Control::Ptr& control, const Control::Ptr& focused, HiddenFocus& hidden) {
    if ((*control).focusable() && (*control).eligible_for_input()) hidden.focusable = true;
    if (control == focused) hidden.focused = true;
}

// Records every visible control below `control`, all of which are hidden
// because `control`'s node does not include its descendants.
void note_hidden_subtree(const Control::Ptr& control, const Control::Ptr& focused, HiddenFocus& hidden) {
    std::vector<Control::Ptr> pending((*control).children().begin(), (*control).children().end());
    while (!pending.empty()) {
        const Control::Ptr current = pending.back();
        pending.pop_back();
        if (!current || !(*current).is_alive() || !(*current).effectively_visible()) continue;
        note_hidden_control(current, focused, hidden);
        const std::span<const Control::Ptr> children = (*current).children();
        for (std::size_t index = 0; index < children.size(); ++index) {
            pending.push_back(children[index]);
        }
    }
}

// Appends `control`'s semantic node, or, for a control that is not exposed,
// the nodes below it, to `destination`. `hidden` receives the focus held by
// `control` and the controls below it when they publish no node of their
// own, for the nearest exposed ancestor's node; an exposed control's node
// takes the focus of its own hidden controls and reports nothing upward.
void append_semantic_nodes(const Control::Ptr& control,
                           const Control* expected_parent,
                           const SurfaceRoot* owner,
                           const Control::Ptr& focused,
                           std::vector<SemanticNode>& destination,
                           std::size_t& count,
                           HiddenFocus& hidden) {
    if (!current_semantic_identity(control, expected_parent, owner) ||
        !(*control).effectively_visible()) return;
    SemanticDescriptor descriptor = (*control).semantic_descriptor();
    if (!current_semantic_identity(control, expected_parent, owner)) return;
    (*control).apply_provider_semantics(descriptor);
    std::vector<SemanticNode> descendants;
    HiddenFocus inner;
    if (!descriptor.exposed || descriptor.include_descendants) {
        const std::vector<Control::Ptr> children((*control).children().begin(),
                                                 (*control).children().end());
        for (const Control::Ptr& child : children) {
            if (!current_semantic_identity(child, control.get(), owner)) continue;
            append_semantic_nodes(child, control.get(), owner, focused,
                                  descendants, count, inner);
        }
        if (!current_semantic_identity(control, expected_parent, owner)) return;
        std::vector<SemanticNode> virtual_children =
            (*control).semantic_virtual_children();
        if (!current_semantic_identity(control, expected_parent, owner)) return;
        require_valid_virtual_ids(*control, virtual_children);
        assign_missing_virtual_runtime_ids(virtual_children);
        count += virtual_children.size();
        descendants.insert(descendants.end(),
                            std::make_move_iterator(virtual_children.begin()),
                            std::make_move_iterator(virtual_children.end()));
    }
    if (!descriptor.exposed) {
        destination.insert(destination.end(),
                           std::make_move_iterator(descendants.begin()),
                           std::make_move_iterator(descendants.end()));
        note_hidden_control(control, focused, hidden);
        if (inner.focusable) hidden.focusable = true;
        if (inner.focused) hidden.focused = true;
        return;
    }
    if (!descriptor.include_descendants) note_hidden_subtree(control, focused, inner);
    SemanticNode node;
    node.runtime_id = (*control).runtime_id().value;
    node.stable_id = std::string((*control).stable_id().value());
    node.role = descriptor.role;
    node.name = descriptor.name;
    node.value = descriptor.value;
    node.description = descriptor.description;
    node.numeric_value = descriptor.numeric_value;
    node.minimum_value = descriptor.minimum_value;
    node.maximum_value = descriptor.maximum_value;
    node.position_in_set = descriptor.position_in_set;
    node.set_size = descriptor.set_size;
    node.has_popup = descriptor.has_popup;
    node.controlled_panel = descriptor.controlled_panel;
    node.orientation = descriptor.orientation;
    node.level = descriptor.level;
    node.modal = descriptor.modal;
    node.row_index = descriptor.row_index;
    node.column_index = descriptor.column_index;
    node.row_count = descriptor.row_count;
    node.column_count = descriptor.column_count;
    node.access_key = descriptor.access_key;
    node.small_step = descriptor.small_step;
    node.large_step = descriptor.large_step;
    node.text_selection = descriptor.text_selection;
    node.multiline = descriptor.multiline;
    node.tool_tip = (*control).tool_tip_text();
    node.bounds = (*control).absolute_bounds();
    if (!current_semantic_identity(control, expected_parent, owner)) return;
    node.states |= descriptor.states;
    if ((*control).effectively_enabled()) node.states |= SemanticState::enabled;
    node.states |= SemanticState::visible;
    // A node is focusable when its control is, or when a control it hides
    // (one that publishes no node of its own) can take focus; it is focused
    // while focus is on its control or on such a hidden control.
    if ((*control).focusable() || inner.focusable) node.states |= SemanticState::focusable;
    if (control == focused || inner.focused) node.states |= SemanticState::focused;
    node.actions = descriptor.actions;
    node.children = std::move(descendants);
    destination.push_back(std::move(node));
    ++count;
}

bool dispatch_semantic_child_action(Control::Ptr control,
                                    std::string_view stable_id,
                                    SemanticAction action,
                                    std::string_view value) {
    if (!control || !(*control).effectively_visible() ||
        !(*control).effectively_enabled()) return false;
    SurfaceRoot* const owner = (*control).attached_surface_root();
    if ((*control).on_semantic_child_action(stable_id, action, value)) return true;
    if (!(*control).is_alive() || (*control).attached_surface_root() != owner) return false;
    const std::vector<Control::Ptr> children((*control).children().begin(),
                                             (*control).children().end());
    for (const Control::Ptr& child : children) {
        if (!child || !(*child).is_alive() || (*child).parent() != control ||
            (*child).attached_surface_root() != owner) {
            continue;
        }
        if (dispatch_semantic_child_action(child, stable_id, action, value)) return true;
    }
    return false;
}

// Whether `control` is the F6 region `region` or lies inside it.
bool region_holds(const Control& region, const Control& control) noexcept {
    const bool held = &region == &control || region.contains(control);
    return held;
}

// Whether two F6 regions are the same control or one is inside the other,
// which the region list refuses.
bool regions_nest(const Control& first, const Control& second) noexcept {
    const bool nest = region_holds(first, second) || region_holds(second, first);
    return nest;
}

// Marks the control whose display chunk is being recorded, for the theme
// engine (T2 E6), for the life of one recording: it restores the marks it
// found on every exit. It borrows the window's two mark fields.
struct RecordingMark final {
    const Control*& control;
    detail::RecordingPainter*& painter;
    const Control* previous_control{};
    detail::RecordingPainter* previous_painter{};

    ~RecordingMark() {
        control = previous_control;
        painter = previous_painter;
    }
};

// The events a window raises through the C ABI (SurfaceRoot::append_interactions),
// in declaration order. The rest of Window's events stay C++-only until a
// consumer needs them; declaring one later is a minor ABI addition.
constexpr std::array<InteractionDeclaration, 7> window_interactions{{
    {.name = "active_changed", .payload = "bool", .mutable_payload = false},
    {.name = "backdrop_changed", .payload = "WindowBackdropGrant", .mutable_payload = false},
    {.name = "dialog_result_changed", .payload = "DialogResult", .mutable_payload = false},
    {.name = "focused_control_changed", .payload = "FocusedControlChange", .mutable_payload = false},
    {.name = "help_requested", .payload = "HelpRequestEvent", .mutable_payload = true},
    {.name = "presentation_changed", .payload = "PresentationSettings", .mutable_payload = false},
    {.name = "theme_changed", .payload = "Theme", .mutable_payload = false},
}};

} // namespace

void SurfaceRoot::FocusChangePublication::operator()() const {
    if (!control ||
        (focused ? !(*root).eligible(control)
                 : (!(*control).is_alive() || (*control).surface_root_ != root))) {
        return;
    }
    (*root).metrics_.record_callback_emitted();
    (*control).on_focus_changed(focused);
    if ((*control).is_alive()) {
        (*control).focus_observed_.emit(focused);
    }
    if (!(*control).is_alive()) return;
    if (!focused || (*root).eligible(control)) {
        (*control).invalidate(invalidation::focus);
    } else {
        (*root).focused_.reset();
    }
}

void SurfaceRoot::FocusScopePublication::operator()() const {
    if (!owner || !(*owner).is_alive() || (*owner).surface_root_ != root) return;
    (*root).focus_scope_changed_.emit(change);
}

void SurfaceRoot::PointerCapturePublication::operator()() const {
    if (!owner || !(*owner).is_alive() || (*owner).surface_root_ != root) return;
    (*root).pointer_capture_changed_.emit(change);
}

void SurfaceRoot::ControlAvailabilityPublication::operator()() const {
    if (!control || !(*control).is_alive() || (*control).surface_root_ != root) {
        return;
    }
    (*root).control_availability_changed_.emit(change);
}

template <typename InputEvent>
void SurfaceRoot::DeferredInputVisitor::operator()(InputEvent&& event) const {
    using EventType =
        std::remove_cv_t<std::remove_reference_t<InputEvent>>;
    if constexpr (std::is_same_v<EventType, PointerEvent>) {
        static_cast<void>((*root).dispatch_pointer(std::move(event)));
    } else if constexpr (std::is_same_v<EventType, KeyEvent>) {
        static_cast<void>((*root).dispatch_key(std::move(event)));
    } else if constexpr (std::is_same_v<EventType, TextInputEvent>) {
        static_cast<void>((*root).dispatch_text(std::move(event)));
    } else if constexpr (std::is_same_v<EventType, DragEvent>) {
        static_cast<void>((*root).dispatch_drag(std::move(event)));
    } else if (event.owner.value != 0U) {
        static_cast<void>((*root).perform_semantic_action(
            event.owner, event.stable_id, event.action, event.value));
    } else {
        static_cast<void>((*root).perform_semantic_action(
            event.stable_id, event.action, event.value));
    }
}


ImageLoadResult SurfaceRoot::load_png(std::span<const std::byte> encoded) {
    require_root_thread("PNG resource load");
    return (*resources_).load_png(encoded);
}

ImageLoadResult SurfaceRoot::load_bgra32_premultiplied(
    std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
    std::span<const std::byte> pixels) {
    require_root_thread("BGRA resource load");
    return (*resources_).load_bgra32_premultiplied(
        width, height, row_bytes, pixels);
}

ImageLoadResult SurfaceRoot::replace_png(ImageId image,
                                    std::span<const std::byte> encoded) {
    require_root_thread("PNG resource replacement");
    // Repaints every window sharing the registry, this one included.
    ImageLoadResult result = (*resources_).replace_png(image, encoded);
    return result;
}

void SurfaceRoot::repaint_for_image_replacement() {
    add_damage_all_planes({0.0, 0.0, client_size_.width, client_size_.height});
    touch_paint();
}

void SurfaceRoot::repaint_for_image_removal() {
    // The unscoped removal API cannot know which retained display chunks
    // reference this generational ID. Conservatively retire all cached paint
    // chunks so a dead ImageId is never replayed into any renderer. Scoped
    // replacement APIs remain available when the caller can name a consumer.
    mark_subtree_dirty(*root_, Dirty::paint | Dirty::semantics);
}

ImageLoadResult SurfaceRoot::replace_bgra32_premultiplied(
    ImageId image, std::uint32_t width, std::uint32_t height,
    std::uint64_t row_bytes, std::span<const std::byte> pixels,
    Control& consumer) {
    require_root_thread("scoped BGRA resource replacement");
    if (consumer.surface_root_ != this) {
        throw std::invalid_argument(
            "scoped BGRA replacement requires an attached consumer");
    }
    ImageLoadResult result = (*resources_).registry().replace_bgra32_premultiplied(
        image, width, height, row_bytes, pixels);
    if (result) {
        mark_dirty(consumer, Dirty::paint | Dirty::semantics);
    }
    return result;
}

ImageLoadResult SurfaceRoot::update_bgra32_premultiplied(
    ImageId image, std::uint32_t width, std::uint32_t height,
    std::uint64_t row_bytes, std::span<const std::byte> pixels,
    Control& consumer) {
    require_root_thread("scoped live BGRA resource update");
    if (consumer.surface_root_ != this) {
        throw std::invalid_argument(
            "scoped live BGRA update requires an attached consumer");
    }
    ImageLoadResult result = (*resources_).registry().update_bgra32_premultiplied(
        image, width, height, row_bytes, pixels);
    if (result) {
        mark_dirty(consumer, Dirty::paint | Dirty::semantics);
    }
    return result;
}

ImageLoadResult SurfaceRoot::patch_bgra32_premultiplied(
    ImageId image, std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height,
    std::uint64_t source_row_bytes, std::span<const std::byte> pixels,
    Control& consumer, Rect local_damage) {
    require_root_thread("scoped BGRA resource patch");
    if (consumer.surface_root_ != this) {
        throw std::invalid_argument(
            "scoped BGRA patch requires an attached consumer");
    }
    ImageLoadResult result = (*resources_).registry().patch_bgra32_premultiplied(
        image, x, y, width, height, source_row_bytes, pixels);
    if (result) {
        mark_paint_dirty(consumer, local_damage);
    }
    return result;
}

ImageLoadResult SurfaceRoot::replace_png(ImageId image,
                                    std::span<const std::byte> encoded,
                                    Control& consumer) {
    require_root_thread("scoped PNG resource replacement");
    if (consumer.surface_root_ != this) {
        throw std::invalid_argument(
            "scoped PNG replacement requires an attached consumer");
    }
    ImageLoadResult result = (*resources_).registry().replace_png(image, encoded);
    if (result) {
        mark_dirty(consumer, Dirty::paint | Dirty::semantics);
    }
    return result;
}

bool SurfaceRoot::remove_image(ImageId image) {
    require_root_thread("PNG resource removal");
    // Retires the paint of every window sharing the registry.
    const bool removed = (*resources_).remove_image(image);
    return removed;
}


AcceleratorToken SurfaceRoot::register_accelerator(
    Component& owner, KeyGesture gesture, std::function<bool()> callback,
    AcceleratorOptions options) {
    require_root_thread("accelerator registration");
    if (!owner.is_alive() || gesture.physical_key == 0U || !callback) {
        throw std::invalid_argument(
            "accelerator requires a live owner, physical key, and callback");
    }
    std::shared_ptr<gui_forms::detail::AcceleratorAttachment> attachment = std::make_shared<detail::AcceleratorAttachment>(
        *this, owner, gesture, std::move(callback), options);
    accelerators_.push_back(attachment);
    owner.own_revocable(attachment);
    return AcceleratorToken(std::move(attachment));
}

void SurfaceRoot::close_accelerator(
    detail::AcceleratorAttachment& accelerator) noexcept {
    AcceleratorList::iterator found = accelerators_.begin();
    while (found != accelerators_.end() &&
           (*found).get() != &accelerator) {
        ++found;
    }
    if (found == accelerators_.end()) return;
    (*(*found)).revoke();
    accelerators_.erase(found);
}

bool SurfaceRoot::offer_accelerators(const KeyEvent& event) {
    require_root_thread("accelerator offer");
    const bool taken = dispatch_accelerator(event, false);
    return taken;
}

bool SurfaceRoot::dispatch_accelerator(const KeyEvent& event, bool preemptive) {
    if (event.action != KeyAction::down) return false;
    const AcceleratorList snapshot = accelerators_;
    for (AcceleratorList::const_reverse_iterator iterator = snapshot.rbegin();
         iterator != snapshot.rend(); ++iterator) {
        const std::shared_ptr<gui_forms::detail::AcceleratorAttachment>& accelerator = *iterator;
        if (!accelerator || !(*accelerator).connected() ||
            (*accelerator).options().before_focused_route != preemptive ||
            (*accelerator).gesture().physical_key != event.physical_key ||
            (*accelerator).gesture().modifiers != event.modifiers) continue;
        Component* owner = (*accelerator).owner();
        if (!owner || !(*owner).is_alive()) continue;
        if ((*accelerator).invoke()) return true;
    }
    return false;
}

PopupToken SurfaceRoot::open_popup(const Control::Ptr& owner,
                              const Control::Ptr& popup,
                              PopupOptions options) {
    require_root_thread("popup attachment");
    const bool owner_available = owner && (*owner).surface_root_ == this &&
        (*owner).is_alive() && (*owner).effectively_visible() &&
        (!options.require_enabled_owner || (*owner).effectively_enabled());
    if (!owner_available) {
        throw std::logic_error(
            options.require_enabled_owner
                ? "GUI.Forms popup requires an enabled, visible attached owner"
                : "GUI.Forms passive popup requires a visible attached owner");
    }
    if (!popup || (*popup).surface_root_ != nullptr || (*popup).parent() || !(*popup).is_alive()) {
        throw std::logic_error("GUI.Forms popup must be a live detached root");
    }
    // Popups are window overlays, not children in the consumer's layout
    // vocabulary. Attaching one to root_ lets a TableLayoutPanelCompat, Flow panel,
    // or docking root assign it an application cell and silently collapse the
    // overlay. Keep it as a separate retained root while still registering the
    // subtree with this Window for lifetime, focus, semantics, and dispatch.
    attach_subtree(popup, {});
    std::shared_ptr<gui_forms::detail::PopupAttachment> attachment = std::make_shared<detail::PopupAttachment>(*this, owner, popup);
    popups_.push_back(attachment);
    (*owner).own_revocable(attachment);
    return PopupToken(std::move(attachment));
}

void SurfaceRoot::close_popup(detail::PopupAttachment& popup) noexcept {
    PopupList::iterator found = popups_.begin();
    while (found != popups_.end() && (*found).get() != &popup) {
        ++found;
    }
    if (found == popups_.end()) {
        popup.revoke();
        return;
    }
    const std::shared_ptr<gui_forms::detail::PopupAttachment> attachment = *found;
    const Control::Ptr owner = (*attachment).owner();
    const Control::Ptr overlay = (*attachment).popup();
    (*attachment).revoke(false);
    popups_.erase(found);
    if (overlay && !(*overlay).parent() && (*overlay).surface_root_ == this &&
        overlay != root_ && !in_lifecycle_notification_) {
        try {
            detach_subtree(overlay);
        } catch (...) {
        }
    }
    const detail::BoundMemberFunction<
        void (detail::PopupAttachment::*)()> publish_closed(
            *attachment, &detail::PopupAttachment::publish_closed);
    if (owner && (*owner).is_alive() && (*owner).surface_root_ == this) {
        (*owner).publish_change(
            static_cast<const void*>(&(*attachment).closed()), publish_closed);
    } else {
        publish_closed();
    }
}

void SurfaceRoot::close_popups_for_subtree(const Control::Ptr& control) noexcept {
    std::vector<std::shared_ptr<detail::PopupAttachment>> closing;
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (contains_control(control, (*popup).owner())) {
            closing.push_back(popup);
        }
    }
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : closing) {
        (*popup).disconnect();
    }
}



Control::Ptr SurfaceRoot::find(std::string_view stable_id) const {
    const StableIdMap::const_iterator found =
        stable_ids_.find(std::string(stable_id));
    return found == stable_ids_.end() ? Control::Ptr{} : (*found).second.lock();
}

Control::Ptr SurfaceRoot::hit_test(Point position) {
    require_root_thread("hit test");
    for (std::uint32_t pass = 0U;
         pass < maximum_semantic_snapshot_passes; ++pass) {
        ensure_layout(true);
        const std::uint64_t generation = semantic_generation_;
        Control::Ptr target;
        ControlList popup_roots;
        popup_roots.reserve(popups_.size());
        for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) popup_roots.push_back((*popup).popup());
        for (ControlList::reverse_iterator popup = popup_roots.rbegin();
             popup != popup_roots.rend();
             ++popup) {
            if (const Control::Ptr& overlay = *popup;
                overlay && (*overlay).is_alive() && (*overlay).surface_root_ == this) {
                target = hit_test_recursive(overlay, position);
                if (target) break;
            }
        }
        if (!target) target = hit_test_recursive(root_, position);
        if (generation == semantic_generation_) return target;
        metrics_.record_callback_arbitration_retry(
            pass + 1U == maximum_semantic_snapshot_passes);
    }
    return {};
}

// --- Help asked of the window ----------------------------------------------

void SurfaceRoot::append_interactions(std::vector<InteractionDeclaration>& interactions) {
    interactions.insert(interactions.end(), window_interactions.begin(), window_interactions.end());
}

bool SurfaceRoot::request_help(bool keyboard_initiated) {
    verify_access("SurfaceRoot::request_help");
    HelpRequestEvent request{};
    const Control::Ptr root_control = root();
    if (root_control) {
        request.target_stable_id = std::string((*root_control).stable_id().value());
        const Rect bounds = (*root_control).absolute_bounds();
        request.position = {bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    }
    request.keyboard_initiated = keyboard_initiated;
    // A handler may destroy the window: nothing of it is read once its
    // lifetime says it is gone.
    const std::weak_ptr<detail::WindowLifetime> watched = lifetime_;
    help_requested_.emit(request);
    if (request.handled) return true;
    // Offered to a copy, skipping a provider a handler disposed meanwhile.
    const std::vector<detail::WindowHelpFallback*> offered = help_fallbacks_;
    for (detail::WindowHelpFallback* const fallback : offered) {
        const std::shared_ptr<detail::WindowLifetime> alive = watched.lock();
        if (!alive || (*alive).window == nullptr) break;
        const bool registered = std::find(help_fallbacks_.begin(), help_fallbacks_.end(), fallback) !=
                                help_fallbacks_.end();
        if (!registered) continue;
        if ((*fallback).offer_window_help(request)) return true;
    }
    return request.handled;
}

void SurfaceRoot::add_help_fallback(detail::WindowHelpFallback& fallback) {
    help_fallbacks_.push_back(&fallback);
}

void SurfaceRoot::remove_help_fallback(const detail::WindowHelpFallback& fallback) noexcept {
    const std::vector<detail::WindowHelpFallback*>::iterator found =
        std::find(help_fallbacks_.begin(), help_fallbacks_.end(), &fallback);
    if (found != help_fallbacks_.end()) help_fallbacks_.erase(found);
}

bool SurfaceRoot::request_focus(const Control::Ptr& control) {
    require_root_thread("focus mutation");
    if (control && (!eligible(control) || !(*control).focusable_)) {
        return false;
    }
    if (control && !focus_allowed_by_active_scope(control)) {
        metrics_.record_focus_scope_rejection();
        return false;
    }
    Control::Ptr previous = focused_.lock();
    if (previous == control) {
        return true;
    }
    if (validating_) {
        ++validation_reentrant_requests_rejected_;
        return false;
    }
    if (previous && (!control || (*control).causes_validation())) {
        AutoValidate mode = AutoValidate::enable_prevent_focus_change;
        for (Control::Ptr current = previous; current; current = (*current).parent()) {
            const AutoValidate authored = (*current).authored_auto_validate();
            if (authored != AutoValidate::inherit) {
                mode = authored;
                break;
            }
        }
        if (mode != AutoValidate::disable) {
            const bool valid = validate_focus_transition(previous, control, mode);
            if (!valid && mode == AutoValidate::enable_prevent_focus_change &&
                focused_.lock() == previous && eligible(previous)) {
                ++validation_focus_moves_blocked_;
                return false;
            }
        }
    }
    // Validation handlers may dispose, detach, hide, or disable either end.
    // Re-evaluate both endpoints before publishing any focus notification.
    if (previous && (!(*previous).is_alive() || (*previous).surface_root() != this)) {
        previous.reset();
        focused_.reset();
    }
    if (control && (!eligible(control) || !(*control).focusable_ ||
                    !focus_allowed_by_active_scope(control))) {
        return false;
    }
    if (previous) {
        focused_.reset();
        (*previous).publish_change(
            static_cast<const void*>(&(*previous).focus_observed_),
            FocusChangePublication{this, previous, false});
    }
    if (control && eligible(control) && (*control).focusable_) {
        focused_ = control;
        remember_region_focus(control);
        (*control).publish_change(
            static_cast<const void*>(&(*control).focus_observed_),
            FocusChangePublication{this, control, true});
    }
    metrics_.record_focus_transition();
    FocusedControlChange moved{};
    if (previous) moved.previous_id = (*previous).runtime_id();
    const Control::Ptr current = focused_.lock();
    if (current) {
        moved.current_id = (*current).runtime_id();
        moved.stable_id = std::string((*current).stable_id().value());
    }
    if (tool_tip_presenter_) (*tool_tip_presenter_).focus_changed(current);
    focused_control_changed_.emit(moved);
    return true;
}

void SurfaceRoot::set_focus_cue_visible(bool visible) {
    if (focus_cue_visible_ == visible) return;
    const Control::Ptr focused = focused_.lock();
    if (focused && (*focused).is_alive() &&
        (*focused).surface_root() == this) {
        add_damage_all_planes(paint_damage_bounds_of(*focused));
    }
    focus_cue_visible_ = visible;
    if (focused && (*focused).is_alive() &&
        (*focused).surface_root() == this) {
        (*focused).on_focus_cue_changed(visible);
    }
    if (focused && (*focused).is_alive() &&
        (*focused).surface_root() == this) {
        (*focused).invalidate(Dirty::paint);
    }
}

void SurfaceRoot::set_accept_button(const Control::Ptr& control) {
    require_root_thread("accept button mutation");
    if (control && ((*control).attached_surface_root() != this || !(*control).is_alive() ||
                    !(*control).supports_dialog_command())) {
        throw std::invalid_argument(
            "GUI.Forms accept button must be a live dialog command in its Window");
    }
    Control::Ptr existing = accept_button_.lock();
    if (existing == control) return;
    if (existing && (*existing).is_alive()) (*existing).notify_default(false);
    try {
        if (control) (*control).notify_default(true);
    } catch (...) {
        if (existing && (*existing).is_alive()) (*existing).notify_default(true);
        throw;
    }
    accept_button_ = control;
}

void SurfaceRoot::set_cancel_button(const Control::Ptr& control) {
    require_root_thread("cancel button mutation");
    if (control && ((*control).attached_surface_root() != this || !(*control).is_alive() ||
                    !(*control).supports_dialog_command())) {
        throw std::invalid_argument(
            "GUI.Forms cancel button must be a live dialog command in its Window");
    }
    if (control) {
        (*control).assign_cancel_dialog_result();
        if ((*control).attached_surface_root() != this || !(*control).is_alive() ||
            !(*control).supports_dialog_command()) {
            throw std::logic_error(
                "GUI.Forms cancel button became unavailable during assignment");
        }
    }
    cancel_button_ = control;
}

DialogKeySnapshot SurfaceRoot::dialog_key_snapshot() const noexcept {
    return {mnemonic_attempts_, mnemonics_handled_, accept_attempts_,
            accept_handled_, cancel_attempts_, cancel_handled_,
            dialog_command_rejections_, mnemonic_candidates_,
            mnemonic_collisions_, mnemonic_cycles_};
}

void SurfaceRoot::set_dialog_result(DialogResult result) {
    require_root_thread("dialog result mutation");
    switch (result) {
    case DialogResult::none:
    case DialogResult::ok:
    case DialogResult::cancel:
    case DialogResult::abort:
    case DialogResult::retry:
    case DialogResult::ignore:
    case DialogResult::yes:
    case DialogResult::no:
    case DialogResult::try_again:
    case DialogResult::continue_: break;
    default:
        throw std::invalid_argument("Window DialogResult is not a defined value");
    }
    if (dialog_result_ == result) return;
    dialog_result_ = result;
    dialog_result_changed_.emit(dialog_result_);
}

bool SurfaceRoot::tab_order_less(const Control::Ptr& left,
                            const Control::Ptr& right) noexcept {
    if (!left) return false;
    if (!right) return true;
    return (*left).tab_index() < (*right).tab_index();
}

void SurfaceRoot::collect_focus_candidates(const Control::Ptr& control,
                                      ControlList& result,
                                      bool focusable_only) const {
    if (!control || !eligible(control)) return;
    if (!focusable_only || ((*control).focusable_ && (*control).tab_stop_)) {
        result.push_back(control);
    }
    ControlList children((*control).children_.begin(),
                         (*control).children_.end());
    std::stable_sort(children.begin(), children.end(), tab_order_less);
    for (const Control::Ptr& child : children) {
        collect_focus_candidates(child, result, focusable_only);
    }
}

void SurfaceRoot::collect_mnemonic_candidates(const Control::Ptr& control,
                                         const Control::Ptr& root,
                                         char32_t character,
                                         ControlList& result) const {
    if (!control || !eligible(control) || !contains_control(root, control)) {
        return;
    }
    if ((*control).mnemonic_matches(character)) {
        result.push_back(control);
    }
    ControlList children((*control).children_.begin(),
                         (*control).children_.end());
    std::stable_sort(children.begin(), children.end(), tab_order_less);
    for (const Control::Ptr& child : children) {
        collect_mnemonic_candidates(child, root, character, result);
    }
}

bool SurfaceRoot::move_focus_after(const Control::Ptr& origin) {
    require_root_thread("mnemonic focus traversal");
    if (!origin || (*origin).surface_root() != this) return false;
    const Control::Ptr scope = active_focus_scope_root();
    const Control::Ptr traversal_root = scope ? scope : root_;
    if (!contains_control(traversal_root, origin)) return false;

    ControlList ordered;
    collect_focus_candidates(traversal_root, ordered, false);
    const ControlList::iterator found =
        std::find(ordered.begin(), ordered.end(), origin);
    if (found == ordered.end()) return false;
    for (ControlList::iterator current = std::next(found);
         current != ordered.end(); ++current) {
        if ((*(*current)).focusable() && (*(*current)).tab_stop() &&
            request_focus(*current)) return true;
    }
    return false;
}

bool SurfaceRoot::validate_command_activation(const Control::Ptr& destination) {
    require_root_thread("dialog command activation");
    if (!destination || !eligible(destination) ||
        !focus_allowed_by_active_scope(destination)) return false;
    const Control::Ptr previous = focused_.lock();
    if (!previous || previous == destination ||
        !(*destination).causes_validation()) return true;
    if (validating_) {
        ++validation_reentrant_requests_rejected_;
        return false;
    }
    AutoValidate mode = AutoValidate::enable_prevent_focus_change;
    for (Control::Ptr current = previous; current; current = (*current).parent()) {
        const AutoValidate authored = (*current).authored_auto_validate();
        if (authored != AutoValidate::inherit) {
            mode = authored;
            break;
        }
    }
    if (mode == AutoValidate::disable) return true;
    const bool accepted = validate_focus_transition(previous, destination, mode);
    return accepted || mode == AutoValidate::enable_allow_focus_change;
}

bool SurfaceRoot::dispatch_mnemonic(char32_t character) {
    ++mnemonic_attempts_;
    const Control::Ptr scope = active_focus_scope_root();
    const Control::Ptr start = scope ? scope : root_;
    if (!start) return false;

    ControlList candidates;
    collect_mnemonic_candidates(start, start, character, candidates);
    mnemonic_candidates_ += candidates.size();
    if (candidates.size() > 1U) ++mnemonic_collisions_;
    if (candidates.empty()) {
        mnemonic_cursor_.reset();
        mnemonic_cursor_character_ = 0;
        return false;
    }

    const char32_t folded = character >= U'A' && character <= U'Z'
        ? character + (U'a' - U'A') : character;
    std::size_t first{};
    if (candidates.size() > 1U && mnemonic_cursor_character_ == folded) {
        if (const Control::Ptr prior = mnemonic_cursor_.lock()) {
            const ControlList::iterator found =
                std::find(candidates.begin(), candidates.end(), prior);
            if (found != candidates.end()) {
                first = (static_cast<std::size_t>(
                    std::distance(candidates.begin(), found)) + 1U) %
                    candidates.size();
                ++mnemonic_cycles_;
            }
        }
    }

    for (std::size_t offset = 0U; offset < candidates.size(); ++offset) {
        const std::size_t index = (first + offset) % candidates.size();
        const Control::Ptr& candidate = candidates[index];
        if (!candidate || !eligible(candidate) ||
            !contains_control(start, candidate)) {
            continue;
        }
        if ((*candidate).process_mnemonic_self(character)) {
            mnemonic_cursor_ = candidate;
            mnemonic_cursor_character_ = folded;
            ++mnemonics_handled_;
            metrics_.record_activation();
            return true;
        }
    }
    return false;
}

bool SurfaceRoot::dispatch_dialog_button(bool accept) {
    std::uint64_t& attempts = accept ? accept_attempts_ : cancel_attempts_;
    std::uint64_t& handled = accept ? accept_handled_ : cancel_handled_;
    ++attempts;
    Control::Ptr target = accept ? accept_button_.lock() : cancel_button_.lock();
    if (!target || !eligible(target) || !focus_allowed_by_active_scope(target)) {
        return false;
    }
    if (!(*target).perform_dialog_command()) {
        ++dialog_command_rejections_;
        // A live target owned this dialog key even when its validation gate
        // rejected activation. Do not leak it to another mnemonic or host.
        return true;
    }
    ++handled;
    metrics_.record_activation();
    return true;
}

bool SurfaceRoot::dispatch_dialog_key(const KeyEvent& event) {
    if (event.action != KeyAction::down || event.repeat) return false;
    const bool alt = has_modifier(event.modifiers, Modifier::alt);
    const bool control = has_modifier(event.modifiers, Modifier::control);
    const bool meta = has_modifier(event.modifiers, Modifier::meta);
    if (alt && !control && !meta) {
        const std::optional<char32_t> character =
            physical_mnemonic(event.physical_key);
        if (character) return dispatch_mnemonic(*character);
    }
    if (!alt && !control && !meta) {
        if (event.physical_key == PhysicalKey::enter) {
            return dispatch_dialog_button(true);
        }
        if (event.physical_key == PhysicalKey::escape) {
            return dispatch_dialog_button(false);
        }
    }
    return false;
}

bool SurfaceRoot::validate_control(const Control::Ptr& control,
                              Control* destination, bool bulk) {
    require_root_thread("control validation");
    if (!control || !eligible(control)) return true;
    if (validating_) {
        ++validation_reentrant_requests_rejected_;
        return false;
    }
    validating_ = true;
    struct Reset final {
        bool& value;
        ~Reset() { value = false; }
    } reset{validating_};
    ++validation_attempts_;
    if (bulk) ++validation_bulk_controls_visited_;
    const bool accepted = (*control).perform_validation(destination, bulk);
    if (accepted) ++validation_succeeded_;
    else ++validation_cancelled_;
    return accepted;
}

bool SurfaceRoot::validate_children(const Control::Ptr& container,
                               ValidationConstraints constraints) {
    require_root_thread("child validation");
    if (!container || !eligible(container)) return true;
    if (validating_) {
        ++validation_reentrant_requests_rejected_;
        return false;
    }
    constexpr std::uint8_t known = static_cast<std::uint8_t>(
        ValidationConstraints::immediate_children |
        ValidationConstraints::selectable |
        ValidationConstraints::enabled |
        ValidationConstraints::visible |
        ValidationConstraints::tab_stop);
    if ((static_cast<std::uint8_t>(constraints) & ~known) != 0U) {
        throw std::invalid_argument(
            "GUI.Forms ValidationConstraints contains unknown flags");
    }
    validating_ = true;
    struct Reset final {
        bool& value;
        ~Reset() { value = false; }
    } reset{validating_};

    bool accepted = true;
    validate_children_recursive(
        container, container, constraints, accepted);
    return accepted;
}

void SurfaceRoot::validate_children_recursive(
    const Control::Ptr& parent, const Control::Ptr& container,
    ValidationConstraints constraints, bool& accepted) {
    const std::vector<Control::Ptr> children(
        (*parent).children().begin(), (*parent).children().end());
    for (const Control::Ptr& child : children) {
        if (!child || !(*child).is_alive() ||
            (*child).attached_surface_root() != this ||
            (*child).parent() != parent) continue;
        bool selected = true;
        if (has_validation_constraint(
                constraints, ValidationConstraints::selectable)) {
            selected = (*child).focusable() &&
                       (*child).has_style(ControlStyles::selectable);
        }
        if (has_validation_constraint(
                constraints, ValidationConstraints::enabled)) {
            selected = selected && (*child).effectively_enabled();
        }
        if (has_validation_constraint(
                constraints, ValidationConstraints::visible)) {
            selected = selected && (*child).effectively_visible();
        }
        if (has_validation_constraint(
                constraints, ValidationConstraints::tab_stop)) {
            selected = selected && (*child).tab_stop();
        }
        if (selected) {
            ++validation_attempts_;
            ++validation_bulk_controls_visited_;
            const bool valid =
                (*child).perform_validation(container.get(), true);
            if (valid) ++validation_succeeded_;
            else ++validation_cancelled_;
            accepted = valid && accepted;
        }
        if (!(*child).is_alive() || (*child).attached_surface_root() != this ||
            (*child).parent() != parent) {
            continue;
        }
        if (!has_validation_constraint(
                constraints, ValidationConstraints::immediate_children)) {
            validate_children_recursive(
                child, container, constraints, accepted);
        }
    }
}

ValidationSnapshot SurfaceRoot::validation_snapshot() const noexcept {
    return {validation_attempts_, validation_succeeded_, validation_cancelled_,
            validation_focus_moves_blocked_,
            validation_reentrant_requests_rejected_,
            validation_bulk_controls_visited_, validating_};
}

bool SurfaceRoot::validate_focus_transition(const Control::Ptr& previous,
                                       const Control::Ptr& destination,
                                       AutoValidate mode) {
    std::unordered_set<const Control*> destination_ancestors;
    for (Control::Ptr current = destination; current; current = (*current).parent()) {
        destination_ancestors.insert(current.get());
    }
    validating_ = true;
    struct Reset final {
        bool& value;
        ~Reset() { value = false; }
    } reset{validating_};
    bool accepted = true;
    for (Control::Ptr current = previous;
         current && !destination_ancestors.contains(current.get());
         current = (*current).parent()) {
        if (!(*current).is_alive() || (*current).surface_root() != this) break;
        ++validation_attempts_;
        const bool valid = (*current).perform_validation(destination.get(), false);
        if (valid) ++validation_succeeded_;
        else ++validation_cancelled_;
        accepted = valid && accepted;
        if (!valid && mode == AutoValidate::enable_prevent_focus_change) break;
    }
    return accepted;
}

FocusScopeId SurfaceRoot::begin_focus_scope(const Control::Ptr& root,
                                       const Control::Ptr& preferred_focus,
                                       FocusScopeOptions options) {
    require_root_thread("focus-scope entry");
    if (!root || !eligible(root)) {
        throw std::logic_error(
            "GUI.Forms focus scope requires an eligible attached root");
    }
    for (FocusScopeList::reverse_iterator current = focus_scopes_.rbegin();
         current != focus_scopes_.rend();
         ++current) {
        if (!(*current).active) {
            continue;
        }
        if ((*current).options.contain_focus) {
            const Control::Ptr containing_root = (*current).root.lock();
            bool contained = contains_control(containing_root, root);
            // Window-owned popup roots are outside the layout tree. They belong
            // to a containing scope only through their registered live owner.
            // This permits a ComboBox inside a dialog without allowing unrelated
            // controls or arbitrary overlays to escape modal focus containment.
            for (const std::shared_ptr<detail::PopupAttachment>& popup : popups_) {
                if (contains_control((*popup).popup(), root) &&
                    contains_control(containing_root, (*popup).owner())) {
                    contained = true;
                    break;
                }
            }
            if (!contained) {
                throw std::logic_error(
                    "GUI.Forms nested focus scope must remain inside its containing scope");
            }
        }
        break;
    }
    if (focus_scope_depth() >= maximum_focus_scope_depth) {
        throw std::length_error("GUI.Forms focus-scope nesting limit reached");
    }
    if (preferred_focus &&
        (!contains_control(root, preferred_focus) || !eligible(preferred_focus) ||
         !(*preferred_focus).focusable_)) {
        throw std::invalid_argument(
            "GUI.Forms preferred focus must be an eligible focusable scope descendant");
    }

    FocusScopeState state;
    state.id = FocusScopeId{next_focus_scope_id_++};
    if (!state.id) {
        state.id = FocusScopeId{next_focus_scope_id_++};
    }
    state.root = root;
    state.previous_focus = focused_;
    state.stable_id = std::string((*root).stable_id().value());
    state.root_id = (*root).runtime_id();
    state.options = options;
    focus_scopes_.push_back(state);
    ++semantic_generation_;

    const std::size_t depth = focus_scope_depth();
    metrics_.record_focus_scope_opened(depth);
    FocusScopeChange change;
    change.scope = state.id;
    change.root_id = state.root_id;
    change.stable_id = state.stable_id;
    change.depth = depth;
    change.opened = true;
    focus_scope_changed_.emit(change);

    bool remains_active = false;
    for (const FocusScopeState& candidate : focus_scopes_) {
        if (candidate.id == state.id && candidate.active) {
            remains_active = true;
            break;
        }
    }
    if (!remains_active) {
        return state.id;
    }

    if (preferred_focus) {
        static_cast<void>(request_focus(preferred_focus));
    } else if (options.focus_first &&
               !contains_control(root, focused_.lock())) {
        static_cast<void>(move_focus(true));
    }
    return state.id;
}

bool SurfaceRoot::end_focus_scope(FocusScopeId scope,
                             FocusScopeCloseReason reason) {
    return end_focus_scope(scope, reason, {});
}

bool SurfaceRoot::end_focus_scope(FocusScopeId scope,
                             FocusScopeCloseReason reason,
                             const Control::Ptr& notification_owner) {
    require_root_thread("focus-scope exit");
    FocusScopeList::iterator found = focus_scopes_.begin();
    while (found != focus_scopes_.end() &&
           ((*found).id != scope || !(*found).active)) {
        ++found;
    }
    if (found == focus_scopes_.end()) {
        return false;
    }

    const FocusScopeId closed_id = (*found).id;
    const RuntimeId closed_root_id = (*found).root_id;
    const std::string closed_stable_id = (*found).stable_id;
    (*found).active = false;
    const bool closes_top = std::next(found) == focus_scopes_.end();

    bool restored = false;
    if (closes_top) {
        Control::Ptr restoration;
        bool restoration_requested = false;
        while (!focus_scopes_.empty() && !focus_scopes_.back().active) {
            const FocusScopeState& removed = focus_scopes_.back();
            if (removed.options.restore_focus) {
                restoration = removed.previous_focus.lock();
                restoration_requested = true;
            }
            focus_scopes_.pop_back();
        }

        if (restoration_requested) {
            if (restoration && eligible(restoration) && (*restoration).focusable_ &&
                focus_allowed_by_active_scope(restoration)) {
                restored = request_focus(restoration) &&
                           focused_.lock() == restoration;
            } else if (const Control::Ptr active_root = active_focus_scope_root()) {
                if (!contains_control(active_root, focused_.lock())) {
                    static_cast<void>(move_focus(true));
                }
            } else {
                static_cast<void>(request_focus({}));
            }
        } else if (!focus_allowed_by_active_scope(focused_.lock())) {
            static_cast<void>(move_focus(true));
        }
    }

    const std::size_t depth = focus_scope_depth();
    metrics_.record_focus_scope_closed(restored, depth);
    ++semantic_generation_;
    FocusScopeChange change;
    change.scope = closed_id;
    change.root_id = closed_root_id;
    change.stable_id = closed_stable_id;
    change.depth = depth;
    change.close_reason = reason;
    change.restored_focus = restored;
    if (notification_owner && (*notification_owner).is_alive() &&
        (*notification_owner).surface_root_ == this) {
        // Scope closure is an identity-bearing stream, not a scalar property.
        // Preserve every nested scope transition rather than coalescing all
        // Window scope notifications under one Event address.
        const std::shared_ptr<unsigned char> notification_key = std::make_shared<std::uint8_t>();
        (*notification_owner).publish_change(
            static_cast<const void*>(notification_key.get()),
            FocusScopePublication{
                this, notification_owner, notification_key, change});
    } else {
        focus_scope_changed_.emit(change);
    }
    return true;
}

std::size_t SurfaceRoot::focus_scope_depth() const noexcept {
    std::size_t depth = 0U;
    for (const FocusScopeState& state : focus_scopes_) {
        depth += state.active ? 1U : 0U;
    }
    return depth;
}

Control::Ptr SurfaceRoot::active_focus_scope_root() const noexcept {
    for (FocusScopeList::const_reverse_iterator current = focus_scopes_.rbegin();
         current != focus_scopes_.rend();
         ++current) {
        if ((*current).active) {
            return (*current).root.lock();
        }
    }
    return {};
}

bool SurfaceRoot::focus_allowed_by_active_scope(
    const Control::Ptr& control) const noexcept {
    for (FocusScopeList::const_reverse_iterator current = focus_scopes_.rbegin();
         current != focus_scopes_.rend();
         ++current) {
        if (!(*current).active) {
            continue;
        }
        if (!(*current).options.contain_focus || !control) {
            return true;
        }
        return contains_control((*current).root.lock(), control);
    }
    return true;
}

std::vector<Control::Ptr> SurfaceRoot::focus_candidates(
    const Control::Ptr& scope_root) const {
    ControlList result;
    collect_focus_candidates(scope_root, result, true);
    return result;
}

bool SurfaceRoot::move_focus(bool forward) {
    require_root_thread("focus traversal");
    const Control::Ptr scope_root = active_focus_scope_root();
    ControlList candidates =
        focus_candidates(scope_root ? scope_root : root_);
    if (candidates.empty()) {
        return false;
    }
    const Control::Ptr current = focused_.lock();
    const ControlList::iterator found =
        std::find(candidates.begin(), candidates.end(), current);
    std::size_t index = 0;
    if (found == candidates.end()) {
        index = forward ? 0U : candidates.size() - 1U;
    } else if (forward) {
        index = (static_cast<std::size_t>(found - candidates.begin()) + 1U) %
                candidates.size();
    } else {
        const std::size_t current_index =
            static_cast<std::size_t>(found - candidates.begin());
        index = current_index == 0U ? candidates.size() - 1U
                                   : current_index - 1U;
    }
    return request_focus(candidates[index]);
}

void SurfaceRoot::set_focus_regions(std::vector<Control::Ptr> regions) {
    require_root_thread("focus region mutation");
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const Control::Ptr& region = regions[index];
        if (!region || !(*region).is_alive() || (*region).surface_root() != this) {
            throw std::invalid_argument("GUI.Forms focus regions must be attached to this Window");
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (regions_nest(*regions[other], *region)) {
                throw std::invalid_argument("GUI.Forms focus regions must be distinct and not nested");
            }
        }
    }
    std::vector<FocusRegion> next;
    next.reserve(regions.size());
    const Control::Ptr focused = focused_.lock();
    for (const Control::Ptr& region : regions) {
        FocusRegion entry;
        entry.region = region;
        if (focused && region_holds(*region, *focused)) entry.remembered = focused;
        next.push_back(std::move(entry));
    }
    focus_regions_.swap(next);
}

std::vector<Control::Ptr> SurfaceRoot::focus_regions() const {
    std::vector<Control::Ptr> regions;
    regions.reserve(focus_regions_.size());
    for (const FocusRegion& entry : focus_regions_) {
        const Control::Ptr region = entry.region.lock();
        if (region) regions.push_back(region);
    }
    return regions;
}

bool SurfaceRoot::add_focus_region(const Control::Ptr& region) {
    require_root_thread("focus region mutation");
    if (!region || !(*region).is_alive() || (*region).surface_root() != this) return false;
    for (const FocusRegion& entry : focus_regions_) {
        const Control::Ptr existing = entry.region.lock();
        if (!existing) continue;
        if (regions_nest(*existing, *region)) return false;
    }
    FocusRegion entry;
    entry.region = region;
    const Control::Ptr focused = focused_.lock();
    if (focused && region_holds(*region, *focused)) entry.remembered = focused;
    focus_regions_.push_back(std::move(entry));
    return true;
}

void SurfaceRoot::remember_region_focus(const Control::Ptr& control) {
    for (FocusRegion& entry : focus_regions_) {
        const Control::Ptr region = entry.region.lock();
        if (region && region_holds(*region, *control)) entry.remembered = control;
    }
}

// Focuses the region's remembered control while it is still inside the
// region and can take focus, or else the region's first Tab stop.
bool SurfaceRoot::focus_into_region(FocusRegion& entry) {
    const Control::Ptr region = entry.region.lock();
    if (!region || !(*region).is_alive() || (*region).surface_root() != this || !eligible(region)) {
        return false;
    }
    const Control::Ptr remembered = entry.remembered.lock();
    const bool remembered_usable = remembered && (*remembered).is_alive() &&
        (*remembered).surface_root() == this && region_holds(*region, *remembered);
    if (remembered_usable && request_focus(remembered)) return true;
    const ControlList candidates = focus_candidates(region);
    for (const Control::Ptr& candidate : candidates) {
        if (request_focus(candidate)) return true;
    }
    return false;
}

bool SurfaceRoot::move_focus_region(bool forward) {
    require_root_thread("focus region traversal");
    if (!focus_scopes_.empty() || focus_regions_.empty()) return false;
    const std::size_t count = focus_regions_.size();
    // The region holding the focused control, if any.
    const Control::Ptr focused = focused_.lock();
    std::optional<std::size_t> current{};
    for (std::size_t index = 0; index < count && focused; ++index) {
        const Control::Ptr region = focus_regions_[index].region.lock();
        if (region && region_holds(*region, *focused)) {
            current = index;
            break;
        }
    }
    // The regions are tried one after another from `origin`, wrapping: from
    // inside a region, each other region once, starting beside it; from
    // outside every region, all of them, from the first (forward) or the
    // last (backward), as if leaving a region just before the first or just
    // after the last.
    std::size_t origin = forward ? count - 1U : 0U;
    std::size_t attempts = count;
    if (current) {
        origin = *current;
        attempts = count - 1U;
    }
    for (std::size_t step = 0; step < attempts; ++step) {
        const std::size_t index = forward ? (origin + 1U + step) % count : (origin + count - 1U - step) % count;
        if (focus_into_region(focus_regions_[index])) return true;
    }
    return false;
}

void SurfaceRoot::capture_pointer(const Control::Ptr& control, std::uint64_t pointer_id) {
    require_root_thread("pointer capture");
    if (control && !eligible(control)) {
        throw std::logic_error("GUI.Forms cannot capture pointer to a foreign control");
    }
    change_pointer_capture(control, pointer_id == 0 ? 1 : pointer_id, false);
}

void SurfaceRoot::release_pointer() {
    require_root_thread("pointer capture release");
    change_pointer_capture({}, 0, false);
}

void SurfaceRoot::change_pointer_capture(const Control::Ptr& control,
                                    std::uint64_t pointer_id,
                                    bool revoked) {
    const Control::Ptr previous = captured_.lock();
    if (previous == control && (!control || captured_pointer_id_ == pointer_id)) {
        return;
    }
    if (control) {
        captured_ = control;
        captured_pointer_id_ = pointer_id;
    } else {
        captured_.reset();
        captured_pointer_id_ = 0;
    }
    if (previous && host_services_ != nullptr) { (*host_services_).revoke_cursor_interaction(); }
    if (revoked && previous) {
        metrics_.record_capture_revocation();
    }
    PointerCaptureChange change;
    change.captured = static_cast<bool>(control);
    change.control_id = control ? (*control).runtime_id() : RuntimeId{};
    change.stable_id = control ? std::string((*control).stable_id().value()) : std::string{};
    change.pointer_id = control ? pointer_id : 0;
    const Control::Ptr publication_owner = control ? control : previous;
    if (publication_owner) {
        (*publication_owner).publish_change(
            static_cast<const void*>(&pointer_capture_changed_),
            PointerCapturePublication{this, publication_owner, change});
    } else {
        pointer_capture_changed_.emit(change);
    }
}

bool SurfaceRoot::dispatch_pointer(PointerEvent event) {
    require_root_thread("pointer dispatch");
    const ShadowFrameScope shadow_frame(ShadowFrameKind::input_dispatch, (*root_).stable_id().value(), "pointer", 0U);
    if (in_paint_) {
        return defer_input(DeferredInput{std::move(event)});
    }
    metrics_.record_input();
    note_engine_pointer(event);
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        set_focus_cue_visible(false);
    }
    if (event.action == PointerAction::move) {
        Control::Ptr next_hover = hit_test(event.position);
        Control::Ptr previous_hover = hovered_.lock();
        if (previous_hover != next_hover) {
            if (previous_hover && eligible(previous_hover)) {
                PointerEvent leave = event;
                leave.action = PointerAction::leave;
                leave.button = PointerButton::none;
                leave.phase = EventPhase::target;
                leave.handled = false;
                metrics_.record_callback_emitted();
                set_pointer_over(*previous_hover, false);
                (*previous_hover).on_pointer(leave);
                (*previous_hover).publish_change(
                    (*previous_hover).pointer_observed_, leave);
            }
            hovered_ = next_hover;
            if (next_hover && eligible(next_hover)) {
                PointerEvent enter = event;
                enter.action = PointerAction::enter;
                enter.button = PointerButton::none;
                enter.phase = EventPhase::target;
                enter.handled = false;
                metrics_.record_callback_emitted();
                set_pointer_over(*next_hover, true);
                (*next_hover).on_pointer(enter);
                (*next_hover).publish_change((*next_hover).pointer_observed_, enter);
            }
        }
        if (tool_tip_presenter_) {
            (*tool_tip_presenter_).pointer_moved(event.position, hovered_.lock(), FrameClock::now());
        }
    } else if (event.action == PointerAction::down && tool_tip_presenter_) {
        (*tool_tip_presenter_).pointer_pressed();
    } else if (event.action == PointerAction::leave) {
        // The pointer left the window: nothing is hovered until it returns,
        // and the control it was over is no longer hot. The leave is routed
        // below as before.
        if (Control::Ptr previous_hover = hovered_.lock()) {
            set_pointer_over(*previous_hover, false);
        }
        hovered_.reset();
        if (tool_tip_presenter_) (*tool_tip_presenter_).pointer_left();
    }
    Control::Ptr target = captured_.lock();
    if (!target) {
        target = hit_test(event.position);
    }
    if (!target) {
        return false;
    }

    // A release callback may synchronously open a modal window. Relinquish both
    // retained and platform capture before user code runs so that the modal can
    // receive its first pointer message. Keep strong local references for this
    // route and for the later click qualification.
    Control::Ptr pressed_for_release;
    const bool primary_release = event.action == PointerAction::up &&
        event.button == PointerButton::primary;
    if (primary_release) {
        pressed_for_release = pressed_.lock();
        pressed_.reset();
        release_pointer();
    }

    const std::vector<Control::Ptr> route = route_to(target);
    event.phase = EventPhase::preview;
    for (const std::shared_ptr<gui_forms::Control>& control : route) {
        if (!eligible(control)) {
            continue;
        }
        metrics_.record_callback_emitted();
        (*control).on_pointer_preview(event);
        (*control).publish_change(
            (*control).pointer_preview_observed_, event);
        if (event.handled) {
            return true;
        }
    }

    if (!eligible(target)) {
        return event.handled;
    }

    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        const bool accepts_press = !(*target).focusable_ || request_focus(target);
        if (!accepts_press) {
            // Prevent-mode validation rejects the input transaction that
            // attempted to enter this focusable target. In particular, a
            // Button must not publish Click after focus validation failed.
            event.handled = true;
            return true;
        }
        if (eligible(target)) {
            pressed_ = target;
            capture_pointer(target, event.pointer_id == 0 ? 1 : event.pointer_id);
        }
    }

    if (eligible(target)) {
        event.phase = EventPhase::target;
        metrics_.record_callback_emitted();
        (*target).on_pointer(event);
        (*target).publish_change((*target).pointer_observed_, event);
    }
    if (!event.handled) {
        event.phase = EventPhase::bubble;
        for (ControlList::const_reverse_iterator iterator = route.rbegin();
             iterator != route.rend(); ++iterator) {
            if (*iterator == target || !eligible(*iterator)) {
                continue;
            }
            metrics_.record_callback_emitted();
            (*(*iterator)).on_pointer_bubble(event);
            if (event.handled) {
                break;
            }
        }
    }

    if (primary_release) {
        Control::Ptr released_over = hit_test(event.position);
        if (pressed_for_release && pressed_for_release == released_over &&
            eligible(pressed_for_release)) {
            metrics_.record_callback_emitted();
            (*pressed_for_release).on_activate();
            metrics_.record_activation();
        }
    }
    return event.handled;
}

bool SurfaceRoot::dispatch_key(KeyEvent event) {
    require_root_thread("key dispatch");
    const ShadowFrameScope shadow_frame(ShadowFrameKind::input_dispatch, (*root_).stable_id().value(), "key", 0U);
    if (in_paint_) {
        return defer_input(DeferredInput{std::move(event)});
    }
    const bool key_down = event.action == KeyAction::down;
    const bool handled = route_key(std::move(event));
    // A key may have moved the keyboard within the focused control, or
    // shown the focus cue: the presenter looks at what focus is on again.
    if (key_down && tool_tip_presenter_) (*tool_tip_presenter_).key_handled();
    return handled;
}

bool SurfaceRoot::route_key(KeyEvent event) {
    metrics_.record_input();
    if (event.action == KeyAction::down) {
        set_focus_cue_visible(true);
    }
    const bool traversal_key = event.action == KeyAction::down &&
        event.physical_key == PhysicalKey::tab;
    // F6 and Shift+F6 cycle the focus regions; other modifiers leave F6 to
    // the application.
    const bool region_key = event.action == KeyAction::down &&
        event.physical_key == PhysicalKey::f6 &&
        (event.modifiers == Modifier::none || event.modifiers == Modifier::shift);
    const bool forward =
        (static_cast<std::uint8_t>(event.modifiers) &
         static_cast<std::uint8_t>(Modifier::shift)) == 0U;
    if (focus_scopes_.empty() && dispatch_accelerator(event, true)) return true;
    Control::Ptr target = focused_.lock();
    if (!target || !eligible(target)) {
        if (dispatch_accelerator(event, false)) return true;
        if (dispatch_dialog_key(event)) return true;
        if (region_key) return move_focus_region(forward);
        return traversal_key && move_focus(forward);
    }
    const std::vector<Control::Ptr> route = route_to(target);
    event.phase = EventPhase::preview;
    for (const std::shared_ptr<gui_forms::Control>& control : route) {
        if (!eligible(control)) {
            continue;
        }
        metrics_.record_callback_emitted();
        (*control).on_key_preview(event);
        if (event.handled) {
            return true;
        }
    }
    if (!eligible(target)) {
        return event.handled;
    }
    event.phase = EventPhase::target;
    metrics_.record_callback_emitted();
    (*target).on_key(event);
    if (!event.handled) {
        event.phase = EventPhase::bubble;
        for (ControlList::const_reverse_iterator iterator = route.rbegin();
             iterator != route.rend(); ++iterator) {
            if (*iterator == target || !eligible(*iterator)) {
                continue;
            }
            metrics_.record_callback_emitted();
            (*(*iterator)).on_key_bubble(event);
            if (event.handled) {
                break;
            }
        }
    }
    if (!event.handled && dispatch_accelerator(event, false)) {
        return true;
    }
    if (!event.handled && dispatch_dialog_key(event)) return true;
    if (!event.handled && traversal_key) {
        return move_focus(forward);
    }
    if (!event.handled && region_key) {
        return move_focus_region(forward);
    }
    return event.handled;
}

bool SurfaceRoot::dispatch_text(TextInputEvent event) {
    require_root_thread("text dispatch");
    const ShadowFrameScope shadow_frame(ShadowFrameKind::input_dispatch, (*root_).stable_id().value(), "text", 0U);
    if (in_paint_) {
        return defer_input(DeferredInput{std::move(event)});
    }
    metrics_.record_input();
    Control::Ptr target = focused_.lock();
    if (!target || !eligible(target)) {
        return false;
    }
    metrics_.record_callback_emitted();
    (*target).on_text_input(event);
    return event.handled;
}

DeferredInputSnapshot SurfaceRoot::deferred_input_snapshot() const noexcept {
    return {deferred_inputs_.size(), maximum_deferred_inputs,
            deferred_inputs_received_, deferred_inputs_delivered_,
            deferred_input_moves_coalesced_,
            deferred_drag_overs_coalesced_, deferred_inputs_rejected_,
            deferred_inputs_abandoned_, deferred_input_faults_,
            deferred_input_drains_, deferred_input_drain_queued_,
            draining_deferred_input_};
}

bool SurfaceRoot::defer_input(DeferredInput input) {
    if (gui_forms::PointerEvent* pointer = std::get_if<PointerEvent>(&input);
        pointer && (*pointer).action == PointerAction::move &&
        !deferred_inputs_.empty()) {
        if (gui_forms::PointerEvent* previous =
                std::get_if<PointerEvent>(&deferred_inputs_.back());
            previous && (*previous).action == PointerAction::move &&
            (*previous).pointer_id == (*pointer).pointer_id) {
            *previous = std::move(*pointer);
            ++deferred_inputs_received_;
            ++deferred_input_moves_coalesced_;
            return true;
        }
    }
    if (gui_forms::DragEvent* drag = std::get_if<DragEvent>(&input);
        drag && (*drag).action == DragAction::over &&
        !deferred_inputs_.empty()) {
        if (gui_forms::DragEvent* previous =
                std::get_if<DragEvent>(&deferred_inputs_.back());
            previous && (*previous).action == DragAction::over &&
            (*previous).session_id == (*drag).session_id) {
            *previous = std::move(*drag);
            ++deferred_inputs_received_;
            ++deferred_drag_overs_coalesced_;
            return true;
        }
    }
    if (deferred_inputs_.size() >= maximum_deferred_inputs) {
        ++deferred_inputs_rejected_;
        return false;
    }
    deferred_inputs_.push_back(std::move(input));
    ++deferred_inputs_received_;
    return true;
}

void SurfaceRoot::schedule_deferred_input_drain() noexcept {
    if (deferred_inputs_.empty() || deferred_input_drain_queued_ ||
        draining_deferred_input_ || in_paint_) {
        return;
    }
    deferred_input_drain_queued_ = true;
    try {
        const detail::BoundMemberFunction<void (SurfaceRoot::*)()> drain(
            *this, &SurfaceRoot::drain_deferred_input);
        deferred_input_drain_operation_ = begin_invoke(drain);
    } catch (...) {
        deferred_input_drain_queued_ = false;
        ++deferred_input_faults_;
        deferred_inputs_abandoned_ += deferred_inputs_.size();
        deferred_inputs_.clear();
    }
}

void SurfaceRoot::drain_deferred_input() {
    require_root_thread("deferred input drain");
    deferred_input_drain_operation_ = {};
    deferred_input_drain_queued_ = false;
    if (in_paint_) {
        schedule_deferred_input_drain();
        return;
    }
    if (draining_deferred_input_ || deferred_inputs_.empty()) return;

    draining_deferred_input_ = true;
    std::exception_ptr first_fault;
    while (!deferred_inputs_.empty()) {
        DeferredInput input = std::move(deferred_inputs_.front());
        deferred_inputs_.pop_front();
        try {
            std::visit(DeferredInputVisitor{this}, std::move(input));
            ++deferred_inputs_delivered_;
        } catch (...) {
            ++deferred_input_faults_;
            if (!first_fault) first_fault = std::current_exception();
        }
    }
    draining_deferred_input_ = false;
    ++deferred_input_drains_;
    if (!deferred_inputs_.empty()) schedule_deferred_input_drain();
    if (first_fault) std::rethrow_exception(first_fault);
}

void SurfaceRoot::abandon_deferred_input() noexcept {
    static_cast<void>(deferred_input_drain_operation_.cancel());
    deferred_input_drain_operation_ = {};
    deferred_inputs_abandoned_ += deferred_inputs_.size();
    deferred_inputs_.clear();
    deferred_input_drain_queued_ = false;
}

void SurfaceRoot::abandon_deferred_drag() noexcept {
    const std::size_t before = deferred_inputs_.size();
    std::deque<DeferredInput>::iterator input = deferred_inputs_.begin();
    while (input != deferred_inputs_.end()) {
        if (std::holds_alternative<DragEvent>(*input)) {
            input = deferred_inputs_.erase(input);
        } else {
            ++input;
        }
    }
    deferred_inputs_abandoned_ += before - deferred_inputs_.size();
    if (deferred_inputs_.empty() && deferred_input_drain_queued_) {
        static_cast<void>(deferred_input_drain_operation_.cancel());
        deferred_input_drain_operation_ = {};
        deferred_input_drain_queued_ = false;
    }
}

Control::Ptr SurfaceRoot::drop_target_at(Point position) {
    for (Control::Ptr target = hit_test(position); target; target = (*target).parent()) {
        if (eligible(target) && (*target).allow_drop()) {
            return target;
        }
    }
    return {};
}

DragDispatchResult SurfaceRoot::route_drag(const Control::Ptr& target, DragEvent event) {
    DragDispatchResult result;
    if (!target || !eligible(target) || !(*target).allow_drop()) {
        return result;
    }
    const std::vector<Control::Ptr> route = route_to(target);
    event.phase = EventPhase::preview;
    for (const std::shared_ptr<gui_forms::Control>& control : route) {
        if (!eligible(control)) {
            continue;
        }
        metrics_.record_callback_emitted();
        (*control).on_drag_preview(event);
        if (event.handled) {
            break;
        }
    }
    if (!event.handled && eligible(target) && (*target).allow_drop()) {
        event.phase = EventPhase::target;
        metrics_.record_callback_emitted();
        (*target).on_drag(event);
    }
    if (!event.handled) {
        event.phase = EventPhase::bubble;
        for (ControlList::const_reverse_iterator iterator = route.rbegin();
             iterator != route.rend(); ++iterator) {
            if (*iterator == target || !eligible(*iterator)) {
                continue;
            }
            metrics_.record_callback_emitted();
            (*(*iterator)).on_drag_bubble(event);
            if (event.handled) {
                break;
            }
        }
    }
    result.handled = event.handled;
    if (single_allowed_effect(event.accepted_effect, event.allowed_effects)) {
        result.accepted_effect = event.accepted_effect;
    }
    return result;
}

void SurfaceRoot::merge_drag_route(DragDispatchResult& aggregate,
                              const Control::Ptr& target,
                              DragEvent event) {
    const DragDispatchResult current = route_drag(target, std::move(event));
    aggregate.handled = aggregate.handled || current.handled;
    if (current.accepted_effect != DragEffect::none) {
        aggregate.accepted_effect = current.accepted_effect;
    }
}

void SurfaceRoot::leave_drag_target(const DragEvent& event,
                               DragDispatchResult& aggregate) {
    const Control::Ptr previous = drag_target_.lock();
    if (previous) {
        DragEvent leave = event;
        leave.action = DragAction::leave;
        leave.accepted_effect = DragEffect::none;
        leave.handled = false;
        merge_drag_route(aggregate, previous, std::move(leave));
    }
    drag_target_.reset();
}

DragDispatchResult SurfaceRoot::dispatch_drag(DragEvent event) {
    require_root_thread("drag dispatch");
    const ShadowFrameScope shadow_frame(ShadowFrameKind::input_dispatch, (*root_).stable_id().value(), "drag", 0U);
    if (in_paint_) {
        const std::uint64_t session = event.session_id;
        const DragEffect allowed = event.allowed_effects;
        const bool retained =
            defer_input(DeferredInput{std::move(event)});
        const DragEffect prior =
            drag_session_id_ == session &&
                    single_allowed_effect(drag_last_accepted_effect_, allowed)
                ? drag_last_accepted_effect_
                : DragEffect::none;
        return {retained, retained ? prior : DragEffect::none,
                retained, !retained};
    }
    metrics_.record_input();

    DragDispatchResult aggregate;

    if (event.action == DragAction::leave) {
        if (drag_session_id_ == event.session_id) {
            leave_drag_target(event, aggregate);
            drag_session_id_ = 0;
            drag_last_accepted_effect_ = DragEffect::none;
        }
        return aggregate;
    }

    if (drag_session_id_ != 0 && drag_session_id_ != event.session_id) {
        leave_drag_target(event, aggregate);
        drag_session_id_ = 0;
        drag_last_accepted_effect_ = DragEffect::none;
    }
    if (drag_session_id_ == 0) {
        drag_session_id_ = event.session_id;
    }

    Control::Ptr candidate = drop_target_at(event.position);
    Control::Ptr previous = drag_target_.lock();
    if (candidate != previous) {
        leave_drag_target(event, aggregate);
        if (candidate) {
            drag_target_ = candidate;
            DragEvent enter = event;
            enter.action = DragAction::enter;
            enter.accepted_effect = DragEffect::none;
            enter.handled = false;
            merge_drag_route(aggregate, candidate, std::move(enter));
        }
    }

    const DragAction action = event.action;
    if (action == DragAction::over && candidate) {
        event.accepted_effect = DragEffect::none;
        event.handled = false;
        merge_drag_route(aggregate, candidate, std::move(event));
    } else if (action == DragAction::drop && candidate) {
        event.accepted_effect = DragEffect::none;
        event.handled = false;
        merge_drag_route(aggregate, candidate, std::move(event));
        drag_target_.reset();
        drag_session_id_ = 0;
    } else if (action == DragAction::drop) {
        drag_target_.reset();
        drag_session_id_ = 0;
    }
    if (action == DragAction::drop) {
        drag_last_accepted_effect_ = DragEffect::none;
    } else {
        drag_last_accepted_effect_ = aggregate.accepted_effect;
    }
    return aggregate;
}

void SurfaceRoot::cancel_drag() noexcept {
    drag_target_.reset();
    drag_session_id_ = 0;
    drag_last_accepted_effect_ = DragEffect::none;
    abandon_deferred_drag();
}

void SurfaceRoot::attach_subtree_state(const Control::Ptr& control,
                                  const Control::WeakPtr& parent,
                                  ControlList& attached) {
    // Listed first, so that a failure past this point is undone by the
    // caller's rollback of `attached`.
    attached.push_back(control);
    (*control).surface_slot_ = control_table_.assign(*control);
    (*control).window_ = presenting_window_;
    (*control).surface_root_ = this;
    {
        std::scoped_lock lock((*control).dispatcher_mutex_);
        (*control).dispatcher_state_ = dispatcher_state_;
    }
    (*control).parent_ = parent;
    (*control).lifecycle_notification_ = true;
    for (const Control::Ptr& child : (*control).children_) {
        attach_subtree_state(child, control, attached);
    }
}

void SurfaceRoot::detach_subtree_state(const Control::Ptr& control,
                                  ControlList& detached) {
    (*control).lifecycle_notification_ = true;
    detached.push_back(control);
    // The control's part records go with it; their handles become stale.
    const std::size_t released = engine_parts_.release_control((*control).stable_id_.value());
    if (released > 0U) metrics_.record_engine_parts_released(released, engine_parts_.record_count());
    // So do its motion values: they jump to their ends, without listeners.
    motion_clock_.release_owner(*control);
    erase_containing_block_entries((*control).runtime_id());
    (*control).on_detaching_from_window(*this);
    (*control).window_ = nullptr;
    (*control).surface_root_ = nullptr;
    control_table_.release((*control).surface_slot_);
    (*control).surface_slot_ = {};
    {
        std::scoped_lock lock((*control).dispatcher_mutex_);
        (*control).dispatcher_state_.reset();
    }
    for (const Control::Ptr& child : (*control).children_) {
        detach_subtree_state(child, detached);
    }
}

bool SurfaceRoot::collect_inline_restyles(const Theme& theme, std::vector<detail::InlineRestyle>& restyles,
                                     std::vector<ThemeCompileDiagnostic>& diagnostics) const {
    if (root_ && !(*root_).theme_override_) {
        if (!(*root_).collect_inline_restyles(theme, restyles, diagnostics)) return false;
    }
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        const Control::Ptr overlay = (*popup).popup();
        if (!overlay || (*overlay).theme_override_) continue;
        if (!(*overlay).collect_inline_restyles(theme, restyles, diagnostics)) return false;
    }
    return true;
}

void SurfaceRoot::commit_inline_restyles(std::vector<detail::InlineRestyle>& restyles) {
    Control::commit_inline_restyles(restyles);
}

void SurfaceRoot::attach_subtree(const Control::Ptr& control, const Control::WeakPtr& parent) {
    require_root_thread("visual-tree attachment");
    register_subtree(control);
    ControlList controls;
    ControlList notified;
    in_lifecycle_notification_ = true;
    try {
        attach_subtree_state(control, parent, controls);
        // The theme in effect may have changed with the window or the parent
        // (P12b).
        for (const Control::Ptr& current : controls) (*current).refresh_inline_style();
        for (const Control::Ptr& current : controls) {
            notified.push_back(current);
            if ((*current).authored_surface_material_) {
                (*current).validate_authored_surface_material_images(
                    *(*current).authored_surface_material_);
            }
            (*current).on_attached_to_window();
        }
    } catch (...) {
        for (ControlList::reverse_iterator current = controls.rbegin();
             current != controls.rend(); ++current) {
            (*(*current)).window_ = nullptr;
            (*(*current)).surface_root_ = nullptr;
            control_table_.release((*(*current)).surface_slot_);
            (*(*current)).surface_slot_ = {};
            {
                std::scoped_lock lock((*(*current)).dispatcher_mutex_);
                (*(*current)).dispatcher_state_.reset();
            }
        }
        for (ControlList::reverse_iterator current = notified.rbegin();
             current != notified.rend(); ++current) {
            (*(*current)).on_detached_from_window();
        }
        for (const Control::Ptr& current : controls) {
            (*current).lifecycle_notification_ = false;
        }
        in_lifecycle_notification_ = false;
        unregister_subtree(control);
        metrics_.set_population(stable_ids_.size(), stable_ids_.size());
        throw;
    }
    for (const Control::Ptr& current : controls) {
        (*current).on_attachment_committed();
    }
    for (const Control::Ptr& current : controls) {
        (*current).lifecycle_notification_ = false;
    }
    in_lifecycle_notification_ = false;
    metrics_.set_population(stable_ids_.size(), stable_ids_.size());
    static_cast<void>(recompute_subtree_dirty(root_));
    mark_subtree_dirty(*control, invalidation::visual_tree);
}

void SurfaceRoot::detach_subtree(const Control::Ptr& control) {
    require_root_thread("visual-tree detachment");
    clear_dialog_targets_for_subtree(control);
    close_popups_for_subtree(control);
    close_focus_scopes_for_subtree(control);
    revoke_interaction_for_subtree(control, true);
    if ((*control).surface_root_ != this) {
        return;
    }
    const Rect old_bounds = absolute_bounds_of(*control);
    add_subtree_damage(control);
    unregister_subtree(control);
    ControlList controls;
    in_lifecycle_notification_ = true;
    detach_subtree_state(control, controls);
    for (ControlList::reverse_iterator current = controls.rbegin();
         current != controls.rend(); ++current) {
        (*(*current)).on_detached_from_window();
        (*(*current)).lifecycle_notification_ = false;
    }
    in_lifecycle_notification_ = false;
    touch_paint();
    ++semantic_generation_;
    if (semantic_generation_ == 0U) ++semantic_generation_;
    metrics_.record_dirty_mark(old_bounds.area());
    metrics_.set_population(stable_ids_.size(), stable_ids_.size());
    update_display_cache_metrics();
}

void SurfaceRoot::dispose_subtree(const Control::Ptr& control) noexcept {
    clear_dialog_targets_for_subtree(control);
    if (control == root_) {
        abandon_deferred_input();
    }
    const std::uint64_t disposal_count = (*control).subtree_size();
    revoke_focus_scopes_for_subtree(control);
    revoke_interaction_for_subtree(control, false);
    const Rect old_bounds = absolute_bounds_of(*control);
    add_subtree_damage(control);
    unregister_subtree(control);
    ControlList controls;
    in_lifecycle_notification_ = true;
    detach_subtree_state(control, controls);
    for (ControlList::reverse_iterator current = controls.rbegin();
         current != controls.rend(); ++current) {
        (*(*current)).on_detached_from_window();
        (*(*current)).lifecycle_notification_ = false;
    }
    in_lifecycle_notification_ = false;
    if (std::shared_ptr<gui_forms::Control> visual_parent = (*control).parent_.lock()) {
        const Control::ChildList::iterator found =
            std::find((*visual_parent).children_.begin(),
                      (*visual_parent).children_.end(), control);
        if (found != (*visual_parent).children_.end()) {
            (*visual_parent).children_.erase(found);
        }
        // Explicit child disposal is also a structural mutation of its live
        // parent. Retire the parent's retained layout/paint chunk now; merely
        // setting the Window-level dirty booleans leaves recursive layout with
        // no dirty node to visit and composites cannot reconcile their model.
        if ((*visual_parent).is_alive()) {
            mark_dirty(*visual_parent, invalidation::visual_tree);
        }
    }
    (*control).parent_.reset();
    touch_paint();
    layout_dirty_ = true;
    hit_test_dirty_ = true;
    ++semantic_generation_;
    if (semantic_generation_ == 0U) ++semantic_generation_;
    metrics_.record_dirty_mark(old_bounds.area());
    metrics_.record_disposal(disposal_count);
    metrics_.set_population(stable_ids_.size(), stable_ids_.size());
    static_cast<void>(recompute_subtree_dirty(root_));
    update_display_cache_metrics();
}

void SurfaceRoot::clear_dialog_targets_for_subtree(
    const Control::Ptr& control) noexcept {
    if (!control) return;
    if (Control::Ptr accept = accept_button_.lock();
        contains_control(control, accept)) {
        if ((*accept).is_alive()) {
            try { (*accept).notify_default(false); } catch (...) {}
        }
        accept_button_.reset();
    }
    if (contains_control(control, cancel_button_.lock())) cancel_button_.reset();
}

void SurfaceRoot::revoke_interaction_for_subtree(const Control::Ptr& control,
                                            bool notify_focus) {
    Control::Ptr focused = focused_.lock();
    if (contains_control(control, focused)) {
        focused_.reset();
        metrics_.record_focus_transition();
        metrics_.record_focus_revocation();
        if (notify_focus && focused && (*focused).is_alive()) {
            (*focused).publish_change(
                static_cast<const void*>(&(*focused).focus_observed_),
                FocusChangePublication{this, focused, false});
        }
    }
    if (contains_control(control, captured_.lock())) {
        change_pointer_capture({}, 0, true);
    }
    if (contains_control(control, pressed_.lock())) {
        pressed_.reset();
        metrics_.record_press_revocation();
    }
    if (contains_control(control, hovered_.lock())) {
        forget_hover();
    }
    if (contains_control(control, drag_target_.lock())) {
        cancel_drag();
    }
}

void SurfaceRoot::close_focus_scopes_for_subtree(const Control::Ptr& control) {
    std::vector<FocusScopeId> closing;
    closing.reserve(focus_scopes_.size());
    for (const FocusScopeState& state : focus_scopes_) {
        if (state.active && contains_control(control, state.root.lock())) {
            closing.push_back(state.id);
        }
    }
    for (FocusScopeId scope : closing) {
        static_cast<void>(end_focus_scope(
            scope, FocusScopeCloseReason::owner_unavailable, control));
    }
}

void SurfaceRoot::revoke_focus_scopes_for_subtree(
    const Control::Ptr& control) noexcept {
    std::size_t closed = 0;
    FocusScopeList::iterator state = focus_scopes_.begin();
    while (state != focus_scopes_.end()) {
        const bool remove = (*state).active &&
            contains_control(control, (*state).root.lock());
        if (remove) {
            ++closed;
            state = focus_scopes_.erase(state);
        } else {
            ++state;
        }
    }
    while (closed-- > 0U) {
        metrics_.record_focus_scope_closed(false, focus_scope_depth());
    }
}

void SurfaceRoot::on_eligibility_changed(const Control::Ptr& control) {
    require_root_thread("eligibility mutation");
    close_popups_for_subtree(control);
    close_focus_scopes_for_subtree(control);
    revoke_interaction_for_subtree(control, true);
}

void SurfaceRoot::on_hit_test_transparency_changed(const Control::Ptr& control) {
    require_root_thread("hit-test transparency mutation");
    if (!control) return;
    if (contains_control(control, captured_.lock())) {
        change_pointer_capture({}, 0, true);
    }
    if (contains_control(control, pressed_.lock())) {
        pressed_.reset();
        metrics_.record_press_revocation();
    }
    if (contains_control(control, hovered_.lock())) forget_hover();
    if (contains_control(control, drag_target_.lock())) cancel_drag();
}

void SurfaceRoot::set_pointer_over(Control& control, bool over) {
    if (control.pointer_over_ == over) return;
    control.pointer_over_ = over;
    const std::span<const AnatomyState> declared = control.anatomy().states;
    const bool reads_hot = std::find(declared.begin(), declared.end(), AnatomyState::hot) != declared.end();
    if (reads_hot) control.invalidate(Dirty::paint);
}

void SurfaceRoot::forget_hover() noexcept {
    const Control::Ptr previous_hover = hovered_.lock();
    if (previous_hover) (*previous_hover).pointer_over_ = false;
    hovered_.reset();
}

void SurfaceRoot::publish_control_availability(Control& control) {
    require_root_thread("control availability publication");
    if (!control.is_alive() || control.surface_root_ != this) return;
    const ControlAvailabilityChange change{
        control.runtime_id(), std::string(control.stable_id().value()),
        control.effectively_visible(), control.effectively_enabled()};
    const std::shared_ptr<gui_forms::Control> retained = control.shared_from_this();
    control.publish_change(
        static_cast<const void*>(&control_availability_changed_),
        ControlAvailabilityPublication{this, retained, change});
}

void SurfaceRoot::collect_subtree_registration(
    const Control::Ptr& control, ControlList& controls,
    std::unordered_set<std::string>& local_ids) {
        if (!(*control).is_alive()) {
            throw std::logic_error("GUI.Forms cannot attach a disposed control");
        }
        const std::string id((*control).stable_id_.value());
        if (!local_ids.insert(id).second || stable_ids_.contains(id)) {
            throw std::logic_error("GUI.Forms duplicate stable ID: " + id);
        }
        controls.push_back(control);
        for (const Control::Ptr& child : (*control).children_) {
            collect_subtree_registration(child, controls, local_ids);
        }
}

void SurfaceRoot::register_subtree(const Control::Ptr& control) {
    ControlList controls;
    std::unordered_set<std::string> local_ids;
    collect_subtree_registration(control, controls, local_ids);
    for (const std::shared_ptr<gui_forms::Control>& current : controls) {
        stable_ids_.emplace(std::string((*current).stable_id_.value()), current);
    }
}

void SurfaceRoot::unregister_subtree(const Control::Ptr& control) {
    stable_ids_.erase(std::string((*control).stable_id_.value()));
    for (const std::shared_ptr<gui_forms::Control>& child : (*control).children_) {
        unregister_subtree(child);
    }
}

void SurfaceRoot::mark_dirty(Control& control, Dirty requested_dirty) {
    require_root_thread("dirty mutation");
    if (has_dirty(requested_dirty, Dirty::measure)) {
        requested_dirty |= Dirty::arrange;
    }
    control.add_dirty(requested_dirty);
    control.subtree_dirty_ |= requested_dirty;
    control_table_.touch(control.surface_slot_, requested_dirty);
    metrics_.record_mutation();

    if (has_dirty(requested_dirty, Dirty::layout)) {
        static std::atomic<std::uint64_t> traced_layout_invalidations{};
        if (layout_invalidation_traced()) {
            const std::uint64_t sequence =
                traced_layout_invalidations.fetch_add(1U,
                    std::memory_order_relaxed) + 1U;
            if (sequence <= 2048U) {
                std::fprintf(stderr,
                    "gui-forms-layout-dirty=%llu|control:%.*s|flags:%u\n",
                    static_cast<unsigned long long>(sequence),
                    static_cast<int>(control.stable_id().value().size()),
                    control.stable_id().value().data(),
                    static_cast<unsigned>(requested_dirty));
            }
        }
        note_suspended_layout_request(control);
        mark_layout_ancestors(control);
        layout_dirty_ = true;
        if (in_layout_) {
            second_layout_pass_requested_ = true;
        }
    }
    for (Control::Ptr ancestor = control.parent(); ancestor;
         ancestor = (*ancestor).parent()) {
        (*ancestor).subtree_dirty_ |= requested_dirty;
    }
    if (has_dirty(requested_dirty, Dirty::hit_test)) {
        hit_test_dirty_ = true;
    }
    if (has_dirty(requested_dirty, Dirty::semantics)) {
        ++semantic_generation_;
        if (semantic_generation_ == 0U) ++semantic_generation_;
    }
    if (has_dirty(requested_dirty, Dirty::paint)) {
        touch_paint();
        Rect bounds = paint_damage_bounds_of(control);
        if (bounds.empty()) {
            bounds = control.requested_bounds_;
        }
        add_damage(bounds, control.paint_plane_);
        metrics_.record_dirty_mark(bounds.area());
    } else {
        metrics_.record_dirty_mark(0.0);
    }
}

void SurfaceRoot::mark_paint_dirty(Control& control, Rect local_damage) {
    require_root_thread("localized paint mutation");
    if (control.surface_root_ != this) {
        throw std::invalid_argument(
            "localized paint mutation requires an attached control");
    }
    local_damage = Rect::intersection(local_damage, control.client_rectangle());
    if (local_damage.empty()) return;

    control.dirty_ |= Dirty::paint;
    control.subtree_dirty_ |= Dirty::paint;
    control_table_.touch(control.surface_slot_, Dirty::paint);
    for (Control::Ptr ancestor = control.parent(); ancestor;
         ancestor = (*ancestor).parent()) {
        (*ancestor).subtree_dirty_ |= Dirty::paint;
    }
    metrics_.record_mutation();
    touch_paint();
    const Rect bounds = absolute_bounds_of(control);
    const Rect window_damage{bounds.x + local_damage.x,
                             bounds.y + local_damage.y,
                             local_damage.width, local_damage.height};
    add_damage(window_damage, control.paint_plane_);
    metrics_.record_dirty_mark(window_damage.area());
}

void SurfaceRoot::mark_child_layout_slot(Control& control) {
    require_root_thread("child layout-slot mutation");
    constexpr Dirty effects = Dirty::arrange | Dirty::hit_test |
                              Dirty::semantics | Dirty::accessibility;
    control.dirty_ |= effects;
    control_table_.touch(control.surface_slot_, effects);
    note_suspended_layout_request(control);
    control.subtree_dirty_ |= effects;
    for (Control::Ptr ancestor = control.parent(); ancestor;
         ancestor = (*ancestor).parent()) {
        (*ancestor).subtree_dirty_ |= effects;
    }
    layout_dirty_ = true;
    hit_test_dirty_ = true;
    ++semantic_generation_;
    if (semantic_generation_ == 0U) ++semantic_generation_;
}

SemanticSnapshot SurfaceRoot::semantic_snapshot() {
    require_root_thread("semantic snapshot");
    if (in_semantic_snapshot_) {
        throw std::logic_error("GUI.Forms rejects recursive semantic snapshots");
    }
    in_semantic_snapshot_ = true;
    struct Reset final {
        bool& value;
        ~Reset() { value = false; }
    } reset{in_semantic_snapshot_};

    for (std::uint32_t pass = 0U;
         pass < maximum_semantic_snapshot_passes; ++pass) {
        ensure_layout(true);
        const std::uint64_t generation = semantic_generation_;
        SemanticSnapshot snapshot;
        snapshot.generation = generation;
        const Control::Ptr focused = focused_.lock();
        const Control::Ptr scoped = focus_allowed_by_active_scope(root_) ? Control::Ptr{} : active_focus_scope_root();
        std::vector<Control::Ptr> roots{scoped ? scoped : root_};
        roots.reserve(1U + popups_.size());
        for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
            if (!scoped || contains_control(scoped, (*popup).owner())) { roots.push_back((*popup).popup()); }
        }
        for (const Control::Ptr& root : roots) {
            if (!root || !(*root).is_alive() || (*root).surface_root_ != this ||
                (root != scoped && (*root).parent())) {
                continue;
            }
            // Focus held by unexposed controls at the top of the tree has no
            // node above it to belong to.
            HiddenFocus unowned;
            append_semantic_nodes(root, root == scoped ? (*root).parent().get() : nullptr, this, focused, snapshot.roots,
                                  snapshot.node_count, unowned);
        }
        if (generation == semantic_generation_) {
            return snapshot;
        }
        metrics_.record_callback_arbitration_retry(
            pass + 1U == maximum_semantic_snapshot_passes);
    }
    throw std::runtime_error(
        "GUI.Forms semantic snapshot did not converge within four passes");
}

bool SurfaceRoot::perform_semantic_action(std::string_view stable_id,
                                     SemanticAction action,
                                     std::string_view value) {
    require_root_thread("semantic action");
    if (in_paint_) {
        return defer_input(DeferredInput{DeferredSemanticInput{
            std::string(stable_id), action, std::string(value)}});
    }
    const Control::Ptr control = find(stable_id);
    const Control::Ptr scoped = focus_allowed_by_active_scope(root_) ? Control::Ptr{} : active_focus_scope_root();
    if (!control) {
        if (dispatch_semantic_child_action(scoped ? scoped : root_, stable_id, action, value)) {
            return true;
        }
        ControlList popup_roots;
        popup_roots.reserve(popups_.size());
        for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
            if (!scoped || contains_control(scoped, (*popup).owner())) { popup_roots.push_back((*popup).popup()); }
        }
        for (ControlList::reverse_iterator popup = popup_roots.rbegin();
             popup != popup_roots.rend();
             ++popup) {
            if (dispatch_semantic_child_action(
                    *popup, stable_id, action, value)) return true;
        }
        return false;
    }
    if (!eligible(control)) return false;
    if (!focus_allowed_by_active_scope(control)) {
        bool closes_active_popup = false;
        for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
            if (action == SemanticAction::collapse && (*popup).owner() == control &&
                contains_control((*popup).popup(), scoped)) { closes_active_popup = true; break; }
        }
        if (!closes_active_popup) return false;
    }
    if (action == SemanticAction::focus) {
        set_focus_cue_visible(true);
        if ((*control).focusable_) {
            const bool focused = request_focus(control);
            return focused;
        }
        // A control that cannot take focus itself, such as a composite over
        // an inner editor, decides in its own on_semantic_action where focus
        // goes. The action is performed only if focus then lies in the
        // control's subtree, so focus is still refused where nothing there
        // can take it.
        const bool handled = (*control).on_semantic_action(action, value);
        if (!handled || !(*control).is_alive() || (*control).surface_root_ != this) return false;
        const Control::Ptr now_focused = focused_.lock();
        const bool inside = contains_control(control, now_focused);
        return inside;
    }
    return (*control).on_semantic_action(action, value);
}

bool SurfaceRoot::perform_semantic_action(RuntimeId control_id,
                                     std::string_view virtual_child,
                                     SemanticAction action,
                                     std::string_view value) {
    require_root_thread("semantic action");
    if (in_paint_) {
        return defer_input(DeferredInput{DeferredSemanticInput{
            std::string(virtual_child), action, std::string(value), control_id}});
    }
    const Control::Ptr control = find_runtime_control(control_id);
    if (!control) return false;
    if (virtual_child.empty()) {
        // Stable identities are unique within a window, so this reaches
        // `control` itself; a replaced control's successor is never found by
        // the old runtime identity.
        const std::string_view stable_id = (*control).stable_id().value();
        if (find(stable_id) != control) return false;
        const bool performed = perform_semantic_action(stable_id, action, value);
        return performed;
    }
    // The virtual child is published by `control` or, when the semantic tree
    // does not expose the control that publishes it, by a control inside it.
    if (!semantic_scope_reaches(control)) return false;
    const bool claimed = dispatch_semantic_child_action(control, virtual_child, action, value);
    return claimed;
}

SemanticTextResult SurfaceRoot::semantic_text_layout(RuntimeId control_id, SemanticTextLayout& layout) {
    require_root_thread("semantic text layout");
    layout.clear();
    if (in_paint_) return SemanticTextResult::during_paint;
    const Control::Ptr control = find_runtime_control(control_id);
    if (!control) return SemanticTextResult::stale_identity;
    const SemanticTextResult result = (*control).on_semantic_text_layout(layout);
    if (result != SemanticTextResult::done) layout.clear();
    return result;
}

SemanticTextResult SurfaceRoot::set_semantic_text_selection(RuntimeId control_id, SemanticTextSelection selection) {
    require_root_thread("semantic text selection");
    if (in_paint_) return SemanticTextResult::during_paint;
    const Control::Ptr control = find_runtime_control(control_id);
    if (!control) return SemanticTextResult::stale_identity;
    if (!semantic_scope_reaches(control)) return SemanticTextResult::outside_focus_scope;
    const SemanticTextResult result = (*control).on_set_semantic_text_selection(selection);
    return result;
}

Control::Ptr SurfaceRoot::find_runtime_control(RuntimeId control) const {
    ControlList pending{root_};
    pending.reserve(1U + popups_.size());
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        pending.push_back((*popup).popup());
    }
    while (!pending.empty()) {
        const Control::Ptr current = pending.back();
        pending.pop_back();
        if (!current || !(*current).is_alive() || (*current).surface_root_ != this) continue;
        if ((*current).runtime_id() == control) return current;
        const std::span<const Control::Ptr> children = (*current).children();
        for (std::size_t index = 0; index < children.size(); ++index) {
            pending.push_back(children[index]);
        }
    }
    return {};
}

bool SurfaceRoot::semantic_scope_reaches(const Control::Ptr& control) const {
    const Control::Ptr scoped = focus_allowed_by_active_scope(root_) ? Control::Ptr{} : active_focus_scope_root();
    if (!scoped || contains_control(scoped, control)) return true;
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (contains_control(scoped, (*popup).owner()) && contains_control((*popup).popup(), control)) return true;
    }
    return false;
}

void SurfaceRoot::apply_subtree_dirty(Control& control, Dirty requested_dirty,
                                 double& requested_damage_area) {
    control.add_dirty(requested_dirty);
    control.subtree_dirty_ |= requested_dirty;
    if (has_dirty(requested_dirty, Dirty::layout) &&
        control.layout_suspend_depth_ != 0U) {
        control.layout_deferred_ = true;
        ++control.layout_requested_revision_;
        if (control.layout_requested_revision_ == 0U) {
            ++control.layout_requested_revision_;
        }
    }
    if (has_dirty(requested_dirty, Dirty::paint)) {
        Rect bounds = paint_damage_bounds_of(control);
        if (bounds.empty()) {
            bounds = control.requested_bounds_;
        }
        // Structural invalidation recomposes every plane so vacated pixels
        // cannot survive beneath a transparent or detached control.
        add_damage_all_planes(bounds);
        requested_damage_area += bounds.area();
    }
    for (const Control::Ptr& child : control.children_) {
        apply_subtree_dirty(*child, requested_dirty, requested_damage_area);
    }
}

void SurfaceRoot::mark_subtree_dirty(Control& control, Dirty requested_dirty) {
    require_root_thread("subtree dirty mutation");
    if (has_dirty(requested_dirty, Dirty::measure)) {
        requested_dirty |= Dirty::arrange;
    }
    double requested_damage_area = 0.0;
    apply_subtree_dirty(control, requested_dirty, requested_damage_area);
    // The subtree's root stands for the subtree in the table.
    control_table_.touch(control.surface_slot_, requested_dirty);
    for (Control::Ptr ancestor = control.parent(); ancestor;
         ancestor = (*ancestor).parent()) {
        (*ancestor).subtree_dirty_ |= requested_dirty;
        if (has_dirty(requested_dirty, Dirty::layout) &&
            (*ancestor).layout_suspend_depth_ != 0U) {
            (*ancestor).layout_deferred_ = true;
            ++(*ancestor).layout_requested_revision_;
            if ((*ancestor).layout_requested_revision_ == 0U) {
                ++(*ancestor).layout_requested_revision_;
            }
        }
    }
    if (has_dirty(requested_dirty, Dirty::layout)) {
        mark_layout_ancestors(control);
    }
    // Subtree mutation has the same semantic-generation contract as a
    // single-control mutation. Cached native accessibility trees must retire
    // hidden descendants even when their arranged geometry is unchanged.
    if (has_dirty(requested_dirty, Dirty::semantics)) {
        ++semantic_generation_;
        if (semantic_generation_ == 0U) ++semantic_generation_;
    }
    metrics_.record_mutation();
    if (has_dirty(requested_dirty, Dirty::layout)) {
        layout_dirty_ = true;
        if (in_layout_) {
            second_layout_pass_requested_ = true;
        }
    }
    if (has_dirty(requested_dirty, Dirty::hit_test)) {
        hit_test_dirty_ = true;
    }
    if (has_dirty(requested_dirty, Dirty::paint)) {
        touch_paint();
        metrics_.record_dirty_mark(requested_damage_area);
    } else {
        metrics_.record_dirty_mark(0.0);
    }
}

void SurfaceRoot::change_paint_plane(Control& control, PaintPlane plane) {
    require_root_thread("paint-plane mutation");
    const Rect bounds = paint_damage_bounds_of(control);
    add_damage_all_planes(bounds);
    control.paint_plane_ = plane;
    control.display_chunk_.reset();
    mark_dirty(control, invalidation::paint_only);
    update_display_cache_metrics();
}

void SurfaceRoot::add_damage(Rect damage, PaintPlane plane) {
    // Damage outside the client area can never be painted. Recording it
    // would also keep the plane pending forever, because a paint retires a
    // plane only when its painted bounds contain the pending bounds.
    damage = Rect::intersection(damage, {0.0, 0.0, client_size_.width, client_size_.height});
    if (damage.empty()) return;
    DamageRegion& region = plane_damage_[paint_plane_index(plane)];
    const std::uint64_t compactions = region.compaction_count();
    const std::uint64_t collapses = region.collapse_count();
    region.add(damage);
    metrics_.record_damage_region(
        region.rectangle_count(), region.compaction_count() - compactions,
        region.collapse_count() - collapses);
    // Glass over the damaged backplane shows it changed (E9-B).
    if (plane == PaintPlane::backplane) damage_backplane_readers(&damage);
}

void SurfaceRoot::add_damage_all_planes(Rect damage) {
    for (std::size_t index = 0; index < paint_plane_count; ++index) {
        add_damage(damage, static_cast<PaintPlane>(index));
    }
}

void SurfaceRoot::add_resize_strip_damage(Rect before, Rect after, double band) {
    // A moving edge's band is redrawn on both sides of the move: the old
    // edge's border inside the larger extent, and the new edge's. The band
    // starts one logical unit further in, for anti-aliasing at a fractional
    // scale.
    const double reach = band > 0.0 ? std::ceil(band) + 1.0 : 0.0;
    const bool width_changed = before.width != after.width;
    const bool height_changed = before.height != after.height;
    const double inner_right = std::min(before.x + before.width, after.x + after.width);
    const double inner_bottom = std::min(before.y + before.height, after.y + after.height);
    const double shared_right = width_changed ? std::max(before.x, inner_right - reach) : inner_right;
    const double shared_bottom = height_changed ? std::max(before.y, inner_bottom - reach) : inner_bottom;
    const double outer_right = std::max(before.x + before.width, after.x + after.width);
    const double outer_bottom = std::max(before.y + before.height, after.y + after.height);
    // The strip right of the shared extent, full height, then the one
    // below it, shared width: together the larger extent minus the smaller.
    const Rect right{shared_right, before.y, outer_right - shared_right, outer_bottom - before.y};
    const Rect below{before.x, shared_bottom, shared_right - before.x, outer_bottom - shared_bottom};
    if (!right.empty()) add_damage_all_planes(right);
    if (!below.empty()) add_damage_all_planes(below);
}

bool SurfaceRoot::keeps_pixels_on_resize(const Control& control) const noexcept {
    if (control.paint_depends_on_size()) return false;
    if (!(control.effective_visual_outsets() == Insets{})) return false;
    if (!(control.last_painted_visual_outsets_ == Insets{})) return false;
    if (!control.owned_decorations_.empty()) return false;
    const bool surface_invariant = !control.authored_surface_material_ ||
                                   surface_material_size_invariant(*control.authored_surface_material_);
    return surface_invariant;
}

void SurfaceRoot::request_paint_wake() noexcept {
    if (paint_wake_pending_) {
        ++paint_wakes_coalesced_;
        return;
    }
    if (occluded_ || !paint_wake_handler_) return;
    paint_wake_pending_ = true;
    try {
        paint_wake_handler_();
        ++paint_wakes_queued_;
    } catch (...) {
        // Host wake seams are advisory and must never make a retained model
        // mutation fail. Clearing the coalescing bit permits a later mutation
        // or exposure transition to retry.
        paint_wake_pending_ = false;
    }
}

void SurfaceRoot::acknowledge_paint_wake_if_damage_drained() noexcept {
    bool damage_remains = false;
    for (const DamageRegion& damage : plane_damage_) {
        if (!damage.empty()) {
            damage_remains = true;
            break;
        }
    }
    if (!damage_remains) paint_wake_pending_ = false;
}

void SurfaceRoot::touch_paint() noexcept {
    ++content_revision_;
    if (content_revision_ == 0U) ++content_revision_;
    if (in_paint_) dirty_after_render_ = true;
    request_paint_wake();
}

PaintLeaseState SurfaceRoot::paint_lease_state() const noexcept {
    if (in_paint_) {
        const PaintLeaseState rendering = dirty_after_render_
            ? PaintLeaseState::rendering_dirty
            : PaintLeaseState::rendering;
        return rendering;
    }
    const bool owed = paint_pending();
    if (owed && occluded_) return PaintLeaseState::occluded_dirty;
    if (owed) return PaintLeaseState::dirty_queued;
    // Nothing owed. A content revision past the rendered one changed nothing
    // a paint can reach (a control outside the client area), so it is not
    // queued; what remains is whether the last render has been presented.
    if (presented_revision_ < rendered_revision_) return PaintLeaseState::ready;
    return PaintLeaseState::clean;
}

void SurfaceRoot::add_subtree_damage(const Control::Ptr& control) {
    add_damage_all_planes(paint_damage_bounds_of(*control));
    for (const std::shared_ptr<gui_forms::Control>& child : (*control).children_) {
        add_subtree_damage(child);
    }
}

void SurfaceRoot::settle_dirty() {
    // The bookkeeping of a touch (ancestor marks, damage, generations) is
    // still done at the write; settling drains the table's bits so that they
    // name the controls touched since the previous read.
    for (std::size_t kind = 0U; kind < detail::dirty_kind_count; ++kind) {
        settle_indices_.clear();
        static_cast<void>(control_table_.take(static_cast<detail::DirtyKind>(kind), settle_indices_));
    }
    settle_indices_.clear();
}

void SurfaceRoot::ensure_layout(bool read_barrier) {
    require_root_thread("layout");
    settle_dirty();
    if (!(*root_).is_alive()) {
        layout_dirty_ = false;
        hit_test_dirty_ = false;
        return;
    }
    if (!layout_dirty_) {
        return;
    }
    if (read_barrier && update_depth_ > 0) {
        return;
    }
    if (in_layout_) {
        second_layout_pass_requested_ = true;
        return;
    }

    if (!has_any_runnable_layout_dirty()) return;

    metrics_.record_flush(read_barrier);
    const ShadowFrameScope shadow_frame(ShadowFrameKind::layout_pass, (*root_).stable_id().value(), std::string_view{},
                                        layout_pass_sequence_ + 1U);
    in_layout_ = true;
    try {
        std::uint32_t pass = 0;
        do {
            ++layout_pass_sequence_;
            second_layout_pass_requested_ = false;
            std::uint64_t measure_visited = 0;
            std::uint64_t measured = 0;
            measure_dirty_recursive(root_, client_size_, measure_visited, measured);
            for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
                if (const Control::Ptr overlay = (*popup).popup()) {
                    measure_dirty_recursive(overlay, client_size_, measure_visited,
                                            measured);
                }
            }
            metrics_.record_measure(measure_visited, measured);

            std::uint64_t arrange_visited = 0;
            std::uint64_t arranged = 0;
            arrange_dirty_recursive(
                root_, {0.0, 0.0, client_size_.width, client_size_.height},
                arrange_visited, arranged);
            // A tooltip below a control follows it before the popups are
            // arranged.
            if (tool_tip_presenter_) (*tool_tip_presenter_).root_arranged();
            for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
                if (const Control::Ptr overlay = (*popup).popup()) {
                    arrange_dirty_recursive(overlay, (*overlay).requested_bounds_,
                                            arrange_visited, arranged);
                }
            }
            metrics_.record_arrange(arrange_visited, arranged);

            static_cast<void>(recompute_subtree_dirty(root_));
            for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
                if (const Control::Ptr overlay = (*popup).popup()) {
                    static_cast<void>(recompute_subtree_dirty(overlay));
                }
            }
            commit_layout_requests_recursive(root_);
            for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
                commit_layout_requests_recursive((*popup).popup());
            }
            ++pass;
        } while (has_any_runnable_layout_dirty() &&
                 pass < maximum_layout_passes);
        ++layout_pass_sequence_;

        const bool popup_layout_dirty = has_popup_layout_dirty();
        if (has_any_runnable_layout_dirty()) {
            metrics_.record_pass_limit_hit();
        }
        layout_dirty_ = has_dirty((*root_).subtree_dirty_, Dirty::layout) ||
                        popup_layout_dirty;
        hit_test_dirty_ = layout_dirty_;
        in_layout_ = false;
    } catch (...) {
        // A user layout callback may fail. Preserve dirty state for an
        // explicit later retry and always release the re-entry guard.
        static_cast<void>(recompute_subtree_dirty(root_));
        for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
            if (const Control::Ptr overlay = (*popup).popup()) {
                static_cast<void>(recompute_subtree_dirty(overlay));
            }
        }
        layout_dirty_ = has_dirty((*root_).subtree_dirty_, Dirty::layout) ||
                        has_popup_layout_dirty();
        hit_test_dirty_ = layout_dirty_;
        second_layout_pass_requested_ = false;
        in_layout_ = false;
        throw;
    }
}


std::vector<Control::Ptr> SurfaceRoot::route_to(const Control::Ptr& target) const {
    std::vector<Control::Ptr> route;
    for (Control::Ptr current = target; current; current = (*current).parent()) {
        route.push_back(current);
    }
    std::reverse(route.begin(), route.end());
    return route;
}

Control::Ptr SurfaceRoot::hit_test_recursive(const Control::Ptr& control,
                                        Point window_position) const {
    if (!eligible(control) || (*control).surface_root_ != this) {
        return {};
    }
    Control* const retained_parent = (*control).parent_.lock().get();
    const Rect bounds = absolute_bounds_of(*control);
    if (!bounds.contains(window_position)) {
        return {};
    }
    const Rect local_child_viewport = (*control).child_viewport_rectangle();
    if (!(*control).is_alive() || (*control).surface_root_ != this ||
        (*control).parent_.lock().get() != retained_parent) {
        return {};
    }
    const Rect child_viewport{bounds.x + local_child_viewport.x,
                              bounds.y + local_child_viewport.y,
                              local_child_viewport.width,
                              local_child_viewport.height};
    if (child_viewport.contains(window_position)) {
        const ControlList children = (*control).children_;
        for (ControlList::const_reverse_iterator child = children.rbegin();
             child != children.rend(); ++child) {
            if (!*child || !(*(*child)).is_alive() ||
                (*(*child)).parent_.lock().get() != control.get() ||
                (*(*child)).surface_root_ != this) {
                continue;
            }
            if (Control::Ptr target = hit_test_recursive(*child, window_position)) {
                if (!(*target).is_alive() || (*target).surface_root_ != this) continue;
                return target;
            }
        }
    }
    const Point local{window_position.x - bounds.x, window_position.y - bounds.y};
    const bool hit = !(*control).hit_test_transparent_ &&
                     (*control).hit_test_local(local);
    return hit && eligible(control) && (*control).surface_root_ == this &&
            (*control).parent_.lock().get() == retained_parent
        ? control : Control::Ptr{};
}

void SurfaceRoot::measure_dirty_recursive(const Control::Ptr& control,
                                     Size available,
                                     std::uint64_t& visited_nodes,
                                     std::uint64_t& callbacks) {
    if (!control || (*control).layout_suspend_depth_ != 0U ||
        !has_dirty((*control).subtree_dirty_, Dirty::measure)) {
        return;
    }
    ++visited_nodes;
    if (!(*control).is_alive() || !(*control).visible_ ||
        (*control).layout_collapsed_) {
        clear_layout_dirty_subtree(control);
        return;
    }

    const Size child_available{
        (*control).requested_bounds_.width > 0.0 ? (*control).requested_bounds_.width
                                               : available.width,
        (*control).requested_bounds_.height > 0.0 ? (*control).requested_bounds_.height
                                                : available.height};
    const std::vector<Control::Ptr> retained = (*control).children_;
    for (const std::shared_ptr<gui_forms::Control>& child : retained) {
        if (!child || !(*child).is_alive() ||
            (*child).parent_.lock().get() != control.get() ||
            (*child).surface_root_ != this) {
            continue;
        }
        measure_dirty_recursive(child, child_available, visited_nodes, callbacks);
    }
    if (!(*control).is_alive() || (*control).surface_root_ != this) return;
    if (has_dirty((*control).dirty_, Dirty::measure)) {
        (*control).clear_dirty(Dirty::measure);
        // A parent that asks its children its own questions while arranging
        // measures this control there; the dirtiness already emptied its
        // cache, so nothing stale is answered.
        const Control::Ptr parent = (*control).parent_.lock();
        if (parent && (*parent).measures_children_in_arrange()) return;
        try {
            // Kept in the control's measure cache, so its parent's arrange
            // asking the same question is answered without a callback. A
            // client size of infinity is admitted by resize but is no
            // layout constraint; it is measured uncached, as before.
            const LayoutConstraint constraint = layout_constraint_at_most(available);
            if (valid_layout_constraint(constraint)) {
                static_cast<void>((*control).measure_for_layout(constraint));
            } else {
                static_cast<void>((*control).measure(available));
            }
        } catch (...) {
            (*control).add_dirty(Dirty::measure | Dirty::arrange);
            (*control).subtree_dirty_ |= Dirty::measure | Dirty::arrange;
            throw;
        }
        ++callbacks;
        if (!(*control).is_alive() || (*control).surface_root_ != this) return;
    }
}

void SurfaceRoot::arrange_dirty_recursive(const Control::Ptr& control,
                                     Rect final_bounds,
                                     std::uint64_t& visited_nodes,
                                     std::uint64_t& callbacks) {
    if (!control || (*control).layout_suspend_depth_ != 0U ||
        !has_dirty((*control).subtree_dirty_, Dirty::arrange)) {
        return;
    }
    ++visited_nodes;
    if (!(*control).is_alive() || !(*control).visible_ ||
        (*control).layout_collapsed_) {
        clear_layout_dirty_subtree(control);
        return;
    }

    if (has_dirty((*control).dirty_, Dirty::arrange)) {
        const Rect old_bounds = visual_bounds_of(
            *control, (*control).last_painted_visual_outsets_);
        const Rect old_arranged = (*control).arranged_bounds_;
        (*control).clear_dirty(Dirty::arrange | Dirty::hit_test);
        try {
            (*control).arrange(final_bounds);
        } catch (...) {
            (*control).dirty_ |= Dirty::arrange | Dirty::hit_test;
            (*control).subtree_dirty_ |= Dirty::arrange | Dirty::hit_test;
            throw;
        }
        ++callbacks;
        if (!(*control).is_alive() || (*control).surface_root_ != this) return;
        // The containers below whose absolutely positioned children were
        // placed against this control's box arrange in this same descent.
        if (!containing_block_watches_.empty() && (*control).arranged_bounds_ != old_arranged) {
            mark_containing_block_dependents(*control);
        }
        const Rect new_bounds = visual_bounds_of(
            *control, (*control).effective_visual_outsets());
        if (old_bounds != new_bounds) {
            // Arrangement changes expose content below the moved control. The
            // compositor uses one target across ordered paint planes, so both
            // the vacated and occupied rectangles need full recomposition,
            // except where a control that kept its origin keeps its pixels
            // (keeps_pixels_on_resize): there only the strips between the
            // two extents change. Its chunk is recorded again at the new
            // size either way (paint_recursive).
            const bool same_origin = old_bounds.x == new_bounds.x && old_bounds.y == new_bounds.y;
            if (same_origin && keeps_pixels_on_resize(*control)) {
                add_resize_strip_damage(old_bounds, new_bounds, 0.0);
            } else {
                add_damage_all_planes(old_bounds);
                add_damage_all_planes(new_bounds);
            }
            touch_paint();
            (*control).publish_change((*control).arranged_bounds_changed_,
                                    new_bounds);
        }
    }
    const std::vector<Control::Ptr> retained = (*control).children_;
    for (const std::shared_ptr<gui_forms::Control>& child : retained) {
        if (!child || !(*child).is_alive() ||
            (*child).parent_.lock().get() != control.get() ||
            (*child).surface_root_ != this) {
            continue;
        }
        arrange_dirty_recursive(
            child, (*child).layout_slot_.value_or((*child).requested_bounds_),
            visited_nodes, callbacks);
    }
}

bool SurfaceRoot::has_runnable_layout_dirty(
    const Control::Ptr& control) const noexcept {
    if (!control || (*control).layout_suspend_depth_ != 0U ||
        !has_dirty((*control).subtree_dirty_, Dirty::layout)) {
        return false;
    }
    if (has_dirty((*control).dirty_, Dirty::layout)) return true;
    for (const Control::Ptr& child : (*control).children_) {
        if (has_runnable_layout_dirty(child)) return true;
    }
    return false;
}

bool SurfaceRoot::has_any_runnable_layout_dirty() const noexcept {
    if (has_runnable_layout_dirty(root_)) return true;
    for (const std::shared_ptr<detail::PopupAttachment>& popup : popups_) {
        if (has_runnable_layout_dirty((*popup).popup())) return true;
    }
    return false;
}

bool SurfaceRoot::has_popup_layout_dirty() const noexcept {
    for (const std::shared_ptr<detail::PopupAttachment>& popup : popups_) {
        const Control::Ptr overlay = (*popup).popup();
        if (overlay && has_dirty((*overlay).subtree_dirty_, Dirty::layout)) {
            return true;
        }
    }
    return false;
}

void SurfaceRoot::mark_layout_ancestors(Control& control) noexcept {
    Control::Ptr ancestor = control.parent();
    while (ancestor) {
        Control::Ptr placing_parent = (*ancestor).parent();
        const bool size_can_change =
            (*ancestor).size_depends_on_content() ||
            (placing_parent && (*placing_parent).sizes_children_from_content());
        if (!size_can_change) {
            (*ancestor).add_dirty(Dirty::arrange);
            (*ancestor).subtree_dirty_ |= Dirty::arrange;
            metrics_.record_layout_boundary_stop();
            return;
        }
        (*ancestor).add_dirty(Dirty::measure | Dirty::arrange);
        (*ancestor).subtree_dirty_ |= Dirty::measure | Dirty::arrange;
        ancestor = std::move(placing_parent);
    }
}

void SurfaceRoot::clear_layout_diagnostics() noexcept {
    layout_diagnostics_.clear();
    dropped_layout_diagnostics_ = 0U;
}

void SurfaceRoot::record_layout_diagnostic(LayoutDiagnostic diagnostic) {
    if (layout_diagnostics_.size() >= maximum_layout_diagnostics) {
        ++dropped_layout_diagnostics_;
        return;
    }
    layout_diagnostics_.push_back(std::move(diagnostic));
}

void SurfaceRoot::note_containing_block(const Control& child, const Control& ancestor) {
    const RuntimeId resolved = ancestor.runtime_id();
    const std::uint64_t key = child.runtime_id().value;
    const std::unordered_map<std::uint64_t, RuntimeId>::iterator found = containing_block_resolutions_.find(key);
    if (found != containing_block_resolutions_.end()) {
        if ((*found).second == resolved) return;
        (*found).second = resolved;
    } else {
        containing_block_resolutions_.emplace(key, resolved);
    }
    const Control::Ptr parent = child.parent();
    if (parent.get() == &ancestor) return;
    LayoutDiagnostic diagnostic{};
    diagnostic.code = LayoutDiagnosticCode::containing_block_not_parent;
    diagnostic.severity = layout_diagnostic_severity(diagnostic.code);
    diagnostic.child = std::string(child.stable_id().value());
    diagnostic.ancestor = std::string(ancestor.stable_id().value());
    record_layout_diagnostic(std::move(diagnostic));
}

std::size_t SurfaceRoot::containing_block_dependent_count(RuntimeId watched) const noexcept {
    const std::unordered_map<std::uint64_t, ContainingBlockWatch>::const_iterator found =
        containing_block_watches_.find(watched.value);
    if (found == containing_block_watches_.end()) return 0U;
    return (*found).second.dependents.size();
}

void SurfaceRoot::withdraw_containing_block_dependent(RuntimeId watched, RuntimeId dependent) noexcept {
    const std::unordered_map<std::uint64_t, ContainingBlockWatch>::iterator watch =
        containing_block_watches_.find(watched.value);
    if (watch == containing_block_watches_.end()) return;
    std::vector<ContainingBlockDependent>& dependents = (*watch).second.dependents;
    for (std::size_t index = 0U; index < dependents.size(); ++index) {
        if (dependents[index].id != dependent) continue;
        dependents[index] = std::move(dependents.back());
        dependents.pop_back();
        break;
    }
    if (dependents.empty() && !(*watch).second.overflowed) containing_block_watches_.erase(watch);
}

void SurfaceRoot::set_containing_block_dependencies(Control& dependent, std::span<const RuntimeId> watched) {
    const RuntimeId id = dependent.runtime_id();
    const std::unordered_map<std::uint64_t, std::vector<RuntimeId>>::iterator found =
        containing_block_paths_.find(id.value);
    if (found == containing_block_paths_.end() && watched.empty()) return;
    if (found != containing_block_paths_.end()) {
        const std::vector<RuntimeId>& registered = (*found).second;
        if (std::equal(registered.begin(), registered.end(), watched.begin(), watched.end())) return;
        for (const RuntimeId& old : registered) withdraw_containing_block_dependent(old, id);
        if (watched.empty()) {
            containing_block_paths_.erase(found);
            return;
        }
    }
    std::vector<RuntimeId>& path = containing_block_paths_[id.value];
    path.assign(watched.begin(), watched.end());
    const Control::WeakPtr observer = dependent.weak_from_this();
    for (const RuntimeId& watch_id : watched) {
        ContainingBlockWatch& watch = containing_block_watches_[watch_id.value];
        if (watch.overflowed) continue;
        if (watch.dependents.size() >= maximum_containing_block_dependents) {
            watch.overflowed = true;
            continue;
        }
        watch.dependents.push_back(ContainingBlockDependent{id, observer});
    }
}

void SurfaceRoot::mark_subtree_arrange(Control& control) noexcept {
    constexpr Dirty effects = Dirty::arrange | Dirty::hit_test;
    control.dirty_ |= effects;
    control.subtree_dirty_ |= effects;
    for (const Control::Ptr& child : control.children_) {
        if (child) mark_subtree_arrange(*child);
    }
}

void SurfaceRoot::mark_containing_block_dependents(Control& watched) {
    const std::unordered_map<std::uint64_t, ContainingBlockWatch>::const_iterator found =
        containing_block_watches_.find(watched.runtime_id().value);
    if (found == containing_block_watches_.end()) return;
    constexpr Dirty effects = Dirty::arrange | Dirty::hit_test;
    const ContainingBlockWatch& watch = (*found).second;
    if (watch.overflowed) {
        // Below the watched control only: it has arranged already.
        for (const Control::Ptr& child : watched.children_) {
            if (child) mark_subtree_arrange(*child);
        }
        ++containing_block_subtree_arranges_;
    }
    // Each dependent is below the watched control: its own mark, and the
    // subtree marks from it up to the root, let the descent reach it.
    for (const ContainingBlockDependent& entry : watch.dependents) {
        const Control::Ptr dependent = entry.control.lock();
        if (!dependent || !(*dependent).is_alive() || (*dependent).surface_root_ != this) continue;
        (*dependent).dirty_ |= effects;
        (*dependent).subtree_dirty_ |= effects;
        note_suspended_layout_request(*dependent);
        for (Control::Ptr ancestor = (*dependent).parent(); ancestor && ancestor.get() != &watched;
             ancestor = (*ancestor).parent()) {
            (*ancestor).subtree_dirty_ |= effects;
        }
    }
    watched.subtree_dirty_ |= effects;
    for (Control::Ptr ancestor = watched.parent(); ancestor; ancestor = (*ancestor).parent()) {
        (*ancestor).subtree_dirty_ |= effects;
    }
    layout_dirty_ = true;
    hit_test_dirty_ = true;
}

void SurfaceRoot::erase_containing_block_entries(RuntimeId control) noexcept {
    if (containing_block_resolutions_.empty() && containing_block_paths_.empty() &&
        containing_block_watches_.empty()) {
        return;
    }
    containing_block_resolutions_.erase(control.value);
    containing_block_watches_.erase(control.value);
    const std::unordered_map<std::uint64_t, std::vector<RuntimeId>>::iterator found =
        containing_block_paths_.find(control.value);
    if (found == containing_block_paths_.end()) return;
    for (const RuntimeId& watched : (*found).second) withdraw_containing_block_dependent(watched, control);
    containing_block_paths_.erase(found);
}

void SurfaceRoot::note_suspended_layout_request(Control& control) noexcept {
    for (Control* current = &control; current != nullptr;) {
        if ((*current).layout_suspend_depth_ != 0U) {
            (*current).layout_deferred_ = true;
            ++(*current).layout_requested_revision_;
            if ((*current).layout_requested_revision_ == 0U) {
                ++(*current).layout_requested_revision_;
            }
        }
        const Control::Ptr parent = (*current).parent_.lock();
        current = parent.get();
    }
}

void SurfaceRoot::commit_layout_requests_recursive(
    const Control::Ptr& control) noexcept {
    if (!control || (*control).layout_suspend_depth_ != 0U) return;
    for (const std::shared_ptr<gui_forms::Control>& child : (*control).children_) {
        commit_layout_requests_recursive(child);
    }
    if (!has_dirty((*control).subtree_dirty_, Dirty::layout) &&
        (*control).layout_deferred_) {
        (*control).layout_committed_revision_ =
            (*control).layout_requested_revision_;
        (*control).layout_deferred_ = false;
    }
}

Dirty SurfaceRoot::recompute_subtree_dirty(const Control::Ptr& control) noexcept {
    Dirty summary = (*control).dirty_;
    for (const std::shared_ptr<gui_forms::Control>& child : (*control).children_) {
        summary |= recompute_subtree_dirty(child);
    }
    (*control).subtree_dirty_ = summary;
    return summary;
}

void SurfaceRoot::clear_layout_dirty_subtree(const Control::Ptr& control) noexcept {
    (*control).clear_dirty(Dirty::layout | Dirty::hit_test);
    (*control).clear_subtree_dirty(Dirty::layout | Dirty::hit_test);
    for (const std::shared_ptr<gui_forms::Control>& child : (*control).children_) {
        clear_layout_dirty_subtree(child);
    }
}

void SurfaceRoot::clear_paint_dirty_subtree(const Control::Ptr& control) noexcept {
    (*control).clear_dirty(Dirty::paint);
    (*control).clear_subtree_dirty(Dirty::paint);
    for (const std::shared_ptr<gui_forms::Control>& child : (*control).children_) {
        clear_paint_dirty_subtree(child);
    }
}

bool SurfaceRoot::find_paint_cover(const Control::Ptr& control, const Rect damage, const PaintPlane plane,
                              std::vector<Control::Ptr>& path) const {
    if (!(*control).is_alive() || (*control).surface_root_ != this || !(*control).visible_ ||
        (*control).layout_collapsed_) {
        return false;
    }
    // A child paints all of `damage` only inside the child viewport. Later
    // children paint later, and a control's subtree after the control.
    if (absolute_child_viewport_of(*control).contains(damage)) {
        const std::vector<Control::Ptr>& children = (*control).children_;
        for (std::size_t index = children.size(); index > 0U; --index) {
            const Control::Ptr& child = children[index - 1U];
            if (!child || !(*child).is_alive() || (*child).parent_.lock().get() != control.get() ||
                (*child).surface_root_ != this) {
                continue;
            }
            const std::size_t depth = path.size();
            if (find_paint_cover(child, damage, plane, path)) {
                path.insert(path.begin() + static_cast<std::ptrdiff_t>(depth), control);
                return true;
            }
        }
    }
    // Only a current chunk: one paint_recursive replays without rebuilding.
    const Rect bounds = absolute_bounds_of(*control);
    const Rect logical_bounds{0.0, 0.0, bounds.width, bounds.height};
    const std::shared_ptr<const detail::DisplayChunk>& chunk = (*control).display_chunk_;
    if ((*control).paint_plane_ != plane || has_dirty((*control).dirty_, Dirty::paint) || !chunk ||
        (*chunk).plane() != plane || (*chunk).logical_bounds() != logical_bounds) {
        return false;
    }
    const std::optional<Rect> cover = (*chunk).opaque_cover();
    if (!cover) return false;
    // It paints nothing outside its visual bounds, which hold its bounds.
    const Rect covered = Rect::intersection({bounds.x + (*cover).x, bounds.y + (*cover).y, (*cover).width,
                                             (*cover).height}, bounds);
    if (covered.empty() || !cover_spans(covered, damage, scale_, client_size_)) return false;
    path.push_back(control);
    return true;
}

void SurfaceRoot::paint_from_cover(const std::span<const Control::Ptr> path,
                              Painter& painter,
                              const Rect damage,
                              const PaintPlane plane,
                              std::uint64_t& visited_nodes,
                              std::uint64_t& painted_controls,
                              std::uint64_t& consumed_invalidations,
                              std::uint64_t& chunks_rebuilt,
                              std::uint64_t& chunks_reused,
                              std::uint64_t& commands_replayed) {
    // Each level as paint_recursive(path.front()) reaches it, before any
    // callback runs: the damage it passes down, each ancestor's children
    // and parent.
    std::vector<Rect> level_damage{damage};
    std::vector<std::vector<Control::Ptr>> siblings;
    std::vector<const Control*> retained_parents;
    level_damage.reserve(path.size());
    siblings.reserve(path.size());
    retained_parents.reserve(path.size());
    for (std::size_t level = 0; level + 1U < path.size(); ++level) {
        const Control& ancestor = *path[level];
        level_damage.push_back(Rect::intersection(absolute_child_viewport_of(ancestor), level_damage.back()));
        siblings.push_back(ancestor.children_);
        retained_parents.push_back(ancestor.parent_.lock().get());
    }
    paint_recursive(path.back(), painter, level_damage.back(), plane, visited_nodes, painted_controls,
                    consumed_invalidations, chunks_rebuilt, chunks_reused, commands_replayed);
    // Then what follows on each level up, as each ancestor's loop goes on.
    for (std::size_t level = path.size() - 1U; level > 0U; --level) {
        const Control::Ptr& ancestor = path[level - 1U];
        const std::vector<Control::Ptr>& children = siblings[level - 1U];
        std::vector<Control::Ptr>::const_iterator child = std::find(children.begin(), children.end(), path[level]);
        if (child != children.end()) ++child;
        for (; child != children.end(); ++child) {
            if (!*child || !(**child).is_alive() || (**child).parent_.lock().get() != ancestor.get() ||
                (**child).surface_root_ != this) {
                continue;
            }
            paint_recursive(*child, painter, level_damage[level], plane, visited_nodes, painted_controls,
                            consumed_invalidations, chunks_rebuilt, chunks_reused, commands_replayed);
        }
        if (!(*ancestor).is_alive() || (*ancestor).surface_root_ != this ||
            (*ancestor).parent_.lock().get() != retained_parents[level - 1U]) {
            continue;
        }
        summarize_subtree_paint(*ancestor);
    }
}

Rect SurfaceRoot::absolute_child_viewport_of(const Control& control) const {
    const Rect bounds = absolute_bounds_of(control);
    const Rect local = control.child_viewport_rectangle();
    const Rect viewport{bounds.x + local.x, bounds.y + local.y, local.width, local.height};
    return viewport;
}

void SurfaceRoot::summarize_subtree_paint(Control& control) const noexcept {
    Dirty paint_summary = control.dirty_ & Dirty::paint;
    for (const std::shared_ptr<gui_forms::Control>& child : control.children_) {
        paint_summary |= (*child).subtree_dirty_ & Dirty::paint;
    }
    control.subtree_dirty_ = without_dirty(control.subtree_dirty_, Dirty::paint) | paint_summary;
}

void SurfaceRoot::paint_recursive(const Control::Ptr& control,
                             Painter& painter,
                             Rect window_damage,
                             PaintPlane plane,
                             std::uint64_t& visited_nodes,
                             std::uint64_t& painted_controls,
                             std::uint64_t& consumed_invalidations,
                             std::uint64_t& chunks_rebuilt,
                             std::uint64_t& chunks_reused,
                             std::uint64_t& commands_replayed) {
    ++visited_nodes;
    if (!(*control).is_alive() || (*control).surface_root_ != this ||
        !(*control).visible_ || (*control).layout_collapsed_) {
        clear_paint_dirty_subtree(control);
        return;
    }
    Control* const retained_parent = (*control).parent_.lock().get();
    const Rect bounds = absolute_bounds_of(*control);
    const Insets current_outsets = (*control).effective_visual_outsets();
    if (!(*control).is_alive() || (*control).surface_root_ != this ||
        (*control).parent_.lock().get() != retained_parent) {
        return;
    }
    const Rect visual_bounds = visual_bounds_of(*control, current_outsets);
    const Rect paint_intersection = Rect::intersection(visual_bounds, window_damage);
    const Rect local_child_viewport = (*control).child_viewport_rectangle();
    if (!(*control).is_alive() || (*control).surface_root_ != this ||
        (*control).parent_.lock().get() != retained_parent) {
        return;
    }
    const Rect child_viewport{bounds.x + local_child_viewport.x,
                              bounds.y + local_child_viewport.y,
                              local_child_viewport.width,
                              local_child_viewport.height};
    const Rect child_intersection = Rect::intersection(
        child_viewport, window_damage);
    if (paint_intersection.empty() && child_intersection.empty()) {
        return;
    }

    if ((*control).paint_plane_ == plane && !paint_intersection.empty()) {
        const Rect logical_bounds{0.0, 0.0, bounds.width, bounds.height};
        const bool invalidated = has_dirty((*control).dirty_, Dirty::paint);
        const bool rebuild = invalidated || !(*control).display_chunk_ ||
                             (*(*control).display_chunk_).plane() != plane ||
                             (*(*control).display_chunk_).logical_bounds() != logical_bounds;
        if (rebuild) {
            (*control).clear_dirty(Dirty::paint);
            detail::RecordingPainter recorder(text_metrics_provider_);
            recorder.inherit_caches((*control).display_chunk_);
            // Declared after the recorder it names, so it is destroyed
            // first and the mark never outlives it.
            const RecordingMark mark{recording_control_, recording_painter_, recording_control_,
                                     recording_painter_};
            recording_control_ = control.get();
            recording_painter_ = &recorder;
            (*control).paint_back_color(recorder, logical_bounds);
            (*control).paint_authored_surface(recorder, logical_bounds);
            (*control).paint_owned_decorations(
                recorder, logical_bounds,
                OwnerDecorationLayer::before_content);
            (*control).on_paint(recorder, logical_bounds);
            if (!(*control).is_alive() || (*control).surface_root_ != this ||
                (*control).parent_.lock().get() != retained_parent) {
                return;
            }
            (*control).on_paint_overlay(recorder, logical_bounds);
            if (!(*control).is_alive() || (*control).surface_root_ != this ||
                (*control).parent_.lock().get() != retained_parent) {
                return;
            }
            (*control).paint_owned_decorations(
                recorder, logical_bounds,
                OwnerDecorationLayer::after_content);
            (*control).display_chunk_ = recorder.finish(++display_generation_, plane,
                                                      logical_bounds);
            if (recorder.group_refusals() > 0U) metrics_.record_paint_group_refusals(recorder.group_refusals());
            ++chunks_rebuilt;
            consumed_invalidations += invalidated ? 1U : 0U;
        } else {
            ++chunks_reused;
        }

        painter.save();
        painter.translate({bounds.x, bounds.y});
        const Rect local_damage{paint_intersection.x - bounds.x,
                                paint_intersection.y - bounds.y,
                                paint_intersection.width,
                                paint_intersection.height};
        painter.clip_rect(local_damage);
        commands_replayed +=
        detail::replay_display_chunk(*(*control).display_chunk_, painter);
        painter.restore();
        (*control).last_painted_visual_outsets_ = current_outsets;
        ++painted_controls;
    }

    if (!child_intersection.empty()) {
        const std::vector<Control::Ptr> children = (*control).children_;
        for (const std::shared_ptr<gui_forms::Control>& child : children) {
            if (!child || !(*child).is_alive() ||
                (*child).parent_.lock().get() != control.get() ||
                (*child).surface_root_ != this) {
                continue;
            }
            paint_recursive(child, painter, child_intersection, plane, visited_nodes,
                            painted_controls, consumed_invalidations, chunks_rebuilt,
                            chunks_reused, commands_replayed);
        }
    }

    if (!(*control).is_alive() || (*control).surface_root_ != this ||
        (*control).parent_.lock().get() != retained_parent) {
        return;
    }

    summarize_subtree_paint(*control);
}

std::uint64_t SurfaceRoot::display_cache_entries(const Control::Ptr& control) const noexcept {
    std::uint64_t entries = (*control).display_chunk_ ? 1U : 0U;
    for (const std::shared_ptr<gui_forms::Control>& child : (*control).children_) {
        entries += display_cache_entries(child);
    }
    return entries;
}

void SurfaceRoot::update_display_cache_metrics() noexcept {
    std::uint64_t entries = root_ ? display_cache_entries(root_) : 0U;
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (const Control::Ptr overlay = (*popup).popup()) {
            entries += display_cache_entries(overlay);
        }
    }
    metrics_.set_display_cache(entries, display_generation_);
}

void SurfaceRoot::compact_frame_requests() noexcept {
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        if ((*request).kind == detail::FrameRequestKind::ui_timer ||
            (*request).kind == detail::FrameRequestKind::motion_clock ||
            (*request).kind == detail::FrameRequestKind::engine_idle) {
            continue;
        }
        const std::shared_ptr<gui_forms::Control> target = (*request).target.lock();
        if (!target || !(*target).is_alive() || (*target).surface_root_ != this) {
            (*request).disconnect();
        }
    }
    FrameRequestList::iterator request = frame_requests_.begin();
    while (request != frame_requests_.end()) {
        if (!(*(*request)).connected()) {
            request = frame_requests_.erase(request);
        } else {
            ++request;
        }
    }
}

Rect SurfaceRoot::absolute_bounds_of(const Control& control) const {
    Rect result = control.arranged_bounds_;
    for (Control::Ptr ancestor = control.parent(); ancestor;
         ancestor = (*ancestor).parent()) {
        result.x += (*ancestor).arranged_bounds_.x;
        result.y += (*ancestor).arranged_bounds_.y;
    }
    return result;
}

Rect SurfaceRoot::visual_bounds_of(const Control& control, Insets outsets) const {
    outsets = {bounded_visual_outset(outsets.left),
               bounded_visual_outset(outsets.top),
               bounded_visual_outset(outsets.right),
               bounded_visual_outset(outsets.bottom)};
    const Rect bounds = absolute_bounds_of(control);
    return {bounds.x - outsets.left, bounds.y - outsets.top,
            bounds.width + outsets.left + outsets.right,
            bounds.height + outsets.top + outsets.bottom};
}

Rect SurfaceRoot::paint_damage_bounds_of(const Control& control) const {
    return Rect::united(
        visual_bounds_of(control, control.effective_visual_outsets()),
        visual_bounds_of(control, control.last_painted_visual_outsets_));
}

bool SurfaceRoot::eligible(const Control::Ptr& control) const noexcept {
    return control && (*control).surface_root_ == this && (*control).eligible_for_input();
}



} // namespace gui_forms
