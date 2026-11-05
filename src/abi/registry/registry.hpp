#pragma once

#include "../control_adapters/abi_control_adapters.hpp"
#include "../error/abi_error.hpp"

namespace gui_forms::abi::detail {

enum class SlotKind {
    empty,
    control,
    subscription,
};

struct ControlRecord;

struct SubscriptionRecord final {
    gf_event_callback callback{};
    gf_event_callback_v2 callback_v2{};
    gf_pointer_callback pointer_callback{};
    gf_key_callback key_callback{};
    gf_key_callback key_preview_callback{};
    gf_text_callback text_callback{};
    void* context{};
    std::thread::id ui_thread;
    gui_forms::SubscriptionToken native_subscription;
    std::uint32_t event_kind{};
    bool connected{true};
};

struct DispatchRecord final {
    gf_dispatch_callback callback{};
    void* context{};
};

enum class AbiHostPhase : std::uint8_t {
    idle,
    starting,
    ready,
    stopping,
};

// The managed trace's spelling of a host phase.
[[nodiscard]] inline const char* host_phase_name(AbiHostPhase phase) noexcept {
    switch (phase) {
    case AbiHostPhase::idle: return "idle";
    case AbiHostPhase::starting: return "starting";
    case AbiHostPhase::ready: return "ready";
    case AbiHostPhase::stopping: return "stopping";
    }
    // A phase outside the enumeration is traced as idle.
    return "idle";
}

// The C ABI code for a component state.
[[nodiscard]] inline std::uint32_t component_state_code(ComponentState state) noexcept {
    switch (state) {
    case ComponentState::alive: return GF_COMPONENT_ALIVE;
    case ComponentState::disposing: return GF_COMPONENT_DISPOSING;
    case ComponentState::disposed: return GF_COMPONENT_DISPOSED;
    }
    // A state outside the enumeration is reported disposed: the control is
    // not to be used.
    return GF_COMPONENT_DISPOSED;
}

struct ControlRecord final {
    std::shared_ptr<Control> control;
    gui_forms::PopupToken popup_token;
    std::vector<std::pair<std::weak_ptr<Control>, gui_forms::PaintPlane>>
        popup_plane_overrides;
    std::thread::id ui_thread;
    std::uint32_t kind{GF_CONTROL_GENERIC};
    std::string name;
    std::string text;
    std::string host_trace;
    std::deque<DispatchRecord> dispatch_queue;
    std::uint32_t dispatch_depth{};
    std::uint32_t managed_callback_depth{};
    bool dispatch_wake_pending{};
    std::function<void()> host_wake;
    std::function<void()> host_close;
    std::function<gui_forms::HostDialogResult(
        const gui_forms::HostDialogRequest&)> host_dialog;
    std::function<gui_forms::HostServiceStatus(
        const gui_forms::HostTooltipRequest&)> host_tooltip_show;
    std::function<void()> host_tooltip_hide;
    std::function<gui_forms::HostClipboardTextResult()> host_clipboard_read;
    std::function<gui_forms::HostServiceStatus(std::string_view)> host_clipboard_write;
    std::string last_dialog_path;
    std::uint64_t callback_faults{};
    std::uint64_t dispatches{};
    std::uint64_t dispatch_turns{};
    bool close_requested{};
    AbiHostPhase host_phase{AbiHostPhase::idle};
    std::uint64_t external_references{1};
    std::vector<gf_event_token> subscriptions;
};

struct RegistrySlot final {
    std::uint32_t generation{1};
    SlotKind kind{SlotKind::empty};
    std::shared_ptr<ControlRecord> control;
    std::shared_ptr<SubscriptionRecord> subscription;
};

inline gf_scroll_axis_state abi_scroll_axis_state(
    const gui_forms::ScrollAxisSnapshot& source) noexcept {
    return {source.enabled ? 1U : 0U, source.visible ? 1U : 0U,
            source.minimum, source.maximum, source.large_change,
            source.small_change, source.value};
}

inline std::uint8_t disabled_color_channel(
    std::uint8_t foreground, std::uint8_t background) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(foreground) * 45U +
         static_cast<unsigned>(background) * 55U) / 100U);
}

