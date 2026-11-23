#include "gui_forms/host.hpp"

#include "gui_forms/window.hpp"
#include "../../failure/fail_fast.hpp"
#include "../../surface_root/access/surface_host_access.hpp"
#include "drag_items.hpp"
#include "host_session_ic_checks.hpp"
#include "session_directory.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <sstream>
#include <stxlib/threads.hpp>
#include <type_traits>
#include <unordered_set>

namespace gui_forms {
namespace {
namespace ic = detail::ic::host_session;

// The geometry rules are HostSession::dispatch's clauses (host_session.ic.toml).
bool valid_size(Size size) noexcept {
    const bool valid = ic::admits_dispatch_size(size.width, size.height);
    return valid;
}

bool valid_scale(double scale) noexcept {
    const bool valid = ic::admits_dispatch_scale(scale);
    return valid;
}

bool valid_monitor_rectangle(Rect rectangle) noexcept {
    return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
           valid_size({rectangle.width, rectangle.height});
}

bool valid_utf8(std::string_view text) noexcept {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = bytes[index];
        if (lead <= 0x7FU) {
            ++index;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t codepoint = 0;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            length = 2;
            codepoint = lead & 0x1FU;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            length = 3;
            codepoint = lead & 0x0FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            length = 4;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (index + length > text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            const unsigned char continuation = bytes[index + offset];
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (continuation & 0x3FU);
        }
        if ((length == 3 && codepoint < 0x800U) ||
            (length == 4 && codepoint < 0x10000U) ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
            codepoint > 0x10FFFFU) {
            return false;
        }
        index += length;
    }
    return true;
}

bool valid_monitor(const HostMonitor& monitor) noexcept {
    return !monitor.id.empty() && valid_monitor_rectangle(monitor.frame) &&
           valid_monitor_rectangle(monitor.work_area) &&
           valid_scale(monitor.scale);
}

bool add_drag_size(std::size_t& total, std::size_t value) noexcept {
    if (value > DragLimits::maximum_total_bytes - total) return false;
    total += value;
    return true;
}

bool valid_monitor_set(const std::vector<HostMonitor>& monitors) {
    if (monitors.empty() || monitors.size() > 64U) {
        return false;
    }
    std::unordered_set<std::string> identifiers;
    std::size_t primary_count = 0;
    for (const HostMonitor& monitor : monitors) {
        if (!valid_monitor(monitor) || !identifiers.insert(monitor.id).second) {
            return false;
        }
        primary_count += monitor.primary ? 1U : 0U;
    }
    return primary_count == 1U;
}

bool valid_drag_event(const DragEvent& event) noexcept {
    if (event.session_id == 0 || !std::isfinite(event.position.x) ||
        !std::isfinite(event.position.y) ||
        !detail::known_drag_effects(event.allowed_effects) ||
        event.accepted_effect != DragEffect::none) {
        return false;
    }
    if (event.action != DragAction::leave &&
        (event.items.empty() || event.allowed_effects == DragEffect::none)) {
        return false;
    }
    const bool valid = detail::valid_drag_items(event.items);
    return valid;
}

// What a host event is refused with in lifecycle `phase` (none when it is
// admitted): before attach only an attach or shutdown, once attached no
// second attach, after close authorization only the close, shutdown or a
// passive deactivation or occlusion, once closed only shutdown, and nothing
// after shutdown.
// Whether a context-request or hover report names a finite position and a
// known origin; every other payload passes.
bool valid_pointer_report(const HostEventPayload& payload) noexcept {
    if (const HostContextRequestEvent* request = std::get_if<HostContextRequestEvent>(&payload)) {
        const bool origin = (*request).origin == ContextRequestOrigin::pointer ||
                            (*request).origin == ContextRequestOrigin::keyboard;
        return origin && std::isfinite((*request).position.x) && std::isfinite((*request).position.y);
    }
    if (const HostHoverEvent* hover = std::get_if<HostHoverEvent>(&payload)) {
        return std::isfinite((*hover).position.x) && std::isfinite((*hover).position.y);
    }
    if (const GestureEvent* gesture = std::get_if<GestureEvent>(&payload)) {
        return valid_gesture_event(*gesture);
    }
    if (const HostRelativeMotionEvent* motion = std::get_if<HostRelativeMotionEvent>(&payload)) {
        const bool delta = std::isfinite((*motion).delta.x) && std::isfinite((*motion).delta.y);
        const Point* const at = (*motion).position.value();
        const bool position = at == nullptr || (std::isfinite((*at).x) && std::isfinite((*at).y));
        return delta && position;
    }
    return true;
}

bool valid_input_settings(const HostInputSettingsEvent& input) noexcept {
    const bool interval = input.double_click_milliseconds != 0U &&
                          input.double_click_milliseconds <= maximum_double_click_milliseconds;
    const bool origin = input.double_click_origin == SettingOrigin::default_value ||
                        input.double_click_origin == SettingOrigin::host;
    return interval && origin && valid_pointer_settings(input.pointer) && valid_caret_settings(input.caret);
}

// The lifecycle matrix is HostSession::dispatch's dispatch_lifecycle clause
// (host_session.ic.toml); shutdown is the call's outside state.
HostDispatchError lifecycle_refusal(HostLifecyclePhase phase, bool attach, bool shutdown,
                                    bool closed, bool passive_terminal) noexcept {
    switch (phase) {
    case HostLifecyclePhase::constructed:
    case HostLifecyclePhase::attached:
    case HostLifecyclePhase::close_authorized:
    case HostLifecyclePhase::closed:
        if (!ic::admits_dispatch_lifecycle(attach, shutdown, closed, passive_terminal, phase)) {
            return HostDispatchError::invalid_lifecycle;
        }
        return HostDispatchError::none;
    case HostLifecyclePhase::shutdown:
        return HostDispatchError::after_shutdown;
    }
    // A phase outside the enumeration admits nothing.
    return HostDispatchError::invalid_lifecycle;
}
} // namespace

namespace detail {

bool known_drag_effects(DragEffect effects) noexcept {
    constexpr std::uint8_t known =
        static_cast<std::uint8_t>(DragEffect::copy) |
        static_cast<std::uint8_t>(DragEffect::move) |
        static_cast<std::uint8_t>(DragEffect::link);
    const std::uint8_t bits = static_cast<std::uint8_t>(effects);
    return (bits & ~known) == 0;
}

bool valid_drag_items(const std::vector<DragDataItem>& items) noexcept {
    if (items.size() > DragLimits::maximum_items) {
        return false;
    }
    std::size_t total = 0;
    for (const DragDataItem& item : items) {
        bool valid = false;
        if (const DragTextData* data = std::get_if<DragTextData>(&item)) {
            valid = (*data).text_utf8.size() <=
                        DragLimits::maximum_text_bytes &&
                    (*data).text_utf8.find('\0') == std::string::npos &&
                    valid_utf8((*data).text_utf8) &&
                    add_drag_size(total, (*data).text_utf8.size());
        } else if (const DragFileListData* data =
                       std::get_if<DragFileListData>(&item)) {
            valid = !(*data).paths_utf8.empty() &&
                    (*data).paths_utf8.size() <= DragLimits::maximum_paths;
            for (const std::string& path : (*data).paths_utf8) {
                if (!valid || path.empty() ||
                    path.size() > DragLimits::maximum_path_bytes ||
                    path.find('\0') != std::string::npos ||
                    !valid_utf8(path) || !add_drag_size(total, path.size())) {
                    valid = false;
                    break;
                }
            }
        } else if (const DragBinaryData* data =
                       std::get_if<DragBinaryData>(&item)) {
            valid = !(*data).media_type.empty() &&
                    (*data).media_type.size() <=
                        DragLimits::maximum_media_type_bytes &&
                    (*data).media_type.find('\0') == std::string::npos &&
                    valid_utf8((*data).media_type) &&
                    add_drag_size(total, (*data).bytes.size());
        }
        if (!valid) {
            return false;
        }
    }
    return true;
}

} // namespace detail
HostSession::HostSession(Window& window,
                         HostCapabilities capabilities,
                         HostServices* services)
    : window_(window.window_ref()), services_(services), ui_thread_(stxlib::current_thread()) {
    snapshot_.capabilities = std::move(capabilities);
    // The window's non-opaque declaration is fixed from here (stage T5).
    detail::SurfaceHostAccess::mark_session_bound(window);
    // The automation core finds the session of a window here
    // (session_directory.hpp); forgotten in shutdown and the destructor.
    detail::note_window_session(window, *this);
    if (services_ != nullptr) {
        if (window.host_services() != nullptr && window.host_services() != services_) {
            throw std::logic_error("GUI.Forms Window already has an active host service seam");
        }
        window.invalidate_cursor_interaction(CursorError::stale_window);
        detail::SurfaceHostAccess::set_host_services(window, services_);
        // The window's corners as the host makes them, which its band
        // follows (owner, 2026-11-08).
        window.apply_corner_preference();
        capture_observation_ = window.pointer_capture_changed().subscribe(
            Delegate<const PointerCaptureChange&>::bind<
                HostSession, &HostSession::observe_pointer_capture>(*this));
        modal_observation_ = (*services_).modal_changed().subscribe(
            Delegate<const HostModalTransition&>::bind<
                HostSession, &HostSession::observe_modal_transition>(*this));
    }
    // The host's keyboard-command defaults (owner decisions DK-30, G5-1, and
    // DK-44): they seed the window's bindings, shared with the application's
    // other windows, unless an earlier session seeded them. The
    // application's rebindings stay over them.
    window.install_host_key_map(snapshot_.capabilities.key_map);
    // The host's locale (exhibit gap E11), where it read one: it seeds the
    // application's locale unless the program set one or an earlier
    // session seeded it.
    if (snapshot_.capabilities.supports(HostCapability::locale)) {
        window.install_host_locale(snapshot_.capabilities.locale);
    }
}

HostSession::~HostSession() {
    shutdown();
    detail::forget_window_session(*this);
}

void HostSession::observe_pointer_capture(
    const PointerCaptureChange& change) {
    if (!snapshot_.shutdown && services_ != nullptr) {
        static_cast<void>((*services_).set_pointer_capture(
            change.captured, change.pointer_id));
    }
}

void HostSession::observe_modal_transition(
    const HostModalTransition& transition) {
    if (snapshot_.shutdown) return;
    snapshot_.modal_depth = transition.depth;
    ++snapshot_.modal_transitions;
    Window* const window = window_.get();
    if (transition.entering && window != nullptr) {
        (*window).release_pointer();
        // A modal window or dialog gives the pointer back (relative_pointer.hpp).
        static_cast<void>((*window).end_relative_pointer(RelativePointerEnd::modal));
    }
}

template <typename PayloadValue>
void HostSession::DispatchVisitor::operator()(
    PayloadValue& payload) const {
    using Payload =
        std::remove_cv_t<std::remove_reference_t<PayloadValue>>;
    if constexpr (std::is_same_v<Payload, HostAttachEvent>) {
        UpdateScope update = (*window).begin_update();
        (*window).resize(payload.client_size);
        (*window).set_scale(payload.scale);
        update.close();
        (*session).snapshot_.attached = true;
        (*session).set_phase(HostLifecyclePhase::attached);
    } else if constexpr (std::is_same_v<Payload, HostResizeEvent>) {
        (*window).resize(payload.client_size);
    } else if constexpr (std::is_same_v<Payload, HostScaleEvent>) {
        (*window).set_scale(payload.scale);
    } else if constexpr (std::is_same_v<Payload, HostActivationEvent>) {
        (*session).snapshot_.active = payload.active;
        (*window).set_active(payload.active);
    } else if constexpr (std::is_same_v<Payload, HostOcclusionEvent>) {
        (*session).snapshot_.occluded = payload.occluded;
        (*window).set_occluded(
            payload.occluded,
            FrameTime{std::chrono::nanoseconds(
                (*event).timestamp_nanoseconds)});
    } else if constexpr (std::is_same_v<Payload, HostDisplayEvent>) {
        ++(*session).snapshot_.display_changes;
        (*session).snapshot_.monitor_count = payload.monitors.size();
    } else if constexpr (std::is_same_v<Payload, DragEvent>) {
        ++(*session).snapshot_.drag_events;
        (*session).snapshot_.drag_drops +=
            payload.action == DragAction::drop ? 1U : 0U;
        const DragDispatchResult drag =
            (*window).dispatch_drag(std::move(payload));
        (*result).handled = drag.handled;
        (*result).drag_effect = drag.accepted_effect;
        (*result).input_deferred = drag.deferred;
        (*result).input_capacity_rejected = drag.capacity_rejected;
    } else if constexpr (std::is_same_v<Payload, PointerEvent>) {
        // An event the host did not stamp itself carries the HostEvent's
        // time (PointerEvent::timestamp_nanoseconds).
        if (payload.timestamp_nanoseconds == 0U) payload.timestamp_nanoseconds = (*event).timestamp_nanoseconds;
        const DeferredInputSnapshot before =
            (*window).deferred_input_snapshot();
        (*result).handled =
            (*window).dispatch_pointer(std::move(payload));
        const DeferredInputSnapshot after =
            (*window).deferred_input_snapshot();
        (*result).input_deferred = after.deferred > before.deferred;
        (*result).input_capacity_rejected =
            after.rejected_capacity > before.rejected_capacity;
    } else if constexpr (std::is_same_v<Payload, KeyEvent>) {
        const DeferredInputSnapshot before =
            (*window).deferred_input_snapshot();
        (*result).handled =
            (*window).dispatch_key(std::move(payload));
        const DeferredInputSnapshot after =
            (*window).deferred_input_snapshot();
        (*result).input_deferred = after.deferred > before.deferred;
        (*result).input_capacity_rejected =
            after.rejected_capacity > before.rejected_capacity;
    } else if constexpr (std::is_same_v<Payload, TextInputEvent>) {
        const DeferredInputSnapshot before =
            (*window).deferred_input_snapshot();
        (*result).handled =
            (*window).dispatch_text(std::move(payload));
        const DeferredInputSnapshot after =
            (*window).deferred_input_snapshot();
        (*result).input_deferred = after.deferred > before.deferred;
        (*result).input_capacity_rejected =
            after.rejected_capacity > before.rejected_capacity;
    } else if constexpr (std::is_same_v<Payload, HostCloseRequest>) {
        ++(*session).snapshot_.close_requests;
        const bool hide_on_accept = payload.hide_on_accept;
        const bool asked_only = payload.query_only;
        (*session).closing_.emit(payload);
        (*result).close_allowed = !payload.cancel;
        (*session).snapshot_.close_cancellations += payload.cancel ? 1U : 0U;
        // An allowed session-end request stays attached: the platform may
        // still call the session's end off, and the host reports the close
        // itself when it happens (HostCloseReason::session_end).
        const bool session_end = payload.reason == HostCloseReason::session_end;
        // A query (a quit asking every window before it closes any) never
        // authorizes the close, whatever the observers answered or changed
        // in the payload during notification.
        if (!payload.cancel && !hide_on_accept && !session_end && !asked_only) {
            (*session).set_phase(HostLifecyclePhase::close_authorized);
        }
    } else if constexpr (std::is_same_v<Payload, HostClosedEvent>) {
        (*session).set_phase(HostLifecyclePhase::closed);
        (*window).host_close_reason_.set(payload.reason);
        (*session).snapshot_.attached = false;
        (*session).snapshot_.closed = true;
        (*session).snapshot_.active = false;
        (*session).snapshot_.occluded = false;
        // Controls hear of the released capture, and may post work about
        // it, while the dispatcher still accepts; the shutdown then cancels
        // that work. Shutting down first would make the post throw.
        (*window).release_pointer();
        (*window).cancel_drag();
        (*window).cancel_frame_requests();
        (*window).shutdown_dispatcher();
    } else if constexpr (std::is_same_v<Payload, HostShutdownEvent>) {
        (*session).shutdown();
    } else if constexpr (std::is_same_v<Payload, HostCompositorEvent>) {
        const bool host_preferences =
            (*session).snapshot_.capabilities.supports(HostCapability::presentation_preferences);
        (*window).set_compositor_state(payload.state, host_preferences);
    } else if constexpr (std::is_same_v<Payload, HostInputSettingsEvent>) {
        // Only the reported fields change; the application's own settings
        // stay as they are.
        PresentationSettings settings = (*window).presentation_base();
        settings.double_click_milliseconds = payload.double_click_milliseconds;
        settings.double_click_origin = payload.double_click_origin;
        // The menu show delay is no host's (owner, 2026-11-22: 300 ms on
        // every host): the window's, default or set by the program, stays.
        const std::uint32_t menu_show = settings.pointer.menu_show_milliseconds;
        const SettingOrigin menu_show_origin = settings.pointer.menu_show_origin;
        settings.pointer = payload.pointer;
        settings.pointer.menu_show_milliseconds = menu_show;
        settings.pointer.menu_show_origin = menu_show_origin;
        settings.caret = payload.caret;
        detail::SurfaceHostAccess::set_host_presentation(*window, settings);
    } else if constexpr (std::is_same_v<Payload, HostContextRequestEvent>) {
        (*result).handled = (*window).dispatch_context_request(payload.origin, payload.position);
    } else if constexpr (std::is_same_v<Payload, HostHoverEvent>) {
        (*window).dispatch_host_hover(payload.position);
    } else if constexpr (std::is_same_v<Payload, HostRelativeMotionEvent>) {
        if (payload.timestamp_nanoseconds == 0U) payload.timestamp_nanoseconds = (*event).timestamp_nanoseconds;
        ++(*session).snapshot_.relative_motion_events;
        (*result).handled = (*window).dispatch_relative_motion(payload);
    } else if constexpr (std::is_same_v<Payload, GestureEvent>) {
        if (payload.timestamp_nanoseconds == 0U) payload.timestamp_nanoseconds = (*event).timestamp_nanoseconds;
        ++(*session).snapshot_.gesture_events;
        (*result).handled = (*window).dispatch_gesture(std::move(payload));
    } else if constexpr (std::is_same_v<Payload, HostWindowStateEvent>) {
        // Full screen first: a band that hides there gives up its
        // reservation before the corners' strips are damaged.
        (*window).report_full_screen(payload.full_screen);
        (*window).apply_corner_preference();
    } else if constexpr (std::is_same_v<Payload, HostSafeInsetsEvent>) {
        // Validated with the geometry above (stage VA).
        (*window).set_safe_insets(payload.insets);
    }
}

HostDispatchResult HostSession::dispatch(HostEvent event) {
    HostDispatchResult result;
    if (stxlib::current_thread() != ui_thread_) {
        result.error = HostDispatchError::wrong_thread;
        return result;
    } else if (!ic::admits_dispatch_sequence(event.sequence)) {
        result.error = HostDispatchError::sequence_zero;
    } else if (!ic::admits_dispatch_order(event.sequence, snapshot_.last_sequence)) {
        result.error = HostDispatchError::non_monotonic_sequence;
    } else if (!snapshot_.shutdown && window_.get() == nullptr) {
        // The window was destroyed first: the session ends with it.
        end_without_window();
        result.error = HostDispatchError::after_shutdown;
    } else if (snapshot_.shutdown) {
        result.error = HostDispatchError::after_shutdown;
    }
    if (result.accepted()) {
        const bool attach = std::holds_alternative<HostAttachEvent>(event.payload);
        const bool shutdown = std::holds_alternative<HostShutdownEvent>(event.payload);
        const bool closed = std::holds_alternative<HostClosedEvent>(event.payload);
        bool passive_terminal = false;
        if (const HostActivationEvent* activation =
                std::get_if<HostActivationEvent>(&event.payload)) {
            passive_terminal = !(*activation).active;
        } else if (const HostOcclusionEvent* occlusion =
                       std::get_if<HostOcclusionEvent>(&event.payload)) {
            passive_terminal = (*occlusion).occluded;
        }
        result.error = lifecycle_refusal(snapshot_.phase, attach, shutdown, closed,
                                         passive_terminal);
    }
    if (!result.accepted()) {
        ++snapshot_.events_rejected;
        observed_.emit(event, result);
        return result;
    }
    result = admit(event, InputOrigin::user);
    return result;
}

namespace {
// The payloads a model may send through dispatch_model_input: the user's
// own kinds of input, without drags (a model drags with pointer moves) and
// relative motion (a mode a program asks of the host).
bool model_input_payload(const HostEventPayload& payload) noexcept {
    const bool admitted = std::holds_alternative<PointerEvent>(payload) ||
                          std::holds_alternative<KeyEvent>(payload) ||
                          std::holds_alternative<TextInputEvent>(payload) ||
                          std::holds_alternative<HostHoverEvent>(payload) ||
                          std::holds_alternative<HostContextRequestEvent>(payload);
    return admitted;
}

// Whether a payload is the user's input, which a modal window and the user
// input gate hold back.
bool input_payload(const HostEventPayload& payload) noexcept {
    const bool input = std::holds_alternative<PointerEvent>(payload) ||
                       std::holds_alternative<KeyEvent>(payload) ||
                       std::holds_alternative<TextInputEvent>(payload) ||
                       std::holds_alternative<DragEvent>(payload) ||
                       std::holds_alternative<HostContextRequestEvent>(payload) ||
                       std::holds_alternative<HostHoverEvent>(payload) ||
                       std::holds_alternative<HostRelativeMotionEvent>(payload) ||
                       std::holds_alternative<GestureEvent>(payload);
    return input;
}
} // namespace

HostDispatchResult HostSession::dispatch_model_input(HostEventPayload payload, std::uint64_t timestamp_nanoseconds) {
    HostDispatchResult result;
    if (std::this_thread::get_id() != ui_thread_) {
        result.error = HostDispatchError::wrong_thread;
        return result;
    }
    // Sequence zero: the model's events take no place in the host's
    // sequence, and observers see them as the model's.
    HostEvent event{0U, timestamp_nanoseconds, std::move(payload)};
    if (!model_input_payload(event.payload)) {
        result.error = HostDispatchError::invalid_payload;
    } else if (!snapshot_.shutdown && window_.get() == nullptr) {
        end_without_window();
        result.error = HostDispatchError::after_shutdown;
    } else if (snapshot_.shutdown) {
        result.error = HostDispatchError::after_shutdown;
    } else {
        result.error = lifecycle_refusal(snapshot_.phase, false, false, false, false);
    }
    if (!result.accepted()) {
        ++snapshot_.events_rejected;
        observed_.emit(event, result);
        return result;
    }
    result = admit(event, InputOrigin::model);
    return result;
}

HostDispatchResult HostSession::admit(HostEvent& event, InputOrigin origin) {
    HostDispatchResult result;
    bool invalid_geometry = false;
    if (const HostAttachEvent* attach =
            std::get_if<HostAttachEvent>(&event.payload)) {
        invalid_geometry = !valid_size((*attach).client_size) ||
                           !valid_scale((*attach).scale);
    } else if (const HostResizeEvent* resize =
                   std::get_if<HostResizeEvent>(&event.payload)) {
        invalid_geometry = !valid_size((*resize).client_size);
    } else if (const HostScaleEvent* scale =
                   std::get_if<HostScaleEvent>(&event.payload)) {
        invalid_geometry = !valid_scale((*scale).scale);
    } else if (const HostDisplayEvent* display =
                   std::get_if<HostDisplayEvent>(&event.payload)) {
        invalid_geometry = !valid_monitor_set((*display).monitors);
    } else if (const HostSafeInsetsEvent* safe =
                   std::get_if<HostSafeInsetsEvent>(&event.payload)) {
        invalid_geometry = !valid_safe_insets((*safe).insets);
    }
    if (invalid_geometry) {
        result.error = HostDispatchError::invalid_geometry;
        ++snapshot_.events_rejected;
        observed_.emit(event, result);
        return result;
    }
    // A compositor report is usable only when its opaque backdrop is
    // supported and it is later than the window's last one (stage T5).
    if (const HostCompositorEvent* compositor = std::get_if<HostCompositorEvent>(&event.payload);
        compositor != nullptr &&
        (!compositor_state_valid((*compositor).state) ||
         !((*window_.get()).compositor_state().generation < (*compositor).state.generation))) {
        result.error = HostDispatchError::invalid_payload;
        ++snapshot_.events_rejected;
        observed_.emit(event, result);
        return result;
    }
    if (const gui_forms::DragEvent* drag = std::get_if<DragEvent>(&event.payload);
        drag != nullptr && !valid_drag_event(*drag)) {
        result.error = HostDispatchError::invalid_payload;
        ++snapshot_.events_rejected;
        observed_.emit(event, result);
        return result;
    }
    // Typed text enters the text pipeline as UTF-8; a host that passes
    // anything else (Latin-1 from XLookupString, say) is refused here
    // (A-gf-host-01-F5), as drag text already is.
    if (const TextInputEvent* typed = std::get_if<TextInputEvent>(&event.payload);
        typed != nullptr && !valid_utf8((*typed).text_utf8)) {
        result.error = HostDispatchError::invalid_payload;
        ++snapshot_.events_rejected;
        observed_.emit(event, result);
        return result;
    }
    if (!valid_pointer_report(event.payload)) {
        result.error = HostDispatchError::invalid_payload;
        ++snapshot_.events_rejected;
        observed_.emit(event, result);
        return result;
    }
    if (const HostInputSettingsEvent* input = std::get_if<HostInputSettingsEvent>(&event.payload);
        input != nullptr && !valid_input_settings(*input)) {
        result.error = HostDispatchError::invalid_payload;
        ++snapshot_.events_rejected;
        observed_.emit(event, result);
        return result;
    }

    if (origin == InputOrigin::user) {
        snapshot_.last_sequence = event.sequence;
    } else {
        ++snapshot_.model_input_events;
    }
    ++snapshot_.events_accepted;
    const bool input = input_payload(event.payload);
    const bool modal_input = snapshot_.modal_depth != 0 && input;
    if (modal_input) {
        ++snapshot_.modal_input_suppressions;
        observed_.emit(event, result);
        return result;
    }
    // A model holds the interface (model.directed or suspend_input): the
    // user's input is counted and dropped, never the model's. The window is
    // alive here (dispatch and dispatch_model_input saw it).
    Window* const gated = window_.get();
    if (origin == InputOrigin::user && input && gated != nullptr && detail::user_input_held(*gated)) {
        ++snapshot_.user_input_held;
        observed_.emit(event, result);
        return result;
    }
    // Event callbacks are ordinary C++ and propagate through Event::emit. The
    // portable host seam is where they meet the host's native callback, so
    // the delivery goes through the fail-fast boundary: an application
    // exception ends the process with a report and never unwinds into
    // AppKit, Win32, X11 or Wayland frames.
    gui_forms::detail::call_failing_fast("host_session.dispatch", &HostSession::deliver, *this, event, result);
    return result;
}

// Delivers an accepted event to the window, then to the session's
// observers. Runs inside the fail-fast boundary (dispatch).
void HostSession::deliver(HostEvent& event, HostDispatchResult& result) {
    // dispatch saw the window alive, and nothing has run since.
    Window* const window = window_.get();
    if (window == nullptr) {
        end_without_window();
        result.error = HostDispatchError::after_shutdown;
        observed_.emit(event, result);
        return;
    }
    std::visit(DispatchVisitor{this, window, &result, &event}, event.payload);
    // The native window exists from the attach on (a host binds the
    // session before it creates the window, when a chrome request finds
    // nothing to change): every chrome property the program wrote
    // before then reaches it now, start position included, before the
    // window is first shown. A later write of the same value sends
    // nothing, so this is the only time they are sent.
    // A handler during the attach may have destroyed the window: read it
    // again.
    Window* const attached = window_.get();
    if (services_ != nullptr && attached != nullptr && std::holds_alternative<HostAttachEvent>(event.payload)) {
        (*attached).apply_window_chrome(WindowChromeFields{WindowChromeFields::all_bits});
    }
    observed_.emit(event, result);
}

void HostSession::shutdown() noexcept {
    if (snapshot_.shutdown || stxlib::current_thread() != ui_thread_) {
        return;
    }
    snapshot_.active = false;
    snapshot_.attached = false;
    snapshot_.occluded = false;
    // A window destroyed first has done its own teardown; its dispatcher
    // and mutex are gone with it.
    Window* const window = window_.get();
    if (window != nullptr) {
        (*window).invalidate_cursor_interaction(CursorError::closing);
        // As for the closed event: release, then shut the dispatcher down.
        (*window).release_pointer();
        (*window).cancel_drag();
        (*window).cancel_frame_requests();
        (*window).shutdown_dispatcher();
        detail::SurfaceHostAccess::release_host_services(*window, services_);
    } else {
        capture_observation_.disconnect();
        modal_observation_.disconnect();
    }
    detail::forget_window_session(*this);
    snapshot_.shutdown = true;
    set_phase(HostLifecyclePhase::shutdown);
}

void HostSession::end_without_window() noexcept {
    detail::forget_window_session(*this);
    snapshot_.active = false;
    snapshot_.attached = false;
    snapshot_.occluded = false;
    capture_observation_.disconnect();
    modal_observation_.disconnect();
    snapshot_.shutdown = true;
    snapshot_.phase = HostLifecyclePhase::shutdown;
}

void HostSession::set_phase(HostLifecyclePhase phase) noexcept {
    snapshot_.phase = phase;
    // The window, when it is still alive, reports the same phase
    // (Window::host_lifecycle_phase); a window destroyed first is not read.
    Window* const window = window_.get();
    if (window != nullptr) {
        (*window).host_phase_ = phase;
    }
}

HostSessionSnapshot HostSession::snapshot() const {
    return snapshot_;
}

} // namespace gui_forms
