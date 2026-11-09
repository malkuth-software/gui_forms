// GF_TABLE_CONTROLS: layout transactions, window bounds, pointer capture and
// scrolling. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_records.hpp"
#include "abi_tables.hpp"

#include "../values/abi_values.hpp"

#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

namespace gui_forms::abi1 {

gf_status enter_live_control(gf_object object, SlotIdentity slot, Control::Ptr& control) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status found = registry().find_object(object, slot, control);
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

constexpr std::uint32_t layout_transaction_size_1_0 = 32U;
constexpr std::uint32_t scroll_snapshot_size_1_0 = 248U;
static_assert(sizeof(gf_layout_transaction) == layout_transaction_size_1_0);
static_assert(sizeof(gf_scroll_axis) == 48U);
static_assert(sizeof(gf_scroll_snapshot) == scroll_snapshot_size_1_0);

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

gf_status GF_CALL controls_suspend_layout(gf_object object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(suspend_layout_slot.table_id, suspend_layout_slot.slot_index);
        Control::Ptr control;
        const gf_status entered = enter_live_control(object, suspend_layout_slot, control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*control).suspend_layout();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(suspend_layout_slot);
    }
}

gf_status GF_CALL controls_resume_layout(gf_object object, std::uint32_t perform_layout) noexcept {
    try {
        const ShadowFrameScope shadow_frame(resume_layout_slot.table_id, resume_layout_slot.slot_index);
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
    } catch (...) {
        return fail_with_current_exception(resume_layout_slot);
    }
}

gf_status GF_CALL controls_perform_layout(gf_object object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(perform_layout_slot.table_id, perform_layout_slot.slot_index);
        Control::Ptr control;
        const gf_status entered = enter_live_control(object, perform_layout_slot, control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*control).perform_layout();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(perform_layout_slot);
    }
}

gf_status GF_CALL controls_layout_state(gf_object object, gf_layout_transaction* state) noexcept {
    try {
        const ShadowFrameScope shadow_frame(layout_state_slot.table_id, layout_state_slot.slot_index);
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
    } catch (...) {
        return fail_with_current_exception(layout_state_slot);
    }
}

gf_status GF_CALL controls_window_bounds(gf_object object, gf_logical_rect* bounds) noexcept {
    try {
        const ShadowFrameScope shadow_frame(window_bounds_slot.table_id, window_bounds_slot.slot_index);
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
    } catch (...) {
        return fail_with_current_exception(window_bounds_slot);
    }
}

gf_status GF_CALL controls_set_pointer_capture(gf_object object, std::uint32_t captured) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_pointer_capture_slot.table_id, set_pointer_capture_slot.slot_index);
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
    } catch (...) {
        return fail_with_current_exception(set_pointer_capture_slot);
    }
}

gf_status GF_CALL controls_pointer_capture(gf_object object, std::uint32_t* captured) noexcept {
    try {
        const ShadowFrameScope shadow_frame(pointer_capture_slot.table_id, pointer_capture_slot.slot_index);
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
    } catch (...) {
        return fail_with_current_exception(pointer_capture_slot);
    }
}

gf_status GF_CALL controls_scroll_state(gf_object object, gf_scroll_snapshot* state) noexcept {
    try {
        const ShadowFrameScope shadow_frame(scroll_state_slot.table_id, scroll_state_slot.slot_index);
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
    } catch (...) {
        return fail_with_current_exception(scroll_state_slot);
    }
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

gf_status GF_CALL controls_set_scroll_axis(gf_object object, gf_scroll_orientation orientation,
                                           const gf_scroll_axis* axis) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_scroll_axis_slot.table_id, set_scroll_axis_slot.slot_index);
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
        properties.set_minimum((*axis).minimum);
        properties.set_maximum((*axis).maximum);
        properties.set_large_change((*axis).large_change);
        properties.set_small_change((*axis).small_change);
        properties.set_enabled((*axis).enabled == 1U);
        properties.set_visible((*axis).visible == 1U);
        properties.set_value((*axis).value);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_scroll_axis_slot);
    }
}

gf_status GF_CALL controls_scroll_into_view(gf_object object, gf_object child) noexcept {
    try {
        const ShadowFrameScope shadow_frame(scroll_into_view_slot.table_id, scroll_into_view_slot.slot_index);
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
    } catch (...) {
        return fail_with_current_exception(scroll_into_view_slot);
    }
}

gf_status GF_CALL controls_invalidate(gf_object object, const gf_logical_rect* damage) noexcept {
    try {
        const ShadowFrameScope shadow_frame(invalidate_slot.table_id, invalidate_slot.slot_index);
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
    } catch (...) {
        return fail_with_current_exception(invalidate_slot);
    }
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
    if (!(*control).focusable.read()) {
        return fail(GF_STATUS_INVALID_STATE, focus_slot, "the control cannot take focus: it is not focusable");
    }
    const bool moved = (*window).request_focus(control);
    if (!moved || (*window).focused_control() != control) {
        return fail(GF_STATUS_INVALID_STATE, focus_slot,
                    "focus was refused: the control is outside the window's open focus scope, or validation "
                    "kept focus where it was");
    }
    if (focused != nullptr) *focused = (*window).active() ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status GF_CALL controls_focus(gf_object object, std::uint32_t* focused) noexcept {
    try {
        const ShadowFrameScope shadow_frame(focus_slot.table_id, focus_slot.slot_index);
        const gf_status status = focus_control(object, focused);
        return status;
    } catch (...) {
        return fail_with_current_exception(focus_slot);
    }
}

// Whether `holder` is `control` or one of its ancestors.
bool holds(const Control::Ptr& holder, const Control::Ptr& control) noexcept {
    for (Control::Ptr current = control; current; current = (*current).parent()) {
        if (current == holder) return true;
    }
    return false;
}

gf_status GF_CALL controls_focus_state(gf_object object, std::uint32_t* focused,
                                       std::uint32_t* contains_focus) noexcept {
    try {
        const ShadowFrameScope shadow_frame(focus_state_slot.table_id, focus_state_slot.slot_index);
        Control::Ptr control;
        const gf_status entered = enter_live_control(object, focus_state_slot, control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        SurfaceRoot* const window = (*control).attached_surface_root();
        const Control::Ptr current = window == nullptr ? Control::Ptr{} : (*window).focused_control();
        const bool is_focused = current && current == control;
        const bool contains = current && holds(control, current);
        if (focused != nullptr) *focused = is_focused ? 1U : 0U;
        if (contains_focus != nullptr) *contains_focus = contains ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(focus_state_slot);
    }
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
};

} // namespace

const gf_controls_table& controls_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
