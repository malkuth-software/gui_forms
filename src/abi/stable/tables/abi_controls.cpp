// GF_TABLE_CONTROLS: layout transactions, window bounds, pointer capture,
// scrolling, focus and relative pointer mode. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_records.hpp"
#include "abi_tables.hpp"

#include "../values/abi_values.hpp"

#include "gui_forms/controls/scrollable_control.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/surface_material/paint_group_effect/paint_group_effect.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <memory>
#include <optional>

namespace gui_forms::abi1 {

gf_status enter_live_control(gf_object object, SlotIdentity slot, Control::Ptr& control) {
    const gf_status found = registry().enter_object(object, slot, control);
    if (found != GF_STATUS_OK) {
        return found;
    }
    if (!(*control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the object has been disposed");
    }
    return GF_STATUS_OK;
}

namespace {

constexpr SlotIdentity suspend_layout_slot{GF_TABLE_CONTROLS, 0U};
constexpr SlotIdentity resume_layout_slot{GF_TABLE_CONTROLS, 1U};
constexpr SlotIdentity perform_layout_slot{GF_TABLE_CONTROLS, 2U};
constexpr SlotIdentity layout_state_slot{GF_TABLE_CONTROLS, 3U};
constexpr SlotIdentity window_bounds_slot{GF_TABLE_CONTROLS, 4U};
constexpr SlotIdentity set_pointer_capture_slot{GF_TABLE_CONTROLS, 5U};
constexpr SlotIdentity pointer_capture_slot{GF_TABLE_CONTROLS, 6U};
constexpr SlotIdentity scroll_state_slot{GF_TABLE_CONTROLS, 7U};
constexpr SlotIdentity set_scroll_axis_slot{GF_TABLE_CONTROLS, 8U};
constexpr SlotIdentity scroll_into_view_slot{GF_TABLE_CONTROLS, 9U};
constexpr SlotIdentity invalidate_slot{GF_TABLE_CONTROLS, 10U};
constexpr SlotIdentity focus_slot{GF_TABLE_CONTROLS, 11U};
constexpr SlotIdentity focus_state_slot{GF_TABLE_CONTROLS, 12U};
constexpr SlotIdentity request_relative_pointer_slot{GF_TABLE_CONTROLS, 13U};
constexpr SlotIdentity release_relative_pointer_slot{GF_TABLE_CONTROLS, 14U};
constexpr SlotIdentity relative_pointer_state_slot{GF_TABLE_CONTROLS, 15U};
constexpr SlotIdentity warp_pointer_slot{GF_TABLE_CONTROLS, 16U};
constexpr SlotIdentity set_pointer_release_regions_slot{GF_TABLE_CONTROLS, 17U};
constexpr SlotIdentity set_content_effect_slot{GF_TABLE_CONTROLS, 18U};

// The content effect's C limits are the C++ ones.
static_assert(GF_CONTENT_EFFECT_MAXIMUM_MASKS == SurfaceMaterial::maximum_mask_layers);
static_assert(GF_CONTENT_EFFECT_MAXIMUM_BLUR == maximum_paint_group_effect_blur);
static_assert(sizeof(gf_content_effect::colour_table) == MaterialColourTable::channels * MaterialColourTable::levels);
static_assert(sizeof(gf_content_effect::colour_matrix) / sizeof(double) ==
              MaterialColourMatrix::rows * MaterialColourMatrix::columns);

constexpr std::uint32_t layout_transaction_size_1_0 = 32U;
constexpr std::uint32_t scroll_snapshot_size_1_0 = 248U;
static_assert(sizeof(gf_layout_transaction) == layout_transaction_size_1_0);
static_assert(sizeof(gf_scroll_axis) == 48U);
static_assert(sizeof(gf_scroll_snapshot) == scroll_snapshot_size_1_0);
static_assert(sizeof(gf_relative_pointer_request) == 24U);
static_assert(sizeof(gf_relative_pointer_grant) == 24U);
static_assert(sizeof(gf_relative_pointer_state) == 96U);

constexpr const char* not_scrollable = "the control does not scroll";

gf_scroll_axis c_axis(const ScrollAxisSnapshot& axis) noexcept {
    gf_scroll_axis record{};
    record.enabled = axis.enabled ? 1U : 0U;
    record.visible = axis.visible ? 1U : 0U;
    record.minimum = axis.minimum;
    record.maximum = axis.maximum;
    record.large_change = axis.large_change;
    record.small_change = axis.small_change;
    record.value = axis.value;
    return record;
}

// Lays the control out first, as a reader of arranged geometry expects: an
// attached control through its window, a detached one at its own size.
void settle_layout(ScrollableControl& scrollable) {
    if (scrollable.attached()) {
        static_cast<void>(scrollable.arranged_bounds());
        return;
    }
    const Rect requested = scrollable.requested_bounds();
    scrollable.arrange({0.0, 0.0, requested.width, requested.height});
}

gf_status controls_suspend_layout_body(gf_object object) {
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, suspend_layout_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*control).suspend_layout();
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_suspend_layout(gf_object object) noexcept {
    return call_slot(suspend_layout_slot, controls_suspend_layout_body, object);
}

gf_status controls_resume_layout_body(gf_object object, std::uint32_t perform_layout) {
    if (perform_layout > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, resume_layout_slot, "perform_layout is 0 or 1");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, resume_layout_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*control).resume_layout(perform_layout == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_resume_layout(gf_object object, std::uint32_t perform_layout) noexcept {
    return call_slot(resume_layout_slot, controls_resume_layout_body, object, perform_layout);
}

gf_status controls_perform_layout_body(gf_object object) {
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, perform_layout_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*control).perform_layout();
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_perform_layout(gf_object object) noexcept {
    return call_slot(perform_layout_slot, controls_perform_layout_body, object);
}

gf_status controls_layout_state_body(gf_object object, gf_layout_transaction* state) {
    const gf_status checked = check_record(state, layout_state_slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, layout_state_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const LayoutTransactionState snapshot = (*control).layout_transaction_state();
    gf_layout_transaction complete{};
    complete.struct_size = layout_transaction_size_1_0;
    complete.suspend_depth = snapshot.suspend_depth;
    complete.deferred = snapshot.deferred ? 1U : 0U;
    complete.requested_revision = snapshot.requested_revision;
    complete.committed_revision = snapshot.committed_revision;
    write_record(complete, state);
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_layout_state(gf_object object, gf_layout_transaction* state) noexcept {
    return call_slot(layout_state_slot, controls_layout_state_body, object, state);
}

gf_status controls_window_bounds_body(gf_object object, gf_logical_rect* bounds) {
    if (bounds == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, window_bounds_slot, "window_bounds needs an output");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, window_bounds_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const Rect absolute = (*control).absolute_bounds();
    *bounds = gf_logical_rect{absolute.x, absolute.y, absolute.width, absolute.height};
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_window_bounds(gf_object object, gf_logical_rect* bounds) noexcept {
    return call_slot(window_bounds_slot, controls_window_bounds_body, object, bounds);
}

gf_status controls_set_pointer_capture_body(gf_object object, std::uint32_t captured) {
    if (captured > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_pointer_capture_slot, "captured is 0 or 1");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, set_pointer_capture_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*control).set_pointer_capture(captured == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_set_pointer_capture(gf_object object, std::uint32_t captured) noexcept {
    return call_slot(set_pointer_capture_slot, controls_set_pointer_capture_body, object, captured);
}

gf_status controls_pointer_capture_body(gf_object object, std::uint32_t* captured) {
    if (captured == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, pointer_capture_slot, "pointer_capture needs an output");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, pointer_capture_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    *captured = (*control).has_pointer_capture() ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_pointer_capture(gf_object object, std::uint32_t* captured) noexcept {
    return call_slot(pointer_capture_slot, controls_pointer_capture_body, object, captured);
}

gf_status controls_scroll_state_body(gf_object object, gf_scroll_snapshot* state) {
    const gf_status checked = check_record(state, scroll_state_slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    std::shared_ptr<ScrollableControl> scrollable;
    const gf_status entered = enter_typed_control(object, scroll_state_slot, not_scrollable, scrollable);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    settle_layout(*scrollable);
    const ScrollSnapshot snapshot = (*scrollable).scroll_snapshot();
    const std::optional<ScrollEvent> last = (*scrollable).last_scroll_event();
    gf_scroll_snapshot complete{};
    complete.struct_size = scroll_snapshot_size_1_0;
    complete.auto_scroll = snapshot.auto_scroll ? 1U : 0U;
    complete.position = gf_logical_point{snapshot.position.x, snapshot.position.y};
    complete.margin = gf_logical_size{snapshot.margin.width, snapshot.margin.height};
    complete.minimum_content_size =
        gf_logical_size{snapshot.minimum_content_size.width, snapshot.minimum_content_size.height};
    complete.display_rectangle = gf_logical_rect{snapshot.display_rectangle.x, snapshot.display_rectangle.y,
                                                 snapshot.display_rectangle.width,
                                                 snapshot.display_rectangle.height};
    complete.viewport_rectangle = gf_logical_rect{snapshot.viewport_rectangle.x, snapshot.viewport_rectangle.y,
                                                  snapshot.viewport_rectangle.width,
                                                  snapshot.viewport_rectangle.height};
    complete.horizontal = c_axis(snapshot.horizontal);
    complete.vertical = c_axis(snapshot.vertical);
    complete.event_revision = (*scrollable).scroll_event_revision();
    if (last) {
        complete.event_type = static_cast<std::uint32_t>((*last).type);
        complete.event_orientation = static_cast<gf_scroll_orientation>((*last).orientation);
        complete.event_old_value = (*last).old_value;
        complete.event_new_value = (*last).new_value;
    }
    write_record(complete, state);
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_scroll_state(gf_object object, gf_scroll_snapshot* state) noexcept {
    return call_slot(scroll_state_slot, controls_scroll_state_body, object, state);
}

// Whether axis is one ScrollProperties holds.
bool valid_axis(const gf_scroll_axis& axis) noexcept {
    const bool finite = std::isfinite(axis.minimum) && std::isfinite(axis.maximum) &&
                        std::isfinite(axis.large_change) && std::isfinite(axis.small_change) &&
                        std::isfinite(axis.value);
    if (!finite || axis.enabled > 1U || axis.visible > 1U) {
        return false;
    }
    const bool ordered = axis.minimum >= 0.0 && axis.large_change >= 0.0 && axis.small_change >= 0.0 &&
                         axis.value >= axis.minimum && axis.value <= axis.maximum;
    return ordered;
}

gf_status controls_set_scroll_axis_body(gf_object object, gf_scroll_orientation orientation,
                                        const gf_scroll_axis* axis) {
    if (axis == nullptr || orientation > GF_SCROLL_ORIENTATION_VERTICAL || !valid_axis(*axis)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_scroll_axis_slot,
                    "an axis has a known orientation, finite non-negative bounds and changes, and a value "
                    "in its range");
    }
    std::shared_ptr<ScrollableControl> scrollable;
    const gf_status entered = enter_typed_control(object, set_scroll_axis_slot, not_scrollable, scrollable);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    ScrollProperties& properties = orientation == GF_SCROLL_ORIENTATION_HORIZONTAL
                                       ? (*scrollable).horizontal_scroll()
                                       : (*scrollable).vertical_scroll();
    // An auto-scrolling control keeps its own range: set_minimum and
    // set_maximum leave it, and set_value checks against it. So the value is
    // checked against the range the setters will leave before the first
    // one runs, and a refusal changes nothing (A-gf-abi-03-F4).
    if ((*scrollable).auto_scroll.read()) {
        const bool in_kept_range = (*axis).value >= properties.minimum() && (*axis).value <= properties.maximum();
        if (!in_kept_range) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_scroll_axis_slot,
                        "an auto-scrolling control keeps its range; the value must lie in it");
        }
    }
    properties.set_minimum((*axis).minimum);
    properties.set_maximum((*axis).maximum);
    properties.set_large_change((*axis).large_change);
    properties.set_small_change((*axis).small_change);
    properties.set_enabled((*axis).enabled == 1U);
    properties.set_visible((*axis).visible == 1U);
    properties.set_value((*axis).value);
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_set_scroll_axis(gf_object object, gf_scroll_orientation orientation,
                                           const gf_scroll_axis* axis) noexcept {
    return call_slot(set_scroll_axis_slot, controls_set_scroll_axis_body, object, orientation, axis);
}

gf_status controls_scroll_into_view_body(gf_object object, gf_object child) {
    std::shared_ptr<ScrollableControl> scrollable;
    const gf_status entered = enter_typed_control(object, scroll_into_view_slot, not_scrollable, scrollable);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Control::Ptr target;
    const gf_status found = registry().find_object(child, scroll_into_view_slot, target);
    if (found != GF_STATUS_OK) {
        return found;
    }
    settle_layout(*scrollable);
    (*scrollable).scroll_control_into_view(target);
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_scroll_into_view(gf_object object, gf_object child) noexcept {
    return call_slot(scroll_into_view_slot, controls_scroll_into_view_body, object, child);
}

gf_status controls_invalidate_body(gf_object object, const gf_logical_rect* damage) {
    if (damage != nullptr &&
        (!finite_all((*damage).x, (*damage).y, (*damage).width, (*damage).height) || (*damage).width < 0.0 ||
         (*damage).height < 0.0)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, invalidate_slot, "damage is finite with a size not negative");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, invalidate_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    if (damage == nullptr) {
        (*control).invalidate(invalidation::paint_only);
    } else {
        (*control).invalidate(Rect{(*damage).x, (*damage).y, (*damage).width, (*damage).height});
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_invalidate(gf_object object, const gf_logical_rect* damage) noexcept {
    return call_slot(invalidate_slot, controls_invalidate_body, object, damage);
}

// Focus (since 1.1): Control.Focus / Select, Focused and ContainsFocus.
gf_status focus_control(gf_object object, std::uint32_t* focused) {
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, focus_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    SurfaceRoot* const window = (*control).attached_surface_root();
    if (window == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, focus_slot, "the control is not in a window");
    }
    if (!(*control).eligible_for_input()) {
        return fail(GF_STATUS_INVALID_STATE, focus_slot,
                    "the control cannot take focus: it or an ancestor is disabled or hidden");
    }
    // A composite field (a combo box with an edit field, a numeric up-down)
    // gives focus to its edit part, which must be able to take it.
    const Control::Ptr target = SurfaceRoot::focus_target_of(control);
    if (!(*target).eligible_for_input() || !(*target).focusable.read()) {
        return fail(GF_STATUS_INVALID_STATE, focus_slot, "the control cannot take focus: it is not focusable");
    }
    // The focus change runs focus and validation callbacks, which may
    // dispose the window or take the control out of it: the window is
    // watched across the change and read again through the control (held
    // by its Ptr), never through the pointer taken before it
    // (A-gf-abi-03-F3). A root that is not a Window has no lifetime to
    // watch and is found again through the control alone.
    const WindowRef watched = (*window).window_ref();
    const bool watched_window = watched.get() != nullptr;
    const bool moved = (*window).request_focus(control);
    const bool window_gone = watched_window && watched.get() == nullptr;
    SurfaceRoot* const after = window_gone ? nullptr : (*control).attached_surface_root();
    if (after == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, focus_slot,
                    "the control left its window while focus moved: a callback removed it or disposed the window");
    }
    if (!moved || (*after).focused_control() != target) {
        return fail(GF_STATUS_INVALID_STATE, focus_slot,
                    "focus was refused: the control is outside the window's open focus scope, or validation "
                    "kept focus where it was");
    }
    // A window counts as active only once a host session presents it: a
    // window that has not run keeps SurfaceRoot's headless default
    // (active) but does not have the keyboard.
    const bool keyboard = (*after).active() && (*after).host_services() != nullptr;
    if (focused != nullptr) *focused = keyboard ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status controls_focus_body(gf_object object, std::uint32_t* focused) {
    const gf_status status = focus_control(object, focused);
    return status;
}

gf_status GF_CALL controls_focus(gf_object object, std::uint32_t* focused) noexcept {
    return call_slot(focus_slot, controls_focus_body, object, focused);
}

// Whether `holder` is `control` or one of its ancestors.
bool holds(const Control::Ptr& holder, const Control::Ptr& control) noexcept {
    for (Control::Ptr current = control; current; current = (*current).parent()) {
        if (current == holder) return true;
    }
    return false;
}

gf_status controls_focus_state_body(gf_object object, std::uint32_t* focused,
                                    std::uint32_t* contains_focus) {
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, focus_state_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    SurfaceRoot* const window = (*control).attached_surface_root();
    const Control::Ptr current = window == nullptr ? Control::Ptr{} : (*window).focused_control();
    // A composite is focused while its edit part holds focus.
    const bool is_focused = window != nullptr && (*window).control_focused(control);
    const bool contains = current && holds(control, current);
    if (focused != nullptr) *focused = is_focused ? 1U : 0U;
    if (contains_focus != nullptr) *contains_focus = contains ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_focus_state(gf_object object, std::uint32_t* focused,
                                       std::uint32_t* contains_focus) noexcept {
    return call_slot(focus_state_slot, controls_focus_state_body, object, focused, contains_focus);
}

gf_relative_pointer_mode c_mode(RelativePointerMode mode) noexcept {
    switch (mode) {
    case RelativePointerMode::confined: return GF_RELATIVE_POINTER_MODE_CONFINED;
    case RelativePointerMode::locked: return GF_RELATIVE_POINTER_MODE_LOCKED;
    case RelativePointerMode::free:
    default: return GF_RELATIVE_POINTER_MODE_FREE;
    }
}

gf_relative_pointer_result c_result(RelativePointerResult result) noexcept {
    switch (result) {
    case RelativePointerResult::granted: return GF_RELATIVE_POINTER_RESULT_GRANTED;
    case RelativePointerResult::degraded: return GF_RELATIVE_POINTER_RESULT_DEGRADED;
    case RelativePointerResult::not_active: return GF_RELATIVE_POINTER_RESULT_NOT_ACTIVE;
    case RelativePointerResult::no_press: return GF_RELATIVE_POINTER_RESULT_NO_PRESS;
    case RelativePointerResult::invalid_request: return GF_RELATIVE_POINTER_RESULT_INVALID_REQUEST;
    case RelativePointerResult::detached:
    default: return GF_RELATIVE_POINTER_RESULT_DETACHED;
    }
}

gf_relative_pointer_end c_end(RelativePointerEnd end) noexcept {
    switch (end) {
    case RelativePointerEnd::escape: return GF_RELATIVE_POINTER_END_ESCAPE;
    case RelativePointerEnd::focus_lost: return GF_RELATIVE_POINTER_END_FOCUS_LOST;
    case RelativePointerEnd::deactivated: return GF_RELATIVE_POINTER_END_DEACTIVATED;
    case RelativePointerEnd::modal: return GF_RELATIVE_POINTER_END_MODAL;
    case RelativePointerEnd::detached: return GF_RELATIVE_POINTER_END_DETACHED;
    case RelativePointerEnd::press_released: return GF_RELATIVE_POINTER_END_PRESS_RELEASED;
    case RelativePointerEnd::superseded: return GF_RELATIVE_POINTER_END_SUPERSEDED;
    case RelativePointerEnd::released:
    default: return GF_RELATIVE_POINTER_END_RELEASED;
    }
}

gf_status controls_request_relative_pointer_body(gf_object object, const gf_relative_pointer_request* request,
                                                 gf_relative_pointer_grant* grant) {
    const SlotIdentity slot = request_relative_pointer_slot;
    if (request == nullptr || (*request).struct_size < sizeof(gf_relative_pointer_request)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the request is null or smaller than its 1.5 size");
    }
    const gf_status checked = check_record(grant, slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    if ((*request).hide_cursor > 1U || (*request).raw_motion > 1U || (*request).until_release > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "hide_cursor, raw_motion and until_release are 0 or 1");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    RelativePointerRequest asked{};
    switch ((*request).mode) {
    case GF_RELATIVE_POINTER_MODE_FREE: asked.mode = RelativePointerMode::free; break;
    case GF_RELATIVE_POINTER_MODE_CONFINED: asked.mode = RelativePointerMode::confined; break;
    case GF_RELATIVE_POINTER_MODE_LOCKED: asked.mode = RelativePointerMode::locked; break;
    default: return fail(GF_STATUS_INVALID_ARGUMENT, slot, "mode is a gf_relative_pointer_mode");
    }
    asked.hide_cursor = (*request).hide_cursor == 1U;
    asked.raw_motion = (*request).raw_motion == 1U;
    asked.until_release = (*request).until_release == 1U;
    const RelativePointerGrant granted = (*control).request_relative_pointer(asked);
    gf_relative_pointer_grant complete{};
    complete.result = c_result(granted.result);
    complete.mode = c_mode(granted.mode);
    complete.raw = granted.raw ? 1U : 0U;
    complete.cursor_hidden = granted.cursor_hidden ? 1U : 0U;
    complete.host_relative = granted.host_relative ? 1U : 0U;
    write_record(complete, grant);
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_request_relative_pointer(gf_object object, const gf_relative_pointer_request* request,
                                                    gf_relative_pointer_grant* grant) noexcept {
    return call_slot(request_relative_pointer_slot, controls_request_relative_pointer_body, object, request, grant);
}

gf_status controls_release_relative_pointer_body(gf_object object, std::uint32_t* released) {
    const SlotIdentity slot = release_relative_pointer_slot;
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const bool ended = (*control).release_relative_pointer();
    if (released != nullptr) *released = ended ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_release_relative_pointer(gf_object object, std::uint32_t* released) noexcept {
    return call_slot(release_relative_pointer_slot, controls_release_relative_pointer_body, object, released);
}

gf_status controls_relative_pointer_state_body(gf_object object, gf_relative_pointer_state* state) {
    const SlotIdentity slot = relative_pointer_state_slot;
    const gf_status checked = check_record(state, slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    gf_relative_pointer_state complete{};
    SurfaceRoot* const window = (*control).attached_surface_root();
    if (window != nullptr) {
        const RelativePointerSnapshot snapshot = (*window).relative_pointer_snapshot();
        const RelativePointerSupport support = (*window).relative_pointer_support();
        complete.owned = (*control).has_relative_pointer() ? 1U : 0U;
        complete.active = snapshot.active ? 1U : 0U;
        complete.suspended = snapshot.suspended ? 1U : 0U;
        complete.mode = c_mode(snapshot.mode);
        complete.raw = snapshot.raw ? 1U : 0U;
        complete.host_relative = snapshot.host_relative ? 1U : 0U;
        complete.last_end = c_end(snapshot.last_end);
        complete.grants = snapshot.grants;
        complete.samples = snapshot.samples;
        complete.suspensions = snapshot.suspensions;
        complete.resumptions = snapshot.resumptions;
        complete.ends = snapshot.ends;
        complete.support_relative_motion = support.relative_motion ? 1U : 0U;
        complete.support_raw_motion = support.raw_motion ? 1U : 0U;
        complete.support_lock = support.lock ? 1U : 0U;
        complete.support_confine = support.confine ? 1U : 0U;
        complete.support_warp = support.warp ? 1U : 0U;
    }
    write_record(complete, state);
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_relative_pointer_state(gf_object object, gf_relative_pointer_state* state) noexcept {
    return call_slot(relative_pointer_state_slot, controls_relative_pointer_state_body, object, state);
}

gf_status controls_warp_pointer_body(gf_object object, gf_logical_point position, std::uint32_t* warped) {
    const SlotIdentity slot = warp_pointer_slot;
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the position is not finite");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const bool moved = (*control).warp_pointer(Point{position.x, position.y});
    if (warped != nullptr) *warped = moved ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_warp_pointer(gf_object object, gf_logical_point position, std::uint32_t* warped) noexcept {
    return call_slot(warp_pointer_slot, controls_warp_pointer_body, object, position, warped);
}

gf_status controls_set_pointer_release_regions_body(gf_object object, const gf_logical_rect* regions,
                                                    std::uint32_t count) {
    const SlotIdentity slot = set_pointer_release_regions_slot;
    if (count > maximum_pointer_release_regions || (count != 0U && regions == nullptr)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "at most 8 regions, and an array for any");
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    SurfaceRoot* const window = (*control).attached_surface_root();
    if (window == nullptr) {
        return fail(GF_STATUS_INVALID_STATE, slot, "the control is not in a window");
    }
    std::array<Rect, maximum_pointer_release_regions> copied{};
    for (std::uint32_t index = 0; index < count; ++index) {
        const gf_logical_rect region = regions[index];
        copied[index] = Rect{region.x, region.y, region.width, region.height};
    }
    const bool set = (*window).set_pointer_release_regions(std::span<const Rect>(copied.data(), count));
    if (!set) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a region is not finite or is empty");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_set_pointer_release_regions(gf_object object, const gf_logical_rect* regions,
                                                       std::uint32_t count) noexcept {
    return call_slot(set_pointer_release_regions_slot, controls_set_pointer_release_regions_body, object, regions, count);
}

// One caller mask layer as the engine's, or nothing for an unknown mode or
// composite, a nonzero reserved or a point that is not finite.
std::optional<MaterialMaskLayer> mask_layer_from(const gf_effect_mask_layer& layer) {
    if (layer.reserved != 0U || !std::isfinite(layer.start.x) || !std::isfinite(layer.start.y) ||
        !std::isfinite(layer.end.x) || !std::isfinite(layer.end.y)) {
        return std::nullopt;
    }
    MaterialMaskLayer result{};
    switch (layer.mode) {
    case GF_EFFECT_MASK_MODE_ALPHA: result.mode = MaterialMaskMode::alpha; break;
    case GF_EFFECT_MASK_MODE_LUMINANCE: result.mode = MaterialMaskMode::luminance; break;
    case GF_EFFECT_MASK_MODE_INVERSE_ALPHA: result.mode = MaterialMaskMode::inverse_alpha; break;
    case GF_EFFECT_MASK_MODE_INVERSE_LUMINANCE: result.mode = MaterialMaskMode::inverse_luminance; break;
    default: return std::nullopt;
    }
    switch (layer.composite) {
    case GF_EFFECT_MASK_COMPOSITE_ADD: result.composite = MaterialMaskComposite::add; break;
    case GF_EFFECT_MASK_COMPOSITE_SUBTRACT: result.composite = MaterialMaskComposite::subtract; break;
    case GF_EFFECT_MASK_COMPOSITE_INTERSECT: result.composite = MaterialMaskComposite::intersect; break;
    case GF_EFFECT_MASK_COMPOSITE_EXCLUDE: result.composite = MaterialMaskComposite::exclude; break;
    default: return std::nullopt;
    }
    const Color start_colour = unpacked_color(layer.start_rgba);
    const bool solid = layer.start.x == layer.end.x && layer.start.y == layer.end.y;
    if (solid) {
        result.source = MaterialFillLayer::solid(start_colour);
        return result;
    }
    const std::array<GradientStop, 2> stops{GradientStop{0.0, start_colour},
                                            GradientStop{1.0, unpacked_color(layer.end_rgba)}};
    result.source = MaterialFillLayer::linear(Point{layer.start.x, layer.start.y}, Point{layer.end.x, layer.end.y},
                                              stops.data(), stops.size(), MaterialCoordinateSpace::normalized);
    return result;
}

gf_status controls_set_content_effect_body(gf_object object, const gf_content_effect* effect) {
    const SlotIdentity slot = set_content_effect_slot;
    // Everything the caller lends is read and checked before the control
    // changes.
    std::shared_ptr<const PaintGroupEffect> shared{};
    if (effect != nullptr) {
        const gf_content_effect& given = *effect;
        if (given.struct_size < sizeof(gf_content_effect)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the effect is smaller than its 1.6 size");
        }
        if (given.has_colour_matrix > 1U || given.has_colour_table > 1U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "has_colour_matrix and has_colour_table are 0 or 1");
        }
        if (given.mask_count > GF_CONTENT_EFFECT_MAXIMUM_MASKS) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "at most 4 mask layers");
        }
        if (!std::isfinite(given.blur) || given.blur < 0.0 || given.blur > GF_CONTENT_EFFECT_MAXIMUM_BLUR) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the blur is not finite or is out of range");
        }
        std::uint32_t stride = 0U;
        const gf_status arrayed =
            check_array(given.masks, given.mask_count, static_cast<std::uint32_t>(sizeof(gf_effect_mask_layer)), slot,
                        stride);
        if (arrayed != GF_STATUS_OK) {
            return arrayed;
        }
        PaintGroupEffect built{};
        built.blur = given.blur;
        if (given.has_colour_matrix == 1U || given.has_colour_table == 1U) {
            MaterialColourMatrix matrix{};
            if (given.has_colour_matrix == 1U) {
                for (std::size_t index = 0U; index < matrix.values.size(); ++index) {
                    const double value = given.colour_matrix[index];
                    if (!std::isfinite(value)) {
                        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a colour matrix value is not finite");
                    }
                    matrix.values[index] = value;
                }
            }
            if (given.has_colour_table == 1U) {
                MaterialColourTable table{};
                std::memcpy(table.entries.data(), given.colour_table, table.entries.size());
                built.colour_filter = MaterialColourFilter(matrix, table);
            } else {
                built.colour_filter = MaterialColourFilter(matrix);
            }
        }
        built.masks.reserve(given.mask_count);
        for (std::uint32_t index = 0U; index < given.mask_count; ++index) {
            const std::optional<MaterialMaskLayer> layer = mask_layer_from(element_at(given.masks, stride, index));
            if (!layer) {
                return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                            "a mask layer's mode, composite, reserved or points are invalid");
            }
            built.masks.push_back(*layer);
        }
        if (!valid_paint_group_effect(built)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the effect is not valid");
        }
        shared = std::make_shared<const PaintGroupEffect>(std::move(built));
    }
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*control).set_content_effect(std::move(shared));
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_set_content_effect(gf_object object, const gf_content_effect* effect) noexcept {
    return call_slot(set_content_effect_slot, controls_set_content_effect_body, object, effect);
}

constexpr gf_controls_table table{
    .struct_size = sizeof(gf_controls_table),
    .abi_minor = GF_ABI_MINOR,
    .suspend_layout = &controls_suspend_layout,
    .resume_layout = &controls_resume_layout,
    .perform_layout = &controls_perform_layout,
    .layout_state = &controls_layout_state,
    .window_bounds = &controls_window_bounds,
    .set_pointer_capture = &controls_set_pointer_capture,
    .pointer_capture = &controls_pointer_capture,
    .scroll_state = &controls_scroll_state,
    .set_scroll_axis = &controls_set_scroll_axis,
    .scroll_into_view = &controls_scroll_into_view,
    .invalidate = &controls_invalidate,
    .focus = &controls_focus,
    .focus_state = &controls_focus_state,
    .request_relative_pointer = &controls_request_relative_pointer,
    .release_relative_pointer = &controls_release_relative_pointer,
    .relative_pointer_state = &controls_relative_pointer_state,
    .warp_pointer = &controls_warp_pointer,
    .set_pointer_release_regions = &controls_set_pointer_release_regions,
    .set_content_effect = &controls_set_content_effect,
};

} // namespace

const gf_controls_table& controls_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