class Registry final {
#if defined(GUI_FORMS_ABI_REGISTRY_TEST)
    friend struct RegistryLifetimeFixture;
#endif
public:
    gf_result create(std::uint32_t kind, gf_string_view stable_id, gf_handle* output) {
        if (output == nullptr || stable_id.data == nullptr || stable_id.size == 0U ||
            stable_id.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "control_create requires a non-empty bounded stable ID and output");
        }
        std::string id(stable_id.data, static_cast<std::size_t>(stable_id.size));
        if (id.find('\0') != std::string::npos) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "stable ID may not contain NUL bytes");
        }
        std::shared_ptr<gui_forms::abi::detail::ControlRecord> record = std::make_shared<ControlRecord>();
        StableId native_id(std::move(id));
        switch (kind) {
        case GF_CONTROL_FORM:
            (*record).control = std::make_shared<FormControl>(std::move(native_id));
            break;
        case GF_CONTROL_USER_CONTROL:
            (*record).control = std::make_shared<gui_forms::Panel>(std::move(native_id));
            break;
        case GF_CONTROL_PANEL: {
            std::shared_ptr<gui_forms::Panel> panel = std::make_shared<gui_forms::Panel>(std::move(native_id));
            // WinForms Panel starts borderless and inherits its effective
            // background. Explicit BorderStyle and BackColor projection below
            // own the two visual decisions independently.
            (*panel).set_border_style(gui_forms::BorderStyle::none);
            (*record).control = std::move(panel);
            break;
        }
        case GF_CONTROL_BUTTON:
            (*record).control = std::make_shared<gui_forms::Button>(std::move(native_id));
            break;
        case GF_CONTROL_CHECK_BOX:
            (*record).control = std::make_shared<gui_forms::CheckBox>(std::move(native_id));
            break;
        case GF_CONTROL_LABEL:
            (*record).control = std::make_shared<gui_forms::Label>(std::move(native_id));
            break;
        case GF_CONTROL_COMBO_BOX:
            (*record).control = std::make_shared<FieldControl>(
                std::move(native_id), FieldControlKind::combo_box);
            break;
        case GF_CONTROL_LIST_BOX:
            (*record).control = std::make_shared<FieldControl>(
                std::move(native_id), FieldControlKind::list_box);
            break;
        case GF_CONTROL_TEXT_BOX:
            (*record).control = std::make_shared<FieldControl>(
                std::move(native_id), FieldControlKind::text_box);
            break;
        case GF_CONTROL_TRACK_BAR:
            (*record).control = std::make_shared<gui_forms::TrackBar>(std::move(native_id));
            break;
        case GF_CONTROL_RADIO_BUTTON:
            (*record).control = std::make_shared<gui_forms::RadioButton>(std::move(native_id));
            break;
        case GF_CONTROL_GROUP_BOX:
            (*record).control = std::make_shared<gui_forms::GroupBox>(std::move(native_id));
            break;
        case GF_CONTROL_PROGRESS_BAR:
            (*record).control = std::make_shared<gui_forms::ProgressBar>(std::move(native_id));
            break;
        case GF_CONTROL_LINK_LABEL:
            (*record).control = std::make_shared<gui_forms::LinkLabel>(std::move(native_id));
            break;
        case GF_CONTROL_PICTURE_BOX:
            (*record).control = std::make_shared<FieldControl>(
                std::move(native_id), FieldControlKind::picture_box);
            break;
        case GF_CONTROL_DATA_GRID_VIEW:
            (*record).control = std::make_shared<FieldControl>(
                std::move(native_id), FieldControlKind::data_grid);
            break;
        case GF_CONTROL_TOOL_STRIP:
            (*record).control = std::make_shared<FieldControl>(
                std::move(native_id), FieldControlKind::tool_strip);
            break;
        case GF_CONTROL_NUMERIC_UP_DOWN:
            (*record).control = std::make_shared<FieldControl>(
                std::move(native_id), FieldControlKind::numeric_up_down);
            break;
        case GF_CONTROL_INPUT_TRANSPARENT:
            (*record).control =
                std::make_shared<InputTransparentControl>(std::move(native_id));
            break;
        case GF_CONTROL_INPUT_TRANSPARENT_CUSTOM:
            (*record).control =
                std::make_shared<RasterControl>(std::move(native_id), true);
            break;
        case GF_CONTROL_PROPERTY_GRID: {
            std::shared_ptr<gui_forms::PropertyGrid> property_grid =
                std::make_shared<gui_forms::PropertyGrid>(std::move(native_id));
            (*property_grid).initialize_control_tree();
            (*record).control = std::move(property_grid);
            break;
        }
        case GF_CONTROL_PROPERTY_OBJECT_PROXY:
            (*record).control =
                std::make_shared<AbiPropertyObjectControl>(std::move(native_id));
            break;
        case GF_CONTROL_OVERLAY_CUSTOM:
            (*record).control = std::make_shared<RasterControl>(std::move(native_id));
            (*(*record).control).set_paint_plane(gui_forms::PaintPlane::overlay);
            break;
        case GF_CONTROL_CUSTOM:
            (*record).control = std::make_shared<RasterControl>(std::move(native_id));
            break;
        default:
            (*record).control = std::make_shared<Control>(std::move(native_id));
            break;
        }
        (*record).ui_thread = std::this_thread::get_id();
        (*record).kind = kind;
        std::scoped_lock lock(mutex_);
        *output = allocate_locked(SlotKind::control, record, {});
        return GF_OK;
    }

    gf_result set_string(gf_handle handle, gf_string_view input, bool is_text) {
        if ((input.size != 0U && input.data == nullptr) ||
            input.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "string input is not a bounded UTF-8 view");
        }
        std::string value;
        if (input.size != 0U) {
            value.assign(input.data, static_cast<std::size_t>(input.size));
        }
        if (value.find('\0') != std::string::npos) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "string input may not contain NUL bytes");
        }
        if (!gui_forms::validate_utf8(value).valid()) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "string input must contain valid UTF-8");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (is_text ? (*record).text : (*record).name) = value;
        if (is_text) {
            if (const std::shared_ptr<gui_forms::ButtonBase> button =
                    std::dynamic_pointer_cast<gui_forms::ButtonBase>((*record).control)) {
                (*button).set_text(value);
            } else if (const std::shared_ptr<gui_forms::Label> label =
                           std::dynamic_pointer_cast<gui_forms::Label>((*record).control)) {
                (*label).set_text(value);
            } else if (const std::shared_ptr<gui_forms::GroupBox> group =
                           std::dynamic_pointer_cast<gui_forms::GroupBox>((*record).control)) {
                (*group).set_text(value);
            } else if (const std::shared_ptr<gui_forms::abi::detail::FieldControl> field =
                           std::dynamic_pointer_cast<FieldControl>((*record).control)) {
                (*field).set_text(value);
            }
        }
        emit_changed(handle, record);
        return GF_OK;
    }

    gf_result get_string(gf_handle handle, char* buffer, std::uint64_t capacity,
                         std::uint64_t* required_size, bool is_text) {
        if (required_size == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "string read requires a size output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::string& value = is_text ? (*record).text : (*record).name;
        *required_size = value.size();
        if (capacity < value.size() || (value.size() != 0U && buffer == nullptr)) {
            return fail(GF_ERROR_BUFFER_TOO_SMALL,
                        "string buffer is smaller than the required UTF-8 byte count");
        }
        if (!value.empty()) {
            std::memcpy(buffer, value.data(), value.size());
        }
        return GF_OK;
    }

    gf_result set_enabled(gf_handle handle, std::uint32_t enabled) {
        if (enabled > 1U) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "enabled must be zero or one");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (*(*record).control).enabled = enabled != 0U;
        emit_changed(handle, record);
        return GF_OK;
    }

    gf_result get_enabled(gf_handle handle, std::uint32_t* enabled) {
        if (enabled == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "get_enabled requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        *enabled = (*(*record).control).enabled.read() ? 1U : 0U;
        return GF_OK;
    }

    gf_result set_cursor(gf_handle handle, std::uint32_t cursor_kind) {
        if (cursor_kind > GF_CURSOR_FORBIDDEN) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "cursor kind is outside the ABI 0.19 range");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        if (cursor_kind == GF_CURSOR_INHERIT) {
            (*(*record).control).cursor = std::nullopt;
        } else {
            (*(*record).control).cursor = static_cast<gui_forms::CursorKind>(
                cursor_kind - GF_CURSOR_ARROW);
        }
        return GF_OK;
    }

    gf_result get_cursor(gf_handle handle, std::uint32_t* cursor_kind) {
        if (cursor_kind == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "get_cursor requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::optional<CursorKind> value = (*(*record).control).cursor.read();
        *cursor_kind = value.has_value()
            ? static_cast<std::uint32_t>(*value) +
                  static_cast<std::uint32_t>(GF_CURSOR_ARROW)
            : static_cast<std::uint32_t>(GF_CURSOR_INHERIT);
        return GF_OK;
    }

    gf_result set_auto_scroll_offset(gf_handle handle, gf_point offset) {
        if (!std::isfinite(offset.x) || !std::isfinite(offset.y)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "auto-scroll offset must be finite");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (*(*record).control).set_auto_scroll_offset({offset.x, offset.y});
        return GF_OK;
    }

    gf_result set_auto_scroll(gf_handle handle, std::uint32_t enabled) {
        if (enabled > 1U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "auto-scroll enabled must be zero or one");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::ScrollableControl> scrollable = std::dynamic_pointer_cast<
            gui_forms::ScrollableControl>((*record).control);
        if (!scrollable) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "auto-scroll requires a scrollable control");
        }
        (*scrollable).set_auto_scroll(enabled != 0U);
        return GF_OK;
    }

    gf_result set_auto_scroll_margin(gf_handle handle, gf_size margin) {
        if (!std::isfinite(margin.width) || !std::isfinite(margin.height) ||
            margin.width < 0.0 || margin.height < 0.0) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "auto-scroll margin must be finite and nonnegative");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::ScrollableControl> scrollable = std::dynamic_pointer_cast<
            gui_forms::ScrollableControl>((*record).control);
        if (!scrollable) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "auto-scroll margin requires a scrollable control");
        }
        (*scrollable).set_auto_scroll_margin({margin.width, margin.height});
        return GF_OK;
    }

    gf_result set_auto_scroll_min_size(gf_handle handle, gf_size size) {
        if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
            size.width < 0.0 || size.height < 0.0) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "auto-scroll minimum size must be finite and nonnegative");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::ScrollableControl> scrollable = std::dynamic_pointer_cast<
            gui_forms::ScrollableControl>((*record).control);
        if (!scrollable) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "auto-scroll minimum size requires a scrollable control");
        }
        (*scrollable).set_auto_scroll_min_size({size.width, size.height});
        return GF_OK;
    }

    gf_result set_auto_scroll_position(gf_handle handle, gf_point position) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "auto-scroll position must be finite");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::ScrollableControl> scrollable = std::dynamic_pointer_cast<
            gui_forms::ScrollableControl>((*record).control);
        if (!scrollable) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "auto-scroll position requires a scrollable control");
        }
        (*scrollable).set_auto_scroll_position({position.x, position.y});
        return GF_OK;
    }

    gf_result get_scroll_state(gf_handle handle, gf_scroll_state* state) {
        if (state == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "get_scroll_state requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::ScrollableControl> scrollable = std::dynamic_pointer_cast<
            gui_forms::ScrollableControl>((*record).control);
        if (!scrollable) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "get_scroll_state requires a scrollable control");
        }
        bool inside_managed_callback = false;
        {
            std::scoped_lock lock(mutex_);
            inside_managed_callback =
                (*root_record_locked(record)).managed_callback_depth != 0U;
        }
        // A typed callback observes the state produced by the native input
        // turn.  It must not recursively enter retained layout merely to read
        // the event payload; ordinary reads retain the layout read barrier.
        if (!inside_managed_callback) {
            if ((*scrollable).attached()) {
                static_cast<void>((*scrollable).arranged_bounds());
            } else {
                const Rect requested = (*scrollable).requested_bounds();
                (*scrollable).arrange(
                    {0.0, 0.0, requested.width, requested.height});
            }
        }
        const gui_forms::ScrollSnapshot snapshot = (*scrollable).scroll_snapshot();
        const std::optional<gui_forms::ScrollEvent> last =
            (*scrollable).last_scroll_event();
        *state = {
            snapshot.auto_scroll ? 1U : 0U,
            {snapshot.position.x, snapshot.position.y},
            {snapshot.margin.width, snapshot.margin.height},
            {snapshot.minimum_content_size.width,
             snapshot.minimum_content_size.height},
            {snapshot.display_rectangle.x, snapshot.display_rectangle.y,
             snapshot.display_rectangle.width,
             snapshot.display_rectangle.height},
            {snapshot.viewport_rectangle.x, snapshot.viewport_rectangle.y,
             snapshot.viewport_rectangle.width,
             snapshot.viewport_rectangle.height},
            abi_scroll_axis_state(snapshot.horizontal),
            abi_scroll_axis_state(snapshot.vertical),
            (*scrollable).scroll_event_revision(),
            last ? static_cast<std::uint32_t>((*last).type) : 0U,
            last ? static_cast<std::uint32_t>((*last).orientation) : 0U,
            last ? (*last).old_value : 0.0,
            last ? (*last).new_value : 0.0};
        return GF_OK;
    }

    gf_result set_scroll_axis_state(gf_handle handle,
                                    std::uint32_t orientation,
                                    gf_scroll_axis_state state) {
        if (orientation > 1U || state.enabled > 1U || state.visible > 1U ||
            !std::isfinite(state.minimum) || !std::isfinite(state.maximum) ||
            !std::isfinite(state.large_change) ||
            !std::isfinite(state.small_change) || !std::isfinite(state.value) ||
            state.minimum < 0.0 || state.large_change < 0.0 ||
            state.small_change < 0.0 || state.value < state.minimum ||
            state.value > state.maximum) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "scroll axis state is invalid");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::ScrollableControl> scrollable = std::dynamic_pointer_cast<
            gui_forms::ScrollableControl>((*record).control);
        if (!scrollable) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "scroll axis state requires a scrollable control");
        }
        gui_forms::ScrollProperties& axis = orientation == 0U
            ? (*scrollable).horizontal_scroll() : (*scrollable).vertical_scroll();
        axis.set_minimum(state.minimum);
        axis.set_maximum(state.maximum);
        axis.set_large_change(state.large_change);
        axis.set_small_change(state.small_change);
        axis.set_enabled(state.enabled != 0U);
        axis.set_visible(state.visible != 0U);
        axis.set_value(state.value);
        return GF_OK;
    }

    gf_result scroll_control_into_view(gf_handle handle, gf_handle child_handle) {
        std::shared_ptr<ControlRecord> record;
        std::shared_ptr<ControlRecord> child;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        if (const gf_result result = get_control(child_handle, child);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::ScrollableControl> scrollable = std::dynamic_pointer_cast<
            gui_forms::ScrollableControl>((*record).control);
        if (!scrollable) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "ScrollControlIntoView requires a scrollable control");
        }
        if ((*scrollable).attached()) {
            static_cast<void>((*scrollable).arranged_bounds());
        } else {
            const Rect requested = (*scrollable).requested_bounds();
            (*scrollable).arrange(
                {0.0, 0.0, requested.width, requested.height});
        }
        (*scrollable).scroll_control_into_view((*child).control);
        return GF_OK;
    }

    gf_result suspend_layout(gf_handle handle) {
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (*(*record).control).suspend_layout();
        return GF_OK;
    }

    gf_result resume_layout(gf_handle handle, std::uint32_t perform_layout) {
        if (perform_layout > 1U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "resume_layout requires a Boolean perform flag");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (*(*record).control).resume_layout(perform_layout != 0U);
        return GF_OK;
    }

    gf_result perform_control_layout(gf_handle handle) {
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (*(*record).control).perform_layout();
        return GF_OK;
    }

    gf_result get_layout_state(gf_handle handle, gf_layout_state* state) {
        if (state == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "get_layout_state requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const gui_forms::LayoutTransactionState snapshot =
            (*(*record).control).layout_transaction_state();
        *state = {snapshot.suspend_depth, snapshot.deferred ? 1U : 0U,
                  snapshot.requested_revision, snapshot.committed_revision};
        return GF_OK;
    }

    gf_result property_grid_set_selected_controls(
        gf_handle grid_handle, const gf_handle* handles, std::uint64_t count) {
        if (count > 1024U || (count != 0U && handles == nullptr) ||
            count > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid selection requires at most 1024 handles");
        }
        std::shared_ptr<ControlRecord> grid_record;
        if (const gf_result result = get_control(grid_handle, grid_record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::PropertyGrid> grid = std::dynamic_pointer_cast<gui_forms::PropertyGrid>(
            (*grid_record).control);
        if (!grid) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "PropertyGrid selection requires a PropertyGrid handle");
        }
        std::vector<Control::Ptr> controls;
        controls.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t index = 0U; index < count; ++index) {
            std::shared_ptr<ControlRecord> candidate;
            if (const gf_result result = get_control(handles[index], candidate);
                result != GF_OK) {
                return result;
            }
            if ((*candidate).ui_thread != (*grid_record).ui_thread) {
                return fail(GF_ERROR_WRONG_THREAD,
                            "PropertyGrid selections must share one UI thread");
            }
            controls.push_back((*candidate).control);
        }
        const std::vector<Control::Ptr> previous = (*grid).selected_objects();
        const std::shared_ptr<PropertyValueConverterRegistry> previous_converters = (*grid).converter_registry();
        const std::shared_ptr<PropertyEditorRegistry> previous_editors = (*grid).editor_registry();
        std::shared_ptr<PropertyValueConverterRegistry> converters =
            gui_forms::PropertyValueConverterRegistry::create_default();
        std::shared_ptr<PropertyEditorRegistry> editors = gui_forms::PropertyEditorRegistry::create_default();
        std::set<std::string> installed_editor_names;
        for (const Control::Ptr& control : controls) {
            if (const std::shared_ptr<gui_forms::abi::detail::AbiPropertyObjectControl> proxy =
                    std::dynamic_pointer_cast<AbiPropertyObjectControl>(control)) {
                (*proxy).install_converters(*converters);
                (*proxy).install_editors(*editors, installed_editor_names);
            }
        }
        try {
            (*grid).set_converter_registry(converters);
            (*grid).set_editor_registry(editors);
            (*grid).set_selected_objects(std::move(controls));
        } catch (...) {
            try {
                (*grid).set_converter_registry(previous_converters);
                (*grid).set_editor_registry(previous_editors);
                (*grid).set_selected_objects(previous);
            } catch (...) {
                throw std::runtime_error(
                    "PropertyGrid selection failed and its previous projection could not be restored");
            }
            throw;
        }
        emit_changed(grid_handle, grid_record);
        return GF_OK;
    }

    gf_result property_object_define(
        gf_handle handle, const gf_property_descriptor_v1* descriptor,
        const gf_property_callbacks_v1* callbacks) {
        if (descriptor == nullptr || callbacks == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "property_object_define requires descriptor and callback records");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::AbiPropertyObjectControl> proxy =
            std::dynamic_pointer_cast<AbiPropertyObjectControl>((*record).control);
        if (!proxy) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "property definitions require a property-object proxy");
        }
        (*proxy).define(*descriptor, *callbacks);
        return GF_OK;
    }

    gf_result property_object_notify_changed(
        gf_handle handle, gf_string_view property_name) {
        if ((property_name.size != 0U && property_name.data == nullptr) ||
            property_name.size > 256U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "property change notification requires a bounded name");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::AbiPropertyObjectControl> proxy =
            std::dynamic_pointer_cast<AbiPropertyObjectControl>((*record).control);
        if (!proxy) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "property change notification requires a property-object proxy");
        }
        (*proxy).notify_changed(std::string_view(
            property_name.data, static_cast<std::size_t>(property_name.size)));
        return GF_OK;
    }

    gf_result property_grid_try_set_text(
        gf_handle handle, gf_string_view property_name,
        gf_string_view text_value, std::uint32_t* committed) {
        if (committed == nullptr || property_name.data == nullptr ||
            property_name.size == 0U || property_name.size > 256U ||
            (text_value.size != 0U && text_value.data == nullptr) ||
            text_value.size > 1024ULL * 1024ULL) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid text commits require bounded name, text, and output");
        }
        const std::string name(property_name.data,
                               static_cast<std::size_t>(property_name.size));
        std::string text;
        if (text_value.size != 0U) {
            text.assign(text_value.data,
                        static_cast<std::size_t>(text_value.size));
        }
        if (name.find('\0') != std::string::npos ||
            text.find('\0') != std::string::npos ||
            !gui_forms::validate_utf8(name).valid() ||
            !gui_forms::validate_utf8(text).valid()) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid text commits require valid UTF-8 without NUL");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::PropertyGrid> grid = std::dynamic_pointer_cast<gui_forms::PropertyGrid>(
            (*record).control);
        if (!grid) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "property text commits require a PropertyGrid handle");
        }
        *committed = (*grid).try_set_property_text(name, text) ? 1U : 0U;
        return GF_OK;
    }

    gf_result property_grid_reset_property(
        gf_handle handle, gf_string_view property_name,
        std::uint32_t* committed) {
        if (committed == nullptr || property_name.data == nullptr ||
            property_name.size == 0U || property_name.size > 256U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid reset requires a bounded name and output");
        }
        const std::string name(property_name.data,
                               static_cast<std::size_t>(property_name.size));
        if (name.find('\0') != std::string::npos ||
            !gui_forms::validate_utf8(name).valid()) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid reset names require valid UTF-8 without NUL");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::PropertyGrid> grid = std::dynamic_pointer_cast<gui_forms::PropertyGrid>(
            (*record).control);
        if (!grid) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "property reset requires a PropertyGrid handle");
        }
        *committed = (*grid).reset_property(name) ? 1U : 0U;
        return GF_OK;
    }

    gf_result property_grid_activate_editor(
        gf_handle handle, gf_string_view property_name,
        std::uint32_t* activated) {
        if (activated == nullptr || property_name.data == nullptr ||
            property_name.size == 0U || property_name.size > 256U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid editor activation requires a bounded name and output");
        }
        const std::string name(property_name.data,
                               static_cast<std::size_t>(property_name.size));
        if (name.find('\0') != std::string::npos ||
            !gui_forms::validate_utf8(name).valid()) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid editor names require valid UTF-8 without NUL");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::PropertyGrid> grid = std::dynamic_pointer_cast<gui_forms::PropertyGrid>(
            (*record).control);
        if (!grid) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "property editor activation requires a PropertyGrid handle");
        }
        *activated = (*grid).activate_property_editor(name) ? 1U : 0U;
        return GF_OK;
    }

    gf_result property_grid_set_sort(gf_handle grid_handle,
                                     std::uint32_t property_sort) {
        if (property_sort > 3U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid sort is outside the Forms flag range");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(grid_handle, record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::PropertyGrid> grid = std::dynamic_pointer_cast<gui_forms::PropertyGrid>(
            (*record).control);
        if (!grid) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "PropertyGrid sort requires a PropertyGrid handle");
        }
        (*grid).set_property_sort(property_sort == 1U
            ? gui_forms::PropertySort::alphabetical
            : gui_forms::PropertySort::categorized);
        emit_changed(grid_handle, record);
        return GF_OK;
    }

    gf_result property_grid_get_sort(gf_handle grid_handle,
                                     std::uint32_t* property_sort) {
        if (property_sort == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "PropertyGrid sort read requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(grid_handle, record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::PropertyGrid> grid = std::dynamic_pointer_cast<gui_forms::PropertyGrid>(
            (*record).control);
        if (!grid) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "PropertyGrid sort requires a PropertyGrid handle");
        }
        *property_sort = (*grid).property_sort() ==
                gui_forms::PropertySort::alphabetical
            ? 1U : 3U;
        return GF_OK;
    }

    gf_result property_grid_refresh(gf_handle grid_handle) {
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(grid_handle, record);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::PropertyGrid> grid = std::dynamic_pointer_cast<gui_forms::PropertyGrid>(
            (*record).control);
        if (!grid) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "PropertyGrid refresh requires a PropertyGrid handle");
        }
        (*grid).refresh_properties();
        return GF_OK;
    }

    gf_result set_control_png(gf_handle handle, const std::uint8_t* encoded,
                              std::uint64_t encoded_size) {
        if ((encoded_size != 0U && encoded == nullptr) ||
            encoded_size > 32ULL * 1024ULL * 1024ULL ||
            encoded_size > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "set_control_png requires a bounded PNG byte span");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::RasterControl> raster = std::dynamic_pointer_cast<RasterControl>((*record).control);
        if (!raster) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "set_control_png requires a custom raster control");
        }
        const std::span<const std::byte> bytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(encoded),
            static_cast<std::size_t>(encoded_size));
        if (!(*raster).set_png(bytes)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "set_control_png rejected an invalid or unbounded PNG");
        }
        // Raster replacement is renderer state, not a managed property change.
        // Emitting the generic state callback here can also re-enter a managed
        // UnmanagedCallersOnly thunk when paint was requested by an ABI dispatch.
        return GF_OK;
    }

    gf_result set_control_pixels(gf_handle handle, const std::uint8_t* pixels,
                                 std::uint32_t width, std::uint32_t height,
                                 std::uint64_t row_bytes,
                                 std::uint32_t pixel_format) {
        if (pixel_format != GF_PIXEL_FORMAT_BGRA32_PREMULTIPLIED) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "set_control_pixels requires BGRA32 premultiplied pixels");
        }
        if ((width == 0U || height == 0U) && pixels == nullptr) {
            std::shared_ptr<ControlRecord> record;
            if (const gf_result result = get_control(handle, record); result != GF_OK) {
                return result;
            }
            const std::shared_ptr<gui_forms::abi::detail::RasterControl> raster = std::dynamic_pointer_cast<RasterControl>((*record).control);
            return raster && (*raster).set_bgra32_premultiplied(0, 0, 0, {})
                ? GF_OK
                : fail(GF_ERROR_WRONG_HANDLE_KIND,
                       "set_control_pixels requires a custom raster control");
        }
        if (pixels == nullptr || width == 0U || height == 0U ||
            row_bytes < static_cast<std::uint64_t>(width) * 4U ||
            row_bytes > std::numeric_limits<std::size_t>::max() ||
            height > std::numeric_limits<std::size_t>::max() / row_bytes) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "set_control_pixels requires a bounded BGRA32 surface");
        }
        const std::size_t byte_count =
            static_cast<std::size_t>(row_bytes) * height;
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::RasterControl> raster = std::dynamic_pointer_cast<RasterControl>((*record).control);
        if (!raster) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "set_control_pixels requires a custom raster control");
        }
        const std::span<const std::byte> bytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(pixels), byte_count);
        if (!(*raster).set_bgra32_premultiplied(width, height, row_bytes, bytes)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "set_control_pixels rejected an invalid or unbounded surface");
        }
        return GF_OK;
    }

    gf_result compatibility_paint_target(
        gf_handle handle,
        std::weak_ptr<RasterControl>* target,
        std::thread::id* owner_thread) {
        if (target == nullptr || owner_thread == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "compatibility paint target requires outputs");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::RasterControl> raster = std::dynamic_pointer_cast<RasterControl>((*record).control);
        if (!raster) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "compatibility paint endpoint requires a raster control");
        }
        *target = raster;
        *owner_thread = (*record).ui_thread;
        return GF_OK;
    }

    gf_result set_child_index(gf_handle parent_handle, gf_handle child_handle,
                              std::uint64_t index) {
        if (index > static_cast<std::uint64_t>(
                        std::numeric_limits<std::size_t>::max())) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "set_child_index index exceeds the platform bound");
        }
        std::shared_ptr<ControlRecord> parent;
        std::shared_ptr<ControlRecord> child;
        if (const gf_result result = get_control(parent_handle, parent);
            result != GF_OK) {
            return result;
        }
        if (const gf_result result = get_control(child_handle, child);
            result != GF_OK) {
            return result;
        }
        if (!(*(*parent).control).set_child_index(
                (*(*child).control).runtime_id(), static_cast<std::size_t>(index))) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "set_child_index child is not owned by parent");
        }
        // Reordering is initiated and already observed by the managed owner.
        return GF_OK;
    }

    gf_result set_control_colors(gf_handle handle, std::uint32_t foreground_argb,
                                 std::uint32_t background_argb) {
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const gui_forms::Color foreground = color_from_argb(foreground_argb);
        const gui_forms::Color background = color_from_argb(background_argb);
        const Color disabled = gui_forms::Color::rgba(
            disabled_color_channel(foreground.red, background.red),
            disabled_color_channel(foreground.green, background.green),
            disabled_color_channel(foreground.blue, background.blue),
            foreground.alpha);
        if (const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*record).control)) {
            (*field).set_colors(foreground, background);
        } else if (const std::shared_ptr<gui_forms::Label> label =
                       std::dynamic_pointer_cast<gui_forms::Label>((*record).control)) {
            (*label).set_foreground(foreground);
        } else if (const std::shared_ptr<gui_forms::ButtonBase> button =
                       std::dynamic_pointer_cast<gui_forms::ButtonBase>((*record).control)) {
            BasicControlStyle style = (*button).style();
            style.text = foreground;
            style.disabled_text = disabled;
            style.face = background;
            style.face_light = background;
            (*button).set_style(style);
        } else if (const std::shared_ptr<gui_forms::Panel> panel =
                       std::dynamic_pointer_cast<gui_forms::Panel>((*record).control)) {
            BasicControlStyle style = (*panel).style();
            style.text = foreground;
            style.disabled_text = disabled;
            (*panel).set_style(style);
            (*panel).set_background(background);
        }
        // Style projection does not mutate a WinForms-observable property. The
        // managed side already owns and has raised the corresponding change.
        return GF_OK;
    }

    gf_result set_control_text_alignment(gf_handle handle,
                                         std::uint32_t content_alignment) {
        if (content_alignment > 8U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "content alignment must be in the compact 0..8 range");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const gui_forms::HorizontalAlignment horizontal = static_cast<gui_forms::HorizontalAlignment>(
            content_alignment % 3U);
        const gui_forms::VerticalAlignment vertical = static_cast<gui_forms::VerticalAlignment>(
            content_alignment / 3U);
        if (const std::shared_ptr<gui_forms::Label> label =
                std::dynamic_pointer_cast<gui_forms::Label>((*record).control)) {
            (*label).set_alignment(horizontal);
            (*label).set_vertical_alignment(vertical);
            return GF_OK;
        }
        if (const std::shared_ptr<gui_forms::ButtonBase> button =
                std::dynamic_pointer_cast<gui_forms::ButtonBase>((*record).control)) {
            (*button).set_text_alignment(
                static_cast<gui_forms::ContentAlignment>(content_alignment));
            return GF_OK;
        }
        return fail(GF_ERROR_WRONG_HANDLE_KIND,
                    "text alignment requires a retained label or button");
    }

    gf_result set_button_appearance(gf_handle handle, std::uint32_t visual_style,
                                    double flat_border_width) {
        if (visual_style >
                static_cast<std::uint32_t>(gui_forms::ButtonVisualStyle::command) ||
            !std::isfinite(flat_border_width) || flat_border_width < 0.0) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "button appearance is outside its retained range");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::Button> button =
            std::dynamic_pointer_cast<gui_forms::Button>((*record).control);
        if (!button) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "button appearance requires a retained push button");
        }
        (*button).set_visual_style(
            static_cast<gui_forms::ButtonVisualStyle>(visual_style));
        (*button).set_flat_border_width(flat_border_width);
        return GF_OK;
    }

    gf_result set_panel_border_style(gf_handle handle,
                                     std::uint32_t border_style) {
        if (border_style > 2U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "panel border style must be None, FixedSingle, or Fixed3D");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::Panel> panel =
            std::dynamic_pointer_cast<gui_forms::Panel>((*record).control);
        if (!panel) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "panel border style requires a retained panel");
        }
        const gui_forms::BorderStyle native_style = border_style == 0U
            ? gui_forms::BorderStyle::none
            : border_style == 1U ? gui_forms::BorderStyle::line
                                 : gui_forms::BorderStyle::sunken;
        (*panel).set_border_style(native_style);
        return GF_OK;
    }

    gf_result set_field_selection(gf_handle handle, std::uint64_t start,
                                  std::uint64_t length,
                                  std::uint32_t caret_visible) {
        if (caret_visible > 1U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "caret visibility must be zero or one");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*record).control);
        if (!field) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "field selection requires a retained field control");
        }
        if (!(*field).set_selection(start, length, caret_visible != 0U)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field selection exceeds the UTF-8 text extent");
        }
        return GF_OK;
    }

    gf_result set_field_edit_state(gf_handle handle, std::uint64_t anchor,
                                   std::uint64_t caret,
                                   std::uint32_t caret_visible) {
        if (caret_visible > 1U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "caret visibility must be zero or one");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*record).control);
        if (!field) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "field edit state requires a retained field control");
        }
        if (!(*field).set_edit_state(anchor, caret, caret_visible != 0U)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field anchor and caret must be UTF-8 grapheme boundaries");
        }
        return GF_OK;
    }

    gf_result field_position_from_point(gf_handle handle, double local_x,
                                        std::uint64_t* position) {
        if (position == nullptr || !std::isfinite(local_x)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field hit test requires a finite point and output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*record).control);
        if (!field) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "field hit test requires a retained field control");
        }
        *position = (*field).position_at(local_x);
        return GF_OK;
    }

    gf_result write_clipboard_text(gf_handle owner_handle, gf_string_view input) {
        std::string value;
        if (!copy_view(input, value) ||
            value.size() > gui_forms::HostServices::maximum_clipboard_text_bytes ||
            !gui_forms::validate_utf8(value).valid()) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "clipboard text must be bounded valid UTF-8 without NUL");
        }
        std::function<gui_forms::HostServiceStatus(std::string_view)> provider;
        {
            std::scoped_lock lock(mutex_);
            std::shared_ptr<ControlRecord> owner;
            if (const gf_result result = control_locked(owner_handle, owner);
                result != GF_OK) return result;
            if (const gf_result result = require_thread(*owner); result != GF_OK) {
                return result;
            }
            const std::shared_ptr<ControlRecord> root = root_record_locked(owner);
            if (!root) return fail(GF_ERROR_INVALID_ARGUMENT,
                                   "clipboard owner has no retained root");
            provider = (*root).host_clipboard_write;
            if (!provider) {
                clipboard_text_ = value;
                ++clipboard_generation_;
                return GF_OK;
            }
        }
        const gui_forms::HostServiceStatus status = provider(value);
        if (!status.accepted()) {
            return fail(GF_ERROR_INTERNAL,
                        std::string("clipboard host write failed: ") +
                        gui_forms::host_service_error_name(status.error));
        }
        return GF_OK;
    }

    gf_result read_clipboard_text(gf_handle owner_handle, char* buffer,
                                  std::uint64_t capacity,
                                  std::uint64_t* required_size,
                                  std::uint32_t* has_text) {
        if (required_size == nullptr || has_text == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "clipboard read requires size and presence outputs");
        }
        std::function<gui_forms::HostClipboardTextResult()> provider;
        std::string value;
        bool present{};
        {
            std::scoped_lock lock(mutex_);
            std::shared_ptr<ControlRecord> owner;
            if (const gf_result result = control_locked(owner_handle, owner);
                result != GF_OK) return result;
            if (const gf_result result = require_thread(*owner); result != GF_OK) {
                return result;
            }
            const std::shared_ptr<ControlRecord> root = root_record_locked(owner);
            if (!root) return fail(GF_ERROR_INVALID_ARGUMENT,
                                   "clipboard owner has no retained root");
            provider = (*root).host_clipboard_read;
            if (!provider) {
                value = clipboard_text_;
                present = clipboard_generation_ != 0U;
            }
        }
        if (provider) {
            const gui_forms::HostClipboardTextResult result = provider();
            if (!result.status.accepted()) {
                return fail(GF_ERROR_INTERNAL,
                            std::string("clipboard host read failed: ") +
                            gui_forms::host_service_error_name(result.status.error));
            }
            value = result.text_utf8;
            present = result.has_text;
        }
        *required_size = value.size();
        *has_text = present ? 1U : 0U;
        if (capacity < value.size() || (value.size() != 0U && buffer == nullptr)) {
            return fail(GF_ERROR_BUFFER_TOO_SMALL,
                        "clipboard buffer is smaller than the required UTF-8 size");
        }
        if (!value.empty()) std::memcpy(buffer, value.data(), value.size());
        return GF_OK;
    }

    gf_result field_navigate(gf_handle handle, std::uint64_t position,
                             std::int32_t direction, std::uint64_t* result) {
        if (result == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field navigation requires an output position");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result status = get_control(handle, record); status != GF_OK) {
            return status;
        }
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*record).control);
        if (!field) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "field navigation requires a retained field control");
        }
        if (!(*field).navigate(position, direction, *result)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field navigation requires a grapheme boundary and -1/+1");
        }
        return GF_OK;
    }

    gf_result field_replace(gf_handle handle, std::uint64_t start,
                            std::uint64_t length, gf_string_view input,
                            gf_field_edit_result* result) {
        if (result == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field replacement requires an edit result output");
        }
        std::string replacement;
        if (!copy_view(input, replacement) ||
            !gui_forms::validate_utf8(replacement).valid()) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field replacement must be bounded valid UTF-8 without NUL");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result status = get_control(handle, record); status != GF_OK) {
            return status;
        }
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*record).control);
        if (!field) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "field replacement requires a retained field control");
        }
        if (!(*field).replace(start, length, replacement, *result)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field replacement range must use UTF-8 grapheme boundaries");
        }
        (*record).text.assign((*field).text());
        return GF_OK;
    }

    gf_result field_history(gf_handle handle, std::int32_t direction,
                            gf_field_edit_result* result) {
        if (result == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field history requires an edit result output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result status = get_control(handle, record); status != GF_OK) {
            return status;
        }
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*record).control);
        if (!field) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "field history requires a retained field control");
        }
        if (!(*field).history(direction, *result)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "field history direction must be -1 (undo) or +1 (redo)");
        }
        (*record).text.assign((*field).text());
        return GF_OK;
    }

    gf_result field_clear_history(gf_handle handle) {
        std::shared_ptr<ControlRecord> record;
        if (const gf_result status = get_control(handle, record); status != GF_OK) {
            return status;
        }
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*record).control);
        if (!field) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "field history reset requires a retained field control");
        }
        (*field).clear_history();
        return GF_OK;
    }

    gf_result set_check_state(gf_handle handle, std::uint32_t check_state) {
        if (check_state > static_cast<std::uint32_t>(gui_forms::CheckState::indeterminate)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "check state must be unchecked, checked, or indeterminate");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        if (const std::shared_ptr<gui_forms::CheckBox> check_box =
                std::dynamic_pointer_cast<gui_forms::CheckBox>((*record).control)) {
            (*check_box).set_check_state(
                static_cast<gui_forms::CheckState>(check_state));
            return GF_OK;
        }
        if (const std::shared_ptr<gui_forms::RadioButton> radio =
                std::dynamic_pointer_cast<gui_forms::RadioButton>((*record).control)) {
            if (check_state ==
                static_cast<std::uint32_t>(gui_forms::CheckState::indeterminate)) {
                return fail(GF_ERROR_INVALID_ARGUMENT,
                            "radio buttons do not support indeterminate state");
            }
            (*radio).set_checked(
                check_state == static_cast<std::uint32_t>(gui_forms::CheckState::checked));
            return GF_OK;
        }
        return fail(GF_ERROR_WRONG_HANDLE_KIND,
                    "check state requires a checkbox or radio button");
    }

    gf_result get_check_state(gf_handle handle, std::uint32_t* check_state) {
        if (check_state == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "get_check_state requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        if (const std::shared_ptr<gui_forms::CheckBox> check_box =
                std::dynamic_pointer_cast<gui_forms::CheckBox>((*record).control)) {
            *check_state = static_cast<std::uint32_t>((*check_box).check_state());
            return GF_OK;
        }
        if (const std::shared_ptr<gui_forms::RadioButton> radio =
                std::dynamic_pointer_cast<gui_forms::RadioButton>((*record).control)) {
            *check_state = (*radio).checked()
                ? static_cast<std::uint32_t>(gui_forms::CheckState::checked)
                : static_cast<std::uint32_t>(gui_forms::CheckState::unchecked);
            return GF_OK;
        }
        return fail(GF_ERROR_WRONG_HANDLE_KIND,
                    "check state requires a checkbox or radio button");
    }

    gf_result set_range(gf_handle handle, double minimum, double maximum) {
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::RangeControl> range =
            std::dynamic_pointer_cast<gui_forms::RangeControl>((*record).control);
        if (!range) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "range state requires a track bar or progress bar");
        }
        (*range).set_range(minimum, maximum);
        return GF_OK;
    }

    gf_result get_range(gf_handle handle, double* minimum, double* maximum) {
        if (minimum == nullptr || maximum == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "get_range requires minimum and maximum outputs");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::RangeControl> range =
            std::dynamic_pointer_cast<gui_forms::RangeControl>((*record).control);
        if (!range) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "range state requires a track bar or progress bar");
        }
        *minimum = (*range).minimum();
        *maximum = (*range).maximum();
        return GF_OK;
    }

    gf_result set_range_value(gf_handle handle, double value) {
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::RangeControl> range =
            std::dynamic_pointer_cast<gui_forms::RangeControl>((*record).control);
        if (!range) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "range value requires a track bar or progress bar");
        }
        (*range).set_value(value);
        return GF_OK;
    }

    gf_result get_range_value(gf_handle handle, double* value) {
        if (value == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "get_range_value requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::RangeControl> range =
            std::dynamic_pointer_cast<gui_forms::RangeControl>((*record).control);
        if (!range) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "range value requires a track bar or progress bar");
        }
        *value = (*range).value();
        return GF_OK;
    }

    gf_result set_pointer_capture(gf_handle handle, std::uint32_t captured) {
        if (captured > 1U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "pointer capture must be zero or one");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (*(*record).control).set_pointer_capture(captured != 0U);
        return GF_OK;
    }

    gf_result get_pointer_capture(gf_handle handle, std::uint32_t* captured) {
        if (captured == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "get_pointer_capture requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        *captured = (*(*record).control).has_pointer_capture() ? 1U : 0U;
        return GF_OK;
    }

    gf_result show_path_dialog(gf_handle owner_handle, std::uint32_t kind,
                               gf_string_view title,
                               gf_string_view initial_directory,
                               gf_string_view suggested_name,
                               gf_string_view default_extension,
                               gf_string_view filter,
                               std::uint32_t flags,
                               std::uint32_t* accepted) {
        constexpr std::uint32_t known_flags =
            GF_PATH_DIALOG_ALLOW_MULTIPLE | GF_PATH_DIALOG_CONFIRM_OVERWRITE;
        if (accepted == nullptr ||
            (kind != GF_PATH_DIALOG_OPEN_FILE &&
             kind != GF_PATH_DIALOG_SAVE_FILE &&
             kind != GF_PATH_DIALOG_SELECT_FOLDER) ||
            (flags & ~known_flags) != 0U) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "show_path_dialog received an invalid kind, flag, or output");
        }
        std::string title_value;
        std::string directory_value;
        std::string suggested_value;
        std::string extension_value;
        std::string filter_value;
        if (!copy_view(title, title_value) ||
            !copy_view(initial_directory, directory_value) ||
            !copy_view(suggested_name, suggested_value) ||
            !copy_view(default_extension, extension_value) ||
            !copy_view(filter, filter_value)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "path-dialog strings must be bounded UTF-8 views without NUL");
        }

        std::shared_ptr<ControlRecord> owner;
        std::shared_ptr<ControlRecord> root;
        std::function<gui_forms::HostDialogResult(
            const gui_forms::HostDialogRequest&)> provider;
        {
            std::scoped_lock lock(mutex_);
            if (const gf_result result = control_locked(owner_handle, owner);
                result != GF_OK) return result;
            if (const gf_result result = require_thread(*owner); result != GF_OK) {
                return result;
            }
            root = root_record_locked(owner);
            if (!root) {
                return fail(GF_ERROR_INVALID_ARGUMENT,
                            "path-dialog owner has no retained root");
            }
            (*root).last_dialog_path.clear();
            provider = (*root).host_dialog;
        }

        gui_forms::HostDialogRequest request;
        request.request_id = next_dialog_request_++;
        request.owner_id = std::string((*(*root).control).stable_id().value());
        if (kind == GF_PATH_DIALOG_OPEN_FILE) {
            gui_forms::HostOpenFileDialogRequest payload;
            payload.title = std::move(title_value);
            payload.initial_directory = std::move(directory_value);
            payload.suggested_name = std::move(suggested_value);
            payload.filters = parse_filters(filter_value);
            payload.allow_multiple =
                (flags & GF_PATH_DIALOG_ALLOW_MULTIPLE) != 0U;
            request.payload = std::move(payload);
        } else if (kind == GF_PATH_DIALOG_SAVE_FILE) {
            gui_forms::HostSaveFileDialogRequest payload;
            payload.title = std::move(title_value);
            payload.initial_directory = std::move(directory_value);
            payload.suggested_name = std::move(suggested_value);
            payload.default_extension = std::move(extension_value);
            payload.filters = parse_filters(filter_value);
            payload.confirm_overwrite =
                (flags & GF_PATH_DIALOG_CONFIRM_OVERWRITE) != 0U;
            request.payload = std::move(payload);
        } else {
            request.payload = gui_forms::HostFolderDialogRequest{
                std::move(title_value), std::move(directory_value)};
        }

        *accepted = 0U;
        if (!provider) return GF_OK;
        const gui_forms::HostDialogResult result = provider(request);
        if (!result.status.accepted()) {
            return fail(GF_ERROR_INTERNAL,
                        std::string("path-dialog host failed: ") +
                        gui_forms::host_service_error_name(result.status.error));
        }
        const gui_forms::HostPathDialogResult* paths =
            std::get_if<gui_forms::HostPathDialogResult>(&result.payload);
        if (paths == nullptr) {
            return fail(GF_ERROR_INTERNAL,
                        "path-dialog host returned the wrong result payload");
        }
        if ((*paths).outcome == gui_forms::HostDialogOutcome::accepted &&
            !(*paths).paths.empty()) {
            std::scoped_lock lock(mutex_);
            (*root).last_dialog_path = (*paths).paths.front();
            *accepted = 1U;
        }
        return GF_OK;
    }

    gf_result last_dialog_path(gf_handle owner_handle, char* buffer,
                               std::uint64_t capacity,
                               std::uint64_t* required_size) {
        if (required_size == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "last_dialog_path requires a size output");
        }
        std::shared_ptr<ControlRecord> owner;
        std::string value;
        {
            std::scoped_lock lock(mutex_);
            if (const gf_result result = control_locked(owner_handle, owner);
                result != GF_OK) return result;
            if (const gf_result result = require_thread(*owner); result != GF_OK) {
                return result;
            }
            const std::shared_ptr<ControlRecord> root = root_record_locked(owner);
            if (!root) return fail(GF_ERROR_INVALID_ARGUMENT,
                                   "dialog owner has no retained root");
            value = (*root).last_dialog_path;
        }
        *required_size = value.size();
        if (capacity < value.size() || (value.size() != 0U && buffer == nullptr)) {
            return fail(GF_ERROR_BUFFER_TOO_SMALL,
                        "dialog path buffer is smaller than the required UTF-8 size");
        }
        if (!value.empty()) std::memcpy(buffer, value.data(), value.size());
        return GF_OK;
    }

    gf_result show_tooltip(gf_handle owner_handle, gf_string_view text,
                           double x, double y,
                           std::uint32_t duration_milliseconds) {
        if (!std::isfinite(x) || !std::isfinite(y)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "tooltip anchor must be finite");
        }
        std::string text_value;
        if (!copy_view(text, text_value)) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "tooltip text must be a bounded UTF-8 view without NUL");
        }
        std::shared_ptr<ControlRecord> owner;
        std::function<gui_forms::HostServiceStatus(
            const gui_forms::HostTooltipRequest&)> provider;
        gui_forms::Point anchor;
        {
            std::scoped_lock lock(mutex_);
            if (const gf_result result = control_locked(owner_handle, owner);
                result != GF_OK) return result;
            if (const gf_result result = require_thread(*owner); result != GF_OK) {
                return result;
            }
            const std::shared_ptr<ControlRecord> root = root_record_locked(owner);
            if (!root) return fail(GF_ERROR_INVALID_ARGUMENT,
                                   "tooltip owner has no retained root");
            provider = (*root).host_tooltip_show;
            const Rect bounds = (*(*owner).control).absolute_bounds();
            anchor = {bounds.x + x, bounds.y + y};
        }
        if (!provider) return GF_OK;
        const gui_forms::HostServiceStatus result = provider(
            {std::move(text_value), anchor, duration_milliseconds});
        return result.accepted() ? GF_OK :
            fail(GF_ERROR_INTERNAL,
                 std::string("tooltip host failed: ") +
                 gui_forms::host_service_error_name(result.error));
    }

    gf_result hide_tooltip(gf_handle owner_handle) {
        std::function<void()> hide;
        {
            std::scoped_lock lock(mutex_);
            std::shared_ptr<ControlRecord> owner;
            if (const gf_result result = control_locked(owner_handle, owner);
                result != GF_OK) return result;
            if (const gf_result result = require_thread(*owner); result != GF_OK) {
                return result;
            }
            const std::shared_ptr<ControlRecord> root = root_record_locked(owner);
            if (!root) return fail(GF_ERROR_INVALID_ARGUMENT,
                                   "tooltip owner has no retained root");
            hide = (*root).host_tooltip_hide;
        }
        if (hide) hide();
        return GF_OK;
    }

    gf_result run_window(gf_handle handle, std::uint32_t flags) {
        constexpr std::uint32_t known_flags =
            GF_WINDOW_RUN_AUTOMATION_CLOSE | GF_WINDOW_RUN_FORCE_HEADLESS |
            GF_WINDOW_RUN_AUTOMATION_ACTIVATE | GF_WINDOW_RUN_POPUP;
        if ((flags & ~known_flags) != 0U) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "run_window received unknown flags");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        {
            std::scoped_lock lock(mutex_);
            const bool popup = (flags & GF_WINDOW_RUN_POPUP) != 0U;
            const bool valid_root = (*record).kind == GF_CONTROL_FORM ||
                (popup && (*record).kind == GF_CONTROL_CUSTOM);
            if ((*record).host_phase != AbiHostPhase::idle || (*(*record).control).attached() ||
                (*(*record).control).parent() || !valid_root) {
                return fail(GF_ERROR_INVALID_ARGUMENT,
                            "run_window requires an unattached form or custom popup");
            }
            (*record).host_phase = AbiHostPhase::starting;
            (*record).close_requested = false;
            (*record).callback_faults = 0;
            (*record).dispatches = 0;
            (*record).dispatch_turns = 0;
        }
        const HostFinishGuard reset{this, record};

        Rect requested = (*(*record).control).requested_bounds();
        Size client_size{requested.width > 0.0 ? requested.width : 960.0,
                         requested.height > 0.0 ? requested.height : 640.0};
        std::unique_ptr<gui_forms::Window> model = std::make_unique<Window>((*record).control, client_size);
        const bool auto_close = (flags & GF_WINDOW_RUN_AUTOMATION_CLOSE) != 0U;
        const bool force_headless = (flags & GF_WINDOW_RUN_FORCE_HEADLESS) != 0U;
        const bool auto_activate =
            (flags & GF_WINDOW_RUN_AUTOMATION_ACTIVATE) != 0U;
        const bool popup = (flags & GF_WINDOW_RUN_POPUP) != 0U;
        // These remain part of the renderer-neutral launch contract even when
        // this build contains only the headless host.
        static_cast<void>(auto_close);
        static_cast<void>(popup);
        const std::string title = (*record).text.empty()
            ? std::string("GUI.Forms Managed Surface") : (*record).text;

        if (!force_headless) {
#if defined(GF_C_API_HAS_WINDOWS_HOST)
            gui_forms::host::WindowsHostOptions options;
            options.title = title;
            options.initial_size = client_size;
            options.minimum_size = popup ? Size{40.0, 20.0} : Size{320.0, 200.0};
            options.popup_window = popup;
            options.quit_thread_on_close = !popup;
            options.initial_position = {requested.x, requested.y};
            options.print_metrics_on_close = false;
            options.automation_enabled = true;
            options.close_after_launch_for_testing = auto_close;
            const std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<Control>>> automation_controls = named_controls_snapshot(record);
            options.automation_resolve =
                AutomationResolver{automation_controls};
            options.close_request = CloseRequestCallback{this, handle};
            options.host_ready = HostReadyCallback{this, record};
            options.dispatch_pending = WindowsDispatchPendingCallback{
                this, record, automation_controls};
            options.closed = HostClosedCallback{this, handle, record};
            options.final_snapshot = FinalSnapshotCallback{this, record};
            const int result = gui_forms::host::run_windows(std::move(model),
                                                             std::move(options));
            return result == 0 ? GF_OK :
                fail(GF_ERROR_INTERNAL, "Win32 GUI.Forms host returned a failure");
#elif defined(GF_C_API_HAS_MACOS_HOST)
            gui_forms::host::MacHostOptions options;
            options.title = title;
            options.initial_size = client_size;
            options.minimum_size = {320.0, 200.0};
            options.print_metrics_on_close = false;
            options.close_after_launch_for_testing = auto_close;
            options.close_request = CloseRequestCallback{this, handle};
            options.host_ready = HostReadyCallback{this, record};
            options.dispatch_pending = DispatchPendingCallback{this, record};
            options.closed = HostClosedCallback{this, handle, record};
            options.final_snapshot = FinalSnapshotCallback{this, record};
            const int result = gui_forms::host::run_macos(std::move(model),
                                                           std::move(options));
            return result == 0 ? GF_OK :
                fail(GF_ERROR_INTERNAL, "AppKit GUI.Forms host returned a failure");
#endif
        }

        gui_forms::host::HeadlessHost host(*model);
        static_cast<void>(host.dispatch(gui_forms::HostAttachEvent{client_size, 1.0}, 1));
        {
            std::scoped_lock lock(mutex_);
            (*record).host_phase = AbiHostPhase::ready;
        }
        // One FIFO snapshot is the initialization transaction. Work posted by
        // Load remains a normal next-turn callback, matching native hosts.
        pump_pending(record);
        std::uint64_t timestamp = 3;
        bool close_before_input = close_requested(record);
        if (!close_before_input) {
            static_cast<void>(host.dispatch(gui_forms::HostActivationEvent{true}, 2));
            (*model).flush();
            pump_pending(record);
            close_before_input = close_requested(record);
        }
        if (!close_before_input && auto_activate) {
            std::shared_ptr<Control> target = first_button((*record).control);
            if (!target) target = first_pointer_control((*record).control);
            if (target) {
                const Rect bounds = (*target).absolute_bounds();
                const gui_forms::Point center{
                    bounds.x + bounds.width / 2.0,
                    bounds.y + bounds.height / 2.0};
                static_cast<void>(host.dispatch(
                    gui_forms::PointerEvent{gui_forms::PointerAction::down,
                                            gui_forms::PointerButton::primary,
                                            center, {}, gui_forms::Modifier::none, 1},
                    timestamp++));
                static_cast<void>(host.dispatch(
                    gui_forms::PointerEvent{gui_forms::PointerAction::up,
                                            gui_forms::PointerButton::primary,
                                            center, {}, gui_forms::Modifier::none, 1},
                    timestamp++));
            }
        }
        bool dispatch_quiescent = close_before_input;
        if (!close_before_input) {
            for (std::size_t turn = 0U;
                 turn < gui_forms::maximum_posted_callbacks; ++turn) {
                pump_pending(record);
                std::scoped_lock lock(mutex_);
                dispatch_quiescent = (*record).dispatch_queue.empty();
                if (dispatch_quiescent) break;
            }
        }
        if (!dispatch_quiescent) {
            return fail(GF_ERROR_INTERNAL,
                        "headless runtime queue did not reach quiescence");
        }
        gui_forms::HostCloseRequest close{
            gui_forms::HostCloseReason::application, false};
        close.cancel = emit_v2(handle, GF_EVENT_FORM_CLOSING) ==
                       GF_EVENT_CALLBACK_CANCEL;
        const HostDispatchResult close_result = host.dispatch(close, timestamp++);
        if (close_result.accepted() && close_result.close_allowed) {
            mark_host_stopping(record);
            static_cast<void>(host.dispatch(
                gui_forms::HostClosedEvent{gui_forms::HostCloseReason::application},
                timestamp++));
            static_cast<void>(emit_v2(handle, GF_EVENT_FORM_CLOSED));
        }
        mark_host_stopping(record);
        static_cast<void>(host.dispatch(gui_forms::HostShutdownEvent{}, timestamp));
        (*record).host_trace = host.trace() + "managed=" + managed_trace(record) + "\n";
        return GF_OK;
    }

    gf_result last_host_trace(gf_handle handle, char* buffer,
                              std::uint64_t capacity,
                              std::uint64_t* required_size) {
        if (required_size == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "last_host_trace requires a size output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        *required_size = (*record).host_trace.size();
        if (capacity < (*record).host_trace.size() ||
            (!(*record).host_trace.empty() && buffer == nullptr)) {
            return fail(GF_ERROR_BUFFER_TOO_SMALL,
                        "host trace buffer is smaller than the required UTF-8 byte count");
        }
        if (!(*record).host_trace.empty()) {
            std::memcpy(buffer, (*record).host_trace.data(), (*record).host_trace.size());
        }
        return GF_OK;
    }

    gf_result retain(gf_handle handle) {
        std::scoped_lock lock(mutex_);
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = control_locked(handle, record); result != GF_OK) {
            return result;
        }
        if (const gf_result result = require_thread(*record); result != GF_OK) {
            return result;
        }
        if ((*record).external_references ==
            std::numeric_limits<std::uint64_t>::max()) {
            return fail(GF_ERROR_INTERNAL, "control retain count overflow");
        }
        ++(*record).external_references;
        return GF_OK;
    }

    gf_result release(gf_handle handle) {
        std::vector<gui_forms::SubscriptionToken> revoked;
        {
            std::scoped_lock lock(mutex_);
            std::shared_ptr<ControlRecord> record;
            if (const gf_result result = control_locked(handle, record); result != GF_OK) {
                return result;
            }
            if (const gf_result result = require_thread(*record); result != GF_OK) {
                return result;
            }
            if ((*record).external_references == 0U) {
                return fail(GF_ERROR_STALE_HANDLE, "control handle has no external references");
            }
            --(*record).external_references;
            if ((*record).external_references == 0U) {
                invalidate_control_locked(handle, *record, revoked);
            }
        }
        disconnect_revoked(revoked);
        return GF_OK;
    }

    gf_result dispose(gf_handle handle) {
        std::shared_ptr<ControlRecord> record;
        std::vector<std::shared_ptr<ControlRecord>> subtree_records;
        std::vector<gui_forms::SubscriptionToken> revoked;
        {
            std::scoped_lock lock(mutex_);
            if (const gf_result result = control_locked(handle, record); result != GF_OK) {
                return result;
            }
            if (const gf_result result = require_thread(*record); result != GF_OK) {
                return result;
            }
            // Stale every public identity in the owned visual subtree and
            // revoke callback tokens before user-observable teardown can run.
            for (const RegistrySlot& candidate : slots_) {
                if (candidate.kind == SlotKind::control && candidate.control &&
                    contains_control((*record).control, (*candidate.control).control)) {
                    subtree_records.push_back(candidate.control);
                }
            }
            for (const std::shared_ptr<gui_forms::abi::detail::ControlRecord>& subtree_record : subtree_records) {
                std::vector<RegistrySlot>::iterator found = slots_.begin();
                while (found != slots_.end() &&
                       ((*found).kind != SlotKind::control ||
                        (*found).control != subtree_record)) {
                    ++found;
                }
                if (found != slots_.end()) {
                    const gf_handle subtree_handle{
                        static_cast<std::uint32_t>(
                            std::distance(slots_.begin(), found) + 1),
                        (*found).generation};
                    invalidate_control_locked(subtree_handle, *subtree_record, revoked);
                }
            }
        }
        disconnect_revoked(revoked);
        for (const std::shared_ptr<gui_forms::abi::detail::ControlRecord>& subtree_record : subtree_records) {
            cancel_pending(subtree_record);
        }
        (*(*record).control).dispose();
        return GF_OK;
    }

    gf_result component_state(gf_handle handle, std::uint32_t* output) {
        if (output == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "component_state requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        *output = component_state_code((*(*record).control).component_state());
        return GF_OK;
    }

    gf_result stable_id(gf_handle handle,
                        char* buffer,
                        std::uint64_t capacity,
                        std::uint64_t* required_size) {
        if (required_size == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "stable_id requires a size output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const std::string_view value = (*(*record).control).stable_id().value();
        *required_size = value.size();
        if (capacity < value.size() || (value.size() != 0U && buffer == nullptr)) {
            return fail(GF_ERROR_BUFFER_TOO_SMALL,
                        "stable_id buffer is smaller than the required UTF-8 byte count");
        }
        if (!value.empty()) {
            std::memcpy(buffer, value.data(), value.size());
        }
        return GF_OK;
    }

    gf_result set_visible(gf_handle handle, std::uint32_t visible) {
        if (visible > 1U) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "visible must be zero or one");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (*(*record).control).visible = visible != 0U;
        emit_changed(handle, record);
        return GF_OK;
    }

    gf_result get_visible(gf_handle handle, std::uint32_t* visible) {
        if (visible == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "get_visible requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        *visible = (*(*record).control).visible.read() ? 1U : 0U;
        return GF_OK;
    }

    gf_result set_bounds(gf_handle handle, gf_rect bounds) {
        if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
            !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
            bounds.width < 0.0 || bounds.height < 0.0) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "bounds must be finite with non-negative dimensions");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        (*(*record).control).set_requested_bounds(
            {bounds.x, bounds.y, bounds.width, bounds.height});
        emit_changed(handle, record);
        return GF_OK;
    }

    gf_result get_bounds(gf_handle handle, gf_rect* bounds) {
        if (bounds == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT, "get_bounds requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        const Rect value = (*(*record).control).requested_bounds();
        *bounds = {value.x, value.y, value.width, value.height};
        return GF_OK;
    }

    gf_result get_control_absolute_bounds(gf_handle handle, gf_rect* bounds) {
        if (bounds == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "get_control_absolute_bounds requires an output");
        }
        std::shared_ptr<ControlRecord> record;
        if (const gf_result result = get_control(handle, record); result != GF_OK) {
            return result;
        }
        Rect value = (*(*record).control).absolute_bounds();
        if (!(*(*record).control).attached()) {
            value = (*(*record).control).requested_bounds();
            for (gui_forms::Control::Ptr ancestor =
                     (*(*record).control).parent();
                 ancestor;
                 ancestor = (*ancestor).parent()) {
                const Rect parent_bounds = (*ancestor).requested_bounds();
                value.x += parent_bounds.x;
                value.y += parent_bounds.y;
            }
        }
        *bounds = {value.x, value.y, value.width, value.height};
        return GF_OK;
    }

    gf_result add_child(gf_handle parent_handle, gf_handle child_handle) {
        std::shared_ptr<ControlRecord> parent;
        std::shared_ptr<ControlRecord> child;
        if (const gf_result result = get_control(parent_handle, parent); result != GF_OK) {
            return result;
        }
        if (const gf_result result = get_control(child_handle, child); result != GF_OK) {
            return result;
        }
        if ((*parent).ui_thread != (*child).ui_thread) {
            return fail(GF_ERROR_WRONG_THREAD,
                        "visual parent and child belong to different UI threads");
        }
        (*(*parent).control).add_child((*child).control);
        emit_changed(parent_handle, parent);
        return GF_OK;
    }

    gf_result remove_child(gf_handle parent_handle, gf_handle child_handle) {
        std::shared_ptr<ControlRecord> parent;
        std::shared_ptr<ControlRecord> child;
        if (const gf_result result = get_control(parent_handle, parent); result != GF_OK) {
            return result;
        }
        if (const gf_result result = get_control(child_handle, child); result != GF_OK) {
            return result;
        }
        const Control::Ptr removed = (*(*parent).control).remove_child((*(*child).control).runtime_id());
        if (!removed) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "remove_child target is not a child of the supplied parent");
        }
        emit_changed(parent_handle, parent);
        return GF_OK;
    }

    gf_result attach_popup(gf_handle owner_handle, gf_handle popup_handle) {
        std::shared_ptr<ControlRecord> owner;
        std::shared_ptr<ControlRecord> popup;
        if (const gf_result result = get_control(owner_handle, owner); result != GF_OK) {
            return result;
        }
        if (const gf_result result = get_control(popup_handle, popup); result != GF_OK) {
            return result;
        }
        if ((*owner).ui_thread != (*popup).ui_thread) {
            return fail(GF_ERROR_WRONG_THREAD,
                        "popup owner and root belong to different UI threads");
        }
        if ((*popup).popup_token.connected()) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "popup root is already attached to a window");
        }
        Window* const window = (*(*owner).control).attached_window();
        if (window == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "popup owner must be attached to a running window");
        }
        (*popup).popup_plane_overrides.clear();
        promote_nested_popup_forms(popup, (*popup).control);
        try {
            (*popup).popup_token = (*window).open_popup((*owner).control, (*popup).control);
        } catch (...) {
            for (const std::pair<std::weak_ptr<Control>, gui_forms::PaintPlane>&
                     plane_override : (*popup).popup_plane_overrides) {
                const std::weak_ptr<Control>& weak = plane_override.first;
                const gui_forms::PaintPlane plane = plane_override.second;
                if (const Control::Ptr control = weak.lock()) {
                    (*control).set_paint_plane(plane);
                }
            }
            (*popup).popup_plane_overrides.clear();
            throw;
        }
        return GF_OK;
    }

    gf_result detach_popup(gf_handle popup_handle) {
        std::shared_ptr<ControlRecord> popup;
        if (const gf_result result = get_control(popup_handle, popup); result != GF_OK) {
            return result;
        }
        (*popup).popup_token.disconnect();
        for (const std::pair<std::weak_ptr<Control>, gui_forms::PaintPlane>&
                 plane_override : (*popup).popup_plane_overrides) {
            const std::weak_ptr<Control>& weak = plane_override.first;
            const gui_forms::PaintPlane plane = plane_override.second;
            if (const Control::Ptr control = weak.lock()) (*control).set_paint_plane(plane);
        }
        (*popup).popup_plane_overrides.clear();
        return GF_OK;
    }

    gf_result subscribe(gf_handle sender_handle,
                        std::uint32_t event_kind,
                        gf_event_callback callback,
                        void* context,
                        gf_event_token* output) {
        if (event_kind != GF_EVENT_STATE_CHANGED || callback == nullptr || output == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "subscribe requires the state-changed event, callback, and output");
        }
        std::scoped_lock lock(mutex_);
        std::shared_ptr<ControlRecord> sender;
        if (const gf_result result = control_locked(sender_handle, sender); result != GF_OK) {
            return result;
        }
        if (const gf_result result = require_thread(*sender); result != GF_OK) {
            return result;
        }
        std::shared_ptr<gui_forms::abi::detail::SubscriptionRecord> record = std::make_shared<SubscriptionRecord>();
        (*record).callback = callback;
        (*record).context = context;
        (*record).ui_thread = (*sender).ui_thread;
        *output = allocate_locked(SlotKind::subscription, {}, record);
        (*sender).subscriptions.push_back(*output);
        return GF_OK;
    }

    gf_result disconnect(gf_event_token token) {
        // The native connection's forwarder holds the record and its sender;
        // left connected, it keeps the sender's control alive through the
        // control's own event slot. It is disconnected after the lock is
        // released, since dropping the forwarder can release the last
        // reference to a control.
        gui_forms::SubscriptionToken revoked;
        {
            std::scoped_lock lock(mutex_);
            std::shared_ptr<SubscriptionRecord> record;
            if (const gf_result result = subscription_locked(token, record); result != GF_OK) {
                return result;
            }
            if ((*record).ui_thread != std::this_thread::get_id()) {
                return fail(GF_ERROR_WRONG_THREAD,
                            "event token operation was attempted from a non-owner thread");
            }
            (*record).connected = false;
            revoked = std::move((*record).native_subscription);
            invalidate_slot_locked(token);
        }
        revoked.disconnect();
        return GF_OK;
    }

    gf_result subscribe_v2(gf_handle sender_handle,
                           std::uint32_t event_kind,
                           gf_event_callback_v2 callback,
                           void* context,
                           gf_event_token* output) {
        if ((event_kind != GF_EVENT_CLICKED &&
             event_kind != GF_EVENT_FORM_CLOSING &&
             event_kind != GF_EVENT_FORM_CLOSED &&
             event_kind != GF_EVENT_RANGE_VALUE_CHANGED &&
             event_kind != GF_EVENT_RANGE_SCROLL &&
             event_kind != GF_EVENT_BOUNDS_CHANGED &&
             event_kind != GF_EVENT_SCROLL) ||
            callback == nullptr || output == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "subscribe_v2 requires a supported typed event, callback, and output");
        }
        std::scoped_lock lock(mutex_);
        std::shared_ptr<ControlRecord> sender;
        if (const gf_result result = control_locked(sender_handle, sender); result != GF_OK) {
            return result;
        }
        if (const gf_result result = require_thread(*sender); result != GF_OK) {
            return result;
        }
        if (event_kind == GF_EVENT_CLICKED &&
            !std::dynamic_pointer_cast<gui_forms::ButtonBase>((*sender).control)) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "clicked subscriptions require a button control");
        }
        if ((event_kind == GF_EVENT_FORM_CLOSING ||
             event_kind == GF_EVENT_FORM_CLOSED) && (*sender).kind != GF_CONTROL_FORM) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "form lifecycle subscriptions require a form control");
        }
        const std::shared_ptr<gui_forms::RangeControl> range =
            std::dynamic_pointer_cast<gui_forms::RangeControl>((*sender).control);
        const std::shared_ptr<gui_forms::ScrollableControl> scrollable = std::dynamic_pointer_cast<
            gui_forms::ScrollableControl>((*sender).control);
        if ((event_kind == GF_EVENT_RANGE_VALUE_CHANGED ||
             event_kind == GF_EVENT_RANGE_SCROLL) && !range) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "range subscriptions require a track bar or progress bar");
        }
        if (event_kind == GF_EVENT_BOUNDS_CHANGED &&
            (*sender).kind != GF_CONTROL_FORM) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "bounds-changed subscriptions require a form control");
        }
        if (event_kind == GF_EVENT_SCROLL && !scrollable) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "scroll subscriptions require a scrollable control");
        }
        std::shared_ptr<gui_forms::abi::detail::SubscriptionRecord> record = std::make_shared<SubscriptionRecord>();
        (*record).callback_v2 = callback;
        (*record).context = context;
        (*record).ui_thread = (*sender).ui_thread;
        (*record).event_kind = event_kind;
        *output = allocate_locked(SlotKind::subscription, {}, record);
        (*sender).subscriptions.push_back(*output);
        if (event_kind == GF_EVENT_CLICKED) {
            std::shared_ptr<gui_forms::ButtonBase> button = std::dynamic_pointer_cast<gui_forms::ButtonBase>((*sender).control);
            (*record).native_subscription = (*button).clicked().subscribe(
                EmitV2Subscription<gui_forms::ButtonBase&>{
                    this, sender_handle, GF_EVENT_CLICKED});
        } else if (event_kind == GF_EVENT_RANGE_VALUE_CHANGED) {
            (*record).native_subscription = (*range).value_changed().subscribe(
                EmitV2Subscription<double>{
                    this, sender_handle, GF_EVENT_RANGE_VALUE_CHANGED});
        } else if (event_kind == GF_EVENT_RANGE_SCROLL) {
            (*record).native_subscription = (*range).scroll().subscribe(
                EmitV2Subscription<const gui_forms::RangeScrollEvent&>{
                    this, sender_handle, GF_EVENT_RANGE_SCROLL});
        } else if (event_kind == GF_EVENT_BOUNDS_CHANGED) {
            (*record).native_subscription =
                (*(*sender).control).arranged_bounds_changed().subscribe(
                    EmitV2Subscription<const Rect&>{
                        this, sender_handle, GF_EVENT_BOUNDS_CHANGED});
        } else if (event_kind == GF_EVENT_SCROLL) {
            (*record).native_subscription = (*scrollable).scroll().subscribe(
                EmitV2Subscription<gui_forms::ScrollEvent&>{
                    this, sender_handle, GF_EVENT_SCROLL});
        }
        return GF_OK;
    }

    gf_result subscribe_pointer(gf_handle sender_handle,
                                gf_pointer_callback callback, void* context,
                                gf_event_token* output) {
        if (callback == nullptr || output == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "subscribe_pointer requires a callback and output");
        }
        std::scoped_lock lock(mutex_);
        std::shared_ptr<ControlRecord> sender;
        if (const gf_result result = control_locked(sender_handle, sender);
            result != GF_OK) {
            return result;
        }
        if (const gf_result result = require_thread(*sender); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::RasterControl> raster = std::dynamic_pointer_cast<RasterControl>((*sender).control);
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*sender).control);
        std::shared_ptr<gui_forms::abi::detail::SubscriptionRecord> record = std::make_shared<SubscriptionRecord>();
        (*record).pointer_callback = callback;
        (*record).context = context;
        (*record).ui_thread = (*sender).ui_thread;
        *output = allocate_locked(SlotKind::subscription, {}, record);
        (*sender).subscriptions.push_back(*output);
        const RasterPointerForwarder connect{record, sender_handle};
        if (raster) {
            (*record).native_subscription = (*raster).pointer_input().subscribe(connect);
        } else if (field) {
            (*record).native_subscription = (*field).pointer_input().subscribe(connect);
        } else {
            const std::weak_ptr<Control> weak_control = (*sender).control;
            (*record).native_subscription = (*(*sender).control).pointer_observed().subscribe(
                ControlPointerForwarder{
                    record, sender_handle, weak_control});
        }
        return GF_OK;
    }

    gf_result subscribe_key(gf_handle sender_handle,
                            gf_key_callback callback, void* context,
                            gf_event_token* output) {
        if (callback == nullptr || output == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "subscribe_key requires a callback and output");
        }
        std::scoped_lock lock(mutex_);
        std::shared_ptr<ControlRecord> sender;
        if (const gf_result result = control_locked(sender_handle, sender);
            result != GF_OK) return result;
        if (const gf_result result = require_thread(*sender); result != GF_OK) return result;
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*sender).control);
        const std::shared_ptr<gui_forms::abi::detail::RasterControl> raster = std::dynamic_pointer_cast<RasterControl>((*sender).control);
        if (!field && !raster) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "key subscriptions require an interactive retained control");
        }
        std::shared_ptr<gui_forms::abi::detail::SubscriptionRecord> record = std::make_shared<SubscriptionRecord>();
        (*record).key_callback = callback;
        (*record).context = context;
        (*record).ui_thread = (*sender).ui_thread;
        *output = allocate_locked(SlotKind::subscription, {}, record);
        (*sender).subscriptions.push_back(*output);
        gui_forms::Event<const RasterKeySample &>& key_input = field ? (*field).key_input() : (*raster).key_input();
        (*record).native_subscription = key_input.subscribe(
            RasterKeyForwarder{record, sender_handle});
        return GF_OK;
    }

    gf_result subscribe_key_preview(gf_handle sender_handle,
                                    gf_key_callback callback, void* context,
                                    gf_event_token* output) {
        if (callback == nullptr || output == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "subscribe_key_preview requires a callback and output");
        }
        std::scoped_lock lock(mutex_);
        std::shared_ptr<ControlRecord> sender;
        if (const gf_result result = control_locked(sender_handle, sender);
            result != GF_OK) return result;
        if (const gf_result result = require_thread(*sender); result != GF_OK) {
            return result;
        }
        const std::shared_ptr<gui_forms::abi::detail::FormControl> form = std::dynamic_pointer_cast<FormControl>((*sender).control);
        if (!form || (*sender).kind != GF_CONTROL_FORM) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "key preview subscriptions require a form control");
        }
        std::shared_ptr<gui_forms::abi::detail::SubscriptionRecord> record = std::make_shared<SubscriptionRecord>();
        (*record).key_preview_callback = callback;
        (*record).context = context;
        (*record).ui_thread = (*sender).ui_thread;
        *output = allocate_locked(SlotKind::subscription, {}, record);
        (*sender).subscriptions.push_back(*output);
        (*record).native_subscription = (*form).key_preview().subscribe(
            KeyPreviewForwarder{this, record, sender_handle, sender});
        return GF_OK;
    }

    gf_result subscribe_text(gf_handle sender_handle,
                             gf_text_callback callback, void* context,
                             gf_event_token* output) {
        if (callback == nullptr || output == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "subscribe_text requires a callback and output");
        }
        std::scoped_lock lock(mutex_);
        std::shared_ptr<ControlRecord> sender;
        if (const gf_result result = control_locked(sender_handle, sender);
            result != GF_OK) return result;
        if (const gf_result result = require_thread(*sender); result != GF_OK) return result;
        const std::shared_ptr<gui_forms::abi::detail::FieldControl> field = std::dynamic_pointer_cast<FieldControl>((*sender).control);
        if (!field) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "text subscriptions require a retained field control");
        }
        std::shared_ptr<gui_forms::abi::detail::SubscriptionRecord> record = std::make_shared<SubscriptionRecord>();
        (*record).text_callback = callback;
        (*record).context = context;
        (*record).ui_thread = (*sender).ui_thread;
        *output = allocate_locked(SlotKind::subscription, {}, record);
        (*sender).subscriptions.push_back(*output);
        (*record).native_subscription = (*field).text_input().subscribe(
            RasterTextForwarder{record, sender_handle});
        return GF_OK;
    }

    gf_result begin_invoke(gf_handle control_handle,
                           gf_dispatch_callback callback,
                           void* context) {
        if (callback == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "begin_invoke requires a callback");
        }
        std::function<void()> wake;
        {
            std::scoped_lock lock(mutex_);
            std::shared_ptr<ControlRecord> control;
            if (const gf_result result = control_locked(control_handle, control);
                result != GF_OK) {
                return result;
            }
            const std::shared_ptr<ControlRecord> root = root_record_locked(control);
            const bool dispatch_root = root &&
                ((*root).kind == GF_CONTROL_FORM || (*root).kind == GF_CONTROL_CUSTOM) &&
                !(*(*root).control).parent();
            if (!dispatch_root) {
                return fail(GF_ERROR_WRONG_HANDLE_KIND,
                            "begin_invoke requires a control rooted in a top-level form or popup");
            }
            if ((*root).dispatch_queue.size() >=
                gui_forms::maximum_posted_callbacks) {
                return fail(GF_ERROR_INVALID_ARGUMENT,
                            "begin_invoke reached the posted callback bound");
            }
            (*root).dispatch_queue.push_back({callback, context});
            if ((*root).dispatch_depth == 0U &&
                (*root).managed_callback_depth == 0U &&
                !(*root).dispatch_wake_pending && (*root).host_wake) {
                (*root).dispatch_wake_pending = true;
                wake = (*root).host_wake;
            }
        }
        if (wake) wake();
        return GF_OK;
    }

    gf_result request_close(gf_handle form_handle) {
        std::function<void()> request;
        {
            std::scoped_lock lock(mutex_);
            std::shared_ptr<ControlRecord> form;
            if (const gf_result result = control_locked(form_handle, form);
                result != GF_OK) {
                return result;
            }
            const bool closable_root = (*form).kind == GF_CONTROL_FORM ||
                ((*form).kind == GF_CONTROL_CUSTOM &&
                 (*form).host_phase != AbiHostPhase::idle);
            if (!closable_root || (*(*form).control).parent()) {
                return fail(GF_ERROR_WRONG_HANDLE_KIND,
                            "request_close requires a running top-level form or popup");
            }
            (*form).close_requested = true;
            request = (*form).host_close;
        }
        if (request) request();
        return GF_OK;
    }

    gf_result callback_fault_count(gf_handle control_handle,
                                   std::uint64_t* count) {
        if (count == nullptr) {
            return fail(GF_ERROR_INVALID_ARGUMENT,
                        "callback_fault_count requires an output");
        }
        std::scoped_lock lock(mutex_);
        std::shared_ptr<ControlRecord> control;
        if (const gf_result result = control_locked(control_handle, control);
            result != GF_OK) {
            return result;
        }
        const std::shared_ptr<ControlRecord> root = root_record_locked(control);
        *count = root ? (*root).callback_faults : (*control).callback_faults;
        return GF_OK;
    }

private:
    struct HostFinishGuard final {
        Registry* registry{};
        std::shared_ptr<ControlRecord> record;
        ~HostFinishGuard() { (*registry).finish_host(record); }
    };

    struct ManagedCallbackGuard final {
        Registry* registry{};
        std::shared_ptr<ControlRecord> root;
        ~ManagedCallbackGuard() { (*registry).leave_managed_callback(root); }
    };

    struct DispatchGuard final {
        Registry* registry{};
        std::shared_ptr<ControlRecord> root;
        ~DispatchGuard() { (*registry).leave_dispatch(root); }
    };

    struct AutomationResolver final {
        std::shared_ptr<std::unordered_map<
            std::string, std::shared_ptr<Control>>> controls;

        std::shared_ptr<Control> operator()(std::string_view name) const {
            const std::unordered_map<
                std::string, std::shared_ptr<Control>>::const_iterator found =
                    (*controls).find(std::string(name));
            return found == (*controls).end()
                ? std::shared_ptr<Control>{} : (*found).second;
        }
    };

    struct CloseRequestCallback final {
        Registry* registry{};
        gf_handle handle{};

        void operator()(gui_forms::HostCloseRequest& request) const {
            request.cancel = (*registry).emit_v2(
                handle, GF_EVENT_FORM_CLOSING) == GF_EVENT_CALLBACK_CANCEL;
        }
    };

    struct HostReadyCallback final {
        Registry* registry{};
        std::shared_ptr<ControlRecord> record;

        void operator()(
            std::function<void()> wake,
            std::function<void()> close,
            std::function<gui_forms::HostDialogResult(
                const gui_forms::HostDialogRequest&)> dialog,
            std::function<gui_forms::HostServiceStatus(
                const gui_forms::HostTooltipRequest&)> tooltip_show,
            std::function<void()> tooltip_hide,
            std::function<gui_forms::HostClipboardTextResult()> clipboard_read,
            std::function<gui_forms::HostServiceStatus(
                std::string_view)> clipboard_write) const {
            (*registry).publish_host(
                record, std::move(wake), std::move(close), std::move(dialog),
                std::move(tooltip_show), std::move(tooltip_hide),
                std::move(clipboard_read), std::move(clipboard_write));
        }
    };

    struct WindowsDispatchPendingCallback final {
        Registry* registry{};
        std::shared_ptr<ControlRecord> record;
        std::shared_ptr<std::unordered_map<
            std::string, std::shared_ptr<Control>>> automation_controls;

        void operator()() const {
            (*registry).pump_pending(record);
            (*registry).refresh_named_controls(record, *automation_controls);
        }
    };

    struct DispatchPendingCallback final {
        Registry* registry{};
        std::shared_ptr<ControlRecord> record;

        void operator()() const { (*registry).pump_pending(record); }
    };

    struct HostClosedCallback final {
        Registry* registry{};
        gf_handle handle{};
        std::shared_ptr<ControlRecord> record;

        void operator()() const {
            (*registry).mark_host_stopping(record);
            static_cast<void>((*registry).emit_v2(
                handle, GF_EVENT_FORM_CLOSED));
        }
    };

    struct FinalSnapshotCallback final {
        Registry* registry{};
        std::shared_ptr<ControlRecord> record;

        void operator()(std::string_view metrics,
                        std::string_view host) const {
            (*record).host_trace =
                "{\"window\":" + std::string(metrics) +
                ",\"host\":" + std::string(host) +
                ",\"managed\":" + (*registry).managed_trace(record) + "}";
        }
    };

    template <typename EventArgument>
    struct EmitV2Subscription final {
        Registry* registry{};
        gf_handle sender{};
        std::uint32_t event_kind{};

        void operator()(EventArgument) const {
            static_cast<void>((*registry).emit_v2(sender, event_kind));
        }
    };

    struct RasterPointerForwarder final {
        std::shared_ptr<SubscriptionRecord> subscription;
        gf_handle sender{};

        void operator()(const RasterPointerSample& sample) const {
            if (!(*subscription).connected ||
                (*subscription).pointer_callback == nullptr) return;
            static_cast<void>((*subscription).pointer_callback(
                sender, sample.event_kind, sample.x, sample.y,
                sample.wheel_delta, sample.button,
                (*subscription).context));
        }
    };

    struct ControlPointerForwarder final {
        std::shared_ptr<SubscriptionRecord> subscription;
        gf_handle sender{};
        std::weak_ptr<Control> control;

        void operator()(const gui_forms::PointerEvent& event) const {
            if (!(*subscription).connected ||
                (*subscription).pointer_callback == nullptr) return;
            const std::shared_ptr<Control> retained = control.lock();
            if (!retained) return;
            const Rect bounds = (*retained).absolute_bounds();
            const std::uint32_t kind = pointer_event_kind(event.action);
            static_cast<void>((*subscription).pointer_callback(
                sender, kind, event.position.x - bounds.x,
                event.position.y - bounds.y, event.wheel_delta.y,
                static_cast<std::uint32_t>(event.button),
                (*subscription).context));
        }
    };

    struct RasterKeyForwarder final {
        std::shared_ptr<SubscriptionRecord> subscription;
        gf_handle sender{};

        void operator()(const RasterKeySample& sample) const {
            if (!(*subscription).connected ||
                (*subscription).key_callback == nullptr) return;
            static_cast<void>((*subscription).key_callback(
                sender, sample.event_kind, sample.physical_key,
                sample.modifiers, sample.repeat ? 1U : 0U,
                (*subscription).context));
        }
    };

    struct KeyPreviewForwarder final {
        Registry* registry{};
        std::shared_ptr<SubscriptionRecord> subscription;
        gf_handle sender_handle{};
        std::shared_ptr<ControlRecord> sender;

        void operator()(RasterKeySample& sample) const {
            if (!(*subscription).connected ||
                (*subscription).key_preview_callback == nullptr) return;
            const std::uint32_t result =
                (*subscription).key_preview_callback(
                    sender_handle, sample.event_kind, sample.physical_key,
                    sample.modifiers, sample.repeat ? 1U : 0U,
                    (*subscription).context);
            if (result == GF_EVENT_CALLBACK_CANCEL) {
                sample.handled = true;
            } else if (result == GF_EVENT_CALLBACK_FAULTED ||
                       result > GF_EVENT_CALLBACK_FAULTED) {
                std::scoped_lock callback_lock((*registry).mutex_);
                const std::shared_ptr<ControlRecord> root =
                    (*registry).root_record_locked(sender);
                ++(*root).callback_faults;
            }
        }
    };

    struct RasterTextForwarder final {
        std::shared_ptr<SubscriptionRecord> subscription;
        gf_handle sender{};

        void operator()(const RasterTextSample& sample) const {
            if (!(*subscription).connected ||
                (*subscription).text_callback == nullptr) return;
            const gf_string_view text{sample.text.data(), sample.text.size()};
            static_cast<void>((*subscription).text_callback(
                sender, text, sample.composing ? 1U : 0U,
                sample.replacement_start, sample.replacement_length,
                (*subscription).context));
        }
    };

    static void promote_nested_popup_forms(
        const std::shared_ptr<ControlRecord>& popup,
        const Control::Ptr& control) {
        if (std::dynamic_pointer_cast<FormControl>(control)) {
            (*popup).popup_plane_overrides.emplace_back(
                control, (*control).paint_plane());
            (*control).set_paint_plane(gui_forms::PaintPlane::control);
        }
        for (const Control::Ptr& child : (*control).children()) {
            promote_nested_popup_forms(popup, child);
        }
    }

    std::shared_ptr<ControlRecord> root_record_locked(
        const std::shared_ptr<ControlRecord>& record) {
        std::shared_ptr<Control> root = (*record).control;
        while (const gui_forms::Control::Ptr parent = (*root).parent()) {
            root = parent;
        }
        for (const RegistrySlot& candidate : slots_) {
            if (candidate.kind == SlotKind::control && candidate.control &&
                (*candidate.control).control == root) {
                return candidate.control;
            }
        }
        return record;
    }

    void leave_managed_callback(
        const std::shared_ptr<ControlRecord>& root) {
        std::function<void()> wake;
        {
            std::scoped_lock lock(mutex_);
            if ((*root).managed_callback_depth > 0U) {
                --(*root).managed_callback_depth;
            }
            if ((*root).managed_callback_depth == 0U &&
                !(*root).dispatch_queue.empty() &&
                !(*root).dispatch_wake_pending && (*root).host_wake) {
                (*root).dispatch_wake_pending = true;
                wake = (*root).host_wake;
            }
        }
        if (wake) wake();
    }

    void leave_dispatch(const std::shared_ptr<ControlRecord>& root) {
        std::function<void()> wake;
        {
            std::scoped_lock lock(mutex_);
            if ((*root).dispatch_depth > 0U) --(*root).dispatch_depth;
            if ((*root).dispatch_depth == 0U &&
                (*root).managed_callback_depth == 0U &&
                !(*root).dispatch_queue.empty() &&
                !(*root).dispatch_wake_pending && (*root).host_wake) {
                (*root).dispatch_wake_pending = true;
                wake = (*root).host_wake;
            }
        }
        if (wake) wake();
    }

    std::uint32_t emit_v2(gf_handle sender_handle, std::uint32_t event_kind) {
        std::vector<std::shared_ptr<SubscriptionRecord>> snapshot;
        std::shared_ptr<ControlRecord> root;
        {
            std::scoped_lock lock(mutex_);
            std::shared_ptr<ControlRecord> sender;
            if (control_locked(sender_handle, sender) != GF_OK) {
                return GF_EVENT_CALLBACK_CONTINUE;
            }
            root = root_record_locked(sender);
            snapshot.reserve((*sender).subscriptions.size());
            for (const gf_event_token token : (*sender).subscriptions) {
                RegistrySlot* slot = slot_locked(token);
                if (slot != nullptr && (*slot).kind == SlotKind::subscription &&
                    (*slot).subscription && (*(*slot).subscription).connected &&
                    (*(*slot).subscription).event_kind == event_kind &&
                    (*(*slot).subscription).callback_v2 != nullptr) {
                    snapshot.push_back((*slot).subscription);
                }
            }
        }
        {
            std::scoped_lock lock(mutex_);
            ++(*root).managed_callback_depth;
        }
        const ManagedCallbackGuard callback_guard{this, root};
        std::uint32_t aggregate = GF_EVENT_CALLBACK_CONTINUE;
        for (const std::shared_ptr<gui_forms::abi::detail::SubscriptionRecord>& subscription : snapshot) {
            if (!(*subscription).connected) continue;
            const std::uint32_t result = (*subscription).callback_v2(
                sender_handle, event_kind, (*subscription).context);
            if (result == GF_EVENT_CALLBACK_CANCEL) {
                aggregate = GF_EVENT_CALLBACK_CANCEL;
            } else if (result == GF_EVENT_CALLBACK_FAULTED ||
                       result > GF_EVENT_CALLBACK_FAULTED) {
                std::scoped_lock lock(mutex_);
                ++(*root).callback_faults;
            }
        }
        return aggregate;
    }

    void pump_pending(const std::shared_ptr<ControlRecord>& root) {
        {
            std::scoped_lock lock(mutex_);
            (*root).dispatch_wake_pending = false;
            if ((*root).dispatch_depth != 0U || (*root).managed_callback_depth != 0U) {
                return;
            }
            ++(*root).dispatch_depth;
        }
        const DispatchGuard dispatch_guard{this, root};
        std::deque<DispatchRecord> pending;
        {
            std::scoped_lock lock(mutex_);
            pending.swap((*root).dispatch_queue);
            if (!pending.empty()) ++(*root).dispatch_turns;
        }
        for (const DispatchRecord& dispatch : pending) {
            const std::uint32_t result = dispatch.callback(dispatch.context, 0U);
            std::scoped_lock lock(mutex_);
            ++(*root).dispatches;
            if (result == GF_EVENT_CALLBACK_FAULTED ||
                result > GF_EVENT_CALLBACK_FAULTED) {
                ++(*root).callback_faults;
            }
        }
    }

    void cancel_pending(const std::shared_ptr<ControlRecord>& root) noexcept {
        std::deque<DispatchRecord> pending;
        {
            std::scoped_lock lock(mutex_);
            (*root).dispatch_wake_pending = false;
            pending.swap((*root).dispatch_queue);
        }
        for (const DispatchRecord& dispatch : pending) {
            try {
                static_cast<void>(dispatch.callback(dispatch.context, 1U));
            } catch (...) {
            }
        }
    }

    void publish_host(const std::shared_ptr<ControlRecord>& root,
                      std::function<void()> wake,
                      std::function<void()> request_close,
                      std::function<gui_forms::HostDialogResult(
                          const gui_forms::HostDialogRequest&)> dialog = {},
                      std::function<gui_forms::HostServiceStatus(
                          const gui_forms::HostTooltipRequest&)> tooltip_show = {},
                      std::function<void()> tooltip_hide = {},
                      std::function<gui_forms::HostClipboardTextResult()>
                          clipboard_read = {},
                      std::function<gui_forms::HostServiceStatus(std::string_view)>
                          clipboard_write = {}) {
        bool should_wake{};
        bool should_close{};
        std::function<void()> published_wake;
        std::function<void()> published_close;
        {
            std::scoped_lock lock(mutex_);
            (*root).host_wake = std::move(wake);
            (*root).host_close = std::move(request_close);
            (*root).host_dialog = std::move(dialog);
            (*root).host_tooltip_show = std::move(tooltip_show);
            (*root).host_tooltip_hide = std::move(tooltip_hide);
            (*root).host_clipboard_read = std::move(clipboard_read);
            (*root).host_clipboard_write = std::move(clipboard_write);
            (*root).host_phase = AbiHostPhase::ready;
            should_wake = !(*root).dispatch_queue.empty() &&
                !(*root).dispatch_wake_pending;
            if (should_wake) (*root).dispatch_wake_pending = true;
            should_close = (*root).close_requested;
            published_wake = (*root).host_wake;
            published_close = (*root).host_close;
        }
        if (should_wake && published_wake) published_wake();
        if (should_close && published_close) published_close();
    }

    void mark_host_stopping(const std::shared_ptr<ControlRecord>& root) noexcept {
        {
            std::scoped_lock lock(mutex_);
            if ((*root).host_phase != AbiHostPhase::idle) {
                (*root).host_phase = AbiHostPhase::stopping;
            }
        }
    }

    bool close_requested(const std::shared_ptr<ControlRecord>& root) {
        std::scoped_lock lock(mutex_);
        return (*root).close_requested;
    }

    void finish_host(const std::shared_ptr<ControlRecord>& root) noexcept {
        mark_host_stopping(root);
        cancel_pending(root);
        std::scoped_lock lock(mutex_);
        (*root).host_wake = {};
        (*root).host_close = {};
        (*root).host_dialog = {};
        (*root).host_tooltip_show = {};
        (*root).host_tooltip_hide = {};
        (*root).host_clipboard_read = {};
        (*root).host_clipboard_write = {};
        (*root).dispatch_wake_pending = false;
        (*root).host_phase = AbiHostPhase::idle;
    }

    static bool copy_view(gf_string_view input, std::string& output) {
        if ((input.size != 0U && input.data == nullptr) ||
            input.size > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) return false;
        output.assign(input.size == 0U ? "" : input.data,
                      static_cast<std::size_t>(input.size));
        return output.find('\0') == std::string::npos;
    }

    static std::vector<gui_forms::HostFileDialogFilter>
    parse_filters(std::string_view serialized) {
        std::vector<std::string> parts;
        std::size_t begin = 0;
        while (begin <= serialized.size()) {
            const std::size_t end = serialized.find('|', begin);
            parts.emplace_back(serialized.substr(
                begin, end == std::string_view::npos
                    ? serialized.size() - begin : end - begin));
            if (end == std::string_view::npos) break;
            begin = end + 1U;
        }
        std::vector<gui_forms::HostFileDialogFilter> result;
        for (std::size_t index = 0; index + 1U < parts.size() &&
             result.size() < gui_forms::HostServices::maximum_dialog_filters;
             index += 2U) {
            gui_forms::HostFileDialogFilter item;
            item.label = std::move(parts[index]);
            std::string_view patterns = parts[index + 1U];
            std::size_t pattern_begin = 0;
            while (pattern_begin <= patterns.size() &&
                   item.extensions.size() <
                       gui_forms::HostServices::maximum_dialog_extensions) {
                const std::size_t pattern_end = patterns.find(';', pattern_begin);
                std::string extension(patterns.substr(
                    pattern_begin, pattern_end == std::string_view::npos
                        ? patterns.size() - pattern_begin
                        : pattern_end - pattern_begin));
                while (!extension.empty() &&
                       (extension.front() == '*' || extension.front() == '.')) {
                    extension.erase(extension.begin());
                }
                if (!extension.empty()) item.extensions.push_back(std::move(extension));
                if (pattern_end == std::string_view::npos) break;
                pattern_begin = pattern_end + 1U;
            }
            result.push_back(std::move(item));
        }
        return result;
    }

    std::string managed_trace(const std::shared_ptr<ControlRecord>& root) {
        std::scoped_lock lock(mutex_);
        const char* phase = host_phase_name((*root).host_phase);
        return "{\"callback_faults\":" + std::to_string((*root).callback_faults) +
               ",\"dispatches\":" + std::to_string((*root).dispatches) +
               ",\"dispatch_turns\":" + std::to_string((*root).dispatch_turns) +
               ",\"host_phase\":\"" + phase + "\"" +
               ",\"close_requested\":" +
               std::string((*root).close_requested ? "true" : "false") + "}";
    }

    static std::shared_ptr<gui_forms::ButtonBase> first_button(
        const std::shared_ptr<Control>& root) {
        if (const std::shared_ptr<gui_forms::ButtonBase> button =
                std::dynamic_pointer_cast<gui_forms::ButtonBase>(root)) {
            return button;
        }
        const std::span<const gui_forms::Control::Ptr> children =
            (*root).children();
        // The retained vector is painter order, while public child index zero
        // is topmost. Automation follows public order so the selected target
        // stays stable when z-order and docking use their native semantics.
        for (std::size_t index = children.size(); index > 0U; --index) {
            if (std::shared_ptr<gui_forms::ButtonBase> button =
                    first_button(children[index - 1U])) return button;
        }
        return {};
    }

    static std::shared_ptr<Control> first_pointer_control(
        const std::shared_ptr<Control>& root) {
        if (std::dynamic_pointer_cast<RasterControl>(root) ||
            std::dynamic_pointer_cast<FieldControl>(root) ||
            std::dynamic_pointer_cast<gui_forms::RangeControl>(root)) {
            return root;
        }
        const std::span<const gui_forms::Control::Ptr> children =
            (*root).children();
        for (std::size_t index = children.size(); index > 0U; --index) {
            if (std::shared_ptr<Control> target =
                    first_pointer_control(children[index - 1U])) return target;
        }
        return {};
    }

    std::shared_ptr<std::unordered_map<std::string, std::shared_ptr<Control>>>
    named_controls_snapshot(const std::shared_ptr<ControlRecord>& root) {
        std::shared_ptr<std::unordered_map<
            std::string, std::shared_ptr<Control>>> result = std::make_shared<
            std::unordered_map<std::string, std::shared_ptr<Control>>>();
        refresh_named_controls(root, *result);
        return result;
    }

    void refresh_named_controls(
        const std::shared_ptr<ControlRecord>& root,
        std::unordered_map<std::string, std::shared_ptr<Control>>& result) {
        std::scoped_lock lock(mutex_);
        result.clear();
        for (const RegistrySlot& candidate : slots_) {
            if (candidate.kind == SlotKind::control && candidate.control &&
                !(*candidate.control).name.empty() &&
                contains_control((*root).control, (*candidate.control).control)) {
                result.insert_or_assign((*candidate.control).name,
                                        (*candidate.control).control);
            }
        }
    }

    static bool contains_control(const std::shared_ptr<Control>& root,
                                 const std::shared_ptr<Control>& candidate) {
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

    gf_result require_thread(const ControlRecord& record) const {
        if (record.ui_thread != std::this_thread::get_id()) {
            return fail(GF_ERROR_WRONG_THREAD,
                        "control operation was attempted from a non-owner thread");
        }
        return GF_OK;
    }

    gf_result get_control(gf_handle handle, std::shared_ptr<ControlRecord>& output) {
        std::scoped_lock lock(mutex_);
        if (const gf_result result = control_locked(handle, output); result != GF_OK) {
            return result;
        }
        return require_thread(*output);
    }

    gf_result control_locked(gf_handle handle, std::shared_ptr<ControlRecord>& output) {
        RegistrySlot* slot = slot_locked(handle);
        if (slot == nullptr) {
            return fail(GF_ERROR_STALE_HANDLE, "control handle is stale");
        }
        if ((*slot).kind != SlotKind::control || !(*slot).control) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND, "handle does not identify a control");
        }
        output = (*slot).control;
        if (!(*(*output).control).is_alive()) {
            return fail(GF_ERROR_DISPOSED, "control has been disposed");
        }
        return GF_OK;
    }

    gf_result subscription_locked(gf_event_token token,
                                  std::shared_ptr<SubscriptionRecord>& output) {
        RegistrySlot* slot = slot_locked(token);
        if (slot == nullptr) {
            return fail(GF_ERROR_STALE_HANDLE, "event token is stale");
        }
        if ((*slot).kind != SlotKind::subscription || !(*slot).subscription) {
            return fail(GF_ERROR_WRONG_HANDLE_KIND,
                        "handle does not identify an event subscription");
        }
        output = (*slot).subscription;
        return GF_OK;
    }

    RegistrySlot* slot_locked(gf_handle handle) {
        if (handle.slot == 0U || handle.slot > slots_.size()) {
            return nullptr;
        }
        RegistrySlot& slot = slots_[handle.slot - 1U];
        if (slot.kind == SlotKind::empty || slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    gf_handle allocate_locked(SlotKind kind,
                              std::shared_ptr<ControlRecord> control,
                              std::shared_ptr<SubscriptionRecord> subscription) {
        std::vector<RegistrySlot>::iterator found = slots_.begin();
        while (found != slots_.end() &&
               (*found).kind != SlotKind::empty) {
            ++found;
        }
        if (found == slots_.end()) {
            slots_.push_back({});
            found = std::prev(slots_.end());
        }
        (*found).kind = kind;
        (*found).control = std::move(control);
        (*found).subscription = std::move(subscription);
        return {static_cast<std::uint32_t>(std::distance(slots_.begin(), found) + 1),
                (*found).generation};
    }

    // Stales the control and its subscriptions. Their native connections
    // move to revoked, which the caller disconnects after unlocking (see
    // disconnect).
    void invalidate_control_locked(gf_handle handle, ControlRecord& record,
                                   std::vector<gui_forms::SubscriptionToken>& revoked) {
        for (const gf_event_token token : record.subscriptions) {
            if (RegistrySlot* slot = slot_locked(token);
                slot != nullptr && (*slot).kind == SlotKind::subscription) {
                (*(*slot).subscription).connected = false;
                revoked.push_back(std::move((*(*slot).subscription).native_subscription));
                invalidate_slot_locked(token);
            }
        }
        record.subscriptions.clear();
        invalidate_slot_locked(handle);
    }

    static void disconnect_revoked(std::vector<gui_forms::SubscriptionToken>& revoked) noexcept {
        for (gui_forms::SubscriptionToken& token : revoked) token.disconnect();
        revoked.clear();
    }

    void invalidate_slot_locked(gf_handle handle) {
        RegistrySlot* slot = slot_locked(handle);
        if (slot == nullptr) {
            return;
        }
        (*slot).kind = SlotKind::empty;
        (*slot).control.reset();
        (*slot).subscription.reset();
        ++(*slot).generation;
        if ((*slot).generation == 0U) {
            (*slot).generation = 1U;
        }
    }

    void emit_changed(gf_handle sender_handle,
                      const std::shared_ptr<ControlRecord>& sender) {
        std::vector<std::shared_ptr<SubscriptionRecord>> snapshot;
        {
            std::scoped_lock lock(mutex_);
            snapshot.reserve((*sender).subscriptions.size());
            for (const gf_event_token token : (*sender).subscriptions) {
                RegistrySlot* slot = slot_locked(token);
                if (slot != nullptr && (*slot).kind == SlotKind::subscription &&
                    (*slot).subscription && (*(*slot).subscription).connected &&
                    (*(*slot).subscription).callback != nullptr) {
                    snapshot.push_back((*slot).subscription);
                }
            }
        }
        for (const std::shared_ptr<gui_forms::abi::detail::SubscriptionRecord>& subscription : snapshot) {
            if (!(*subscription).connected) {
                continue;
            }
            const gf_event_callback callback = (*subscription).callback;
            void* const context = (*subscription).context;
            callback(sender_handle, GF_EVENT_STATE_CHANGED, context);
        }
    }

    std::mutex mutex_;
    std::vector<RegistrySlot> slots_;
    std::uint64_t next_dialog_request_{1};
    std::string clipboard_text_;
    std::uint64_t clipboard_generation_{};
};

inline Registry& registry() {
    static Registry instance;
    return instance;
}

} // namespace gui_forms::abi::detail
