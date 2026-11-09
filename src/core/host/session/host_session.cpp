#include "gui_forms/host.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <sstream>
#include <type_traits>
#include <unordered_set>

namespace gui_forms {
namespace {
bool valid_size(Size size) noexcept {
    return std::isfinite(size.width) && std::isfinite(size.height) &&
           size.width > 0.0 && size.height > 0.0;
}

bool valid_scale(double scale) noexcept {
    return std::isfinite(scale) && scale > 0.0;
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
    constexpr std::uint8_t known_effects =
        static_cast<std::uint8_t>(DragEffect::copy) |
        static_cast<std::uint8_t>(DragEffect::move) |
        static_cast<std::uint8_t>(DragEffect::link);
    const std::uint8_t effects = static_cast<std::uint8_t>(event.allowed_effects);
    if (event.session_id == 0 || !std::isfinite(event.position.x) ||
        !std::isfinite(event.position.y) || (effects & ~known_effects) != 0 ||
        event.accepted_effect != DragEffect::none ||
        event.items.size() > DragLimits::maximum_items) {
        return false;
    }
    if (event.action != DragAction::leave &&
        (event.items.empty() || event.allowed_effects == DragEffect::none)) {
        return false;
    }
    std::size_t total = 0;
    for (const DragDataItem& item : event.items) {
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
    return true;
}

bool valid_input_settings(const HostInputSettingsEvent& input) noexcept {
    const bool interval = input.double_click_milliseconds != 0U &&
                          input.double_click_milliseconds <= maximum_double_click_milliseconds;
    const bool origin = input.double_click_origin == SettingOrigin::default_value ||
                        input.double_click_origin == SettingOrigin::host;
    return interval && origin && valid_pointer_settings(input.pointer);
}

HostDispatchError lifecycle_refusal(HostLifecyclePhase phase, bool attach, bool shutdown,
                                    bool closed, bool passive_terminal) noexcept {
    switch (phase) {
    case HostLifecyclePhase::constructed:
        if (!attach && !shutdown) return HostDispatchError::invalid_lifecycle;
        return HostDispatchError::none;
    case HostLifecyclePhase::attached:
        if (attach) return HostDispatchError::invalid_lifecycle;
        return HostDispatchError::none;
    case HostLifecyclePhase::close_authorized:
        if (!closed && !shutdown && !passive_terminal) return HostDispatchError::invalid_lifecycle;
        return HostDispatchError::none;
    case HostLifecyclePhase::closed:
        if (!shutdown) return HostDispatchError::invalid_lifecycle;
        return HostDispatchError::none;
    case HostLifecyclePhase::shutdown:
        return HostDispatchError::after_shutdown;
    }
    // A phase outside the enumeration admits nothing.
    return HostDispatchError::invalid_lifecycle;
}
} // namespace
HostSession::HostSession(Window& window,
                         HostCapabilities capabilities,
                         HostServices* services)
    : window_(&window), services_(services), ui_thread_(std::this_thread::get_id()) {
    snapshot_.capabilities = std::move(capabilities);
    // The window's non-opaque declaration is fixed from here (stage T5).
    window.host_session_bound_ = true;
    if (services_ != nullptr) {
        if (window.host_services_ != nullptr && window.host_services_ != services_) {
            throw std::logic_error("GUI.Forms Window already has an active host service seam");
        }
        window.invalidate_cursor_interaction(CursorError::stale_window);
        window.host_services_ = services_;
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
}

HostSession::~HostSession() {
    shutdown();
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
    if (transition.entering && window_ != nullptr) {
        (*window_).release_pointer();
    }
}

template <typename PayloadValue>
void HostSession::DispatchVisitor::operator()(
    PayloadValue& payload) const {
    using Payload =
        std::remove_cv_t<std::remove_reference_t<PayloadValue>>;
    if constexpr (std::is_same_v<Payload, HostAttachEvent>) {
        UpdateScope update = (*(*session).window_).begin_update();
        (*(*session).window_).resize(payload.client_size);
        (*(*session).window_).set_scale(payload.scale);
        update.close();
        (*session).snapshot_.attached = true;
        (*session).snapshot_.phase = HostLifecyclePhase::attached;
    } else if constexpr (std::is_same_v<Payload, HostResizeEvent>) {
        (*(*session).window_).resize(payload.client_size);
    } else if constexpr (std::is_same_v<Payload, HostScaleEvent>) {
        (*(*session).window_).set_scale(payload.scale);
    } else if constexpr (std::is_same_v<Payload, HostActivationEvent>) {
        (*session).snapshot_.active = payload.active;
        (*(*session).window_).set_active(payload.active);
    } else if constexpr (std::is_same_v<Payload, HostOcclusionEvent>) {
        (*session).snapshot_.occluded = payload.occluded;
        (*(*session).window_).set_occluded(
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
            (*(*session).window_).dispatch_drag(std::move(payload));
        (*result).handled = drag.handled;
        (*result).drag_effect = drag.accepted_effect;
        (*result).input_deferred = drag.deferred;
        (*result).input_capacity_rejected = drag.capacity_rejected;
    } else if constexpr (std::is_same_v<Payload, PointerEvent>) {
        const DeferredInputSnapshot before =
            (*(*session).window_).deferred_input_snapshot();
        (*result).handled =
            (*(*session).window_).dispatch_pointer(std::move(payload));
        const DeferredInputSnapshot after =
            (*(*session).window_).deferred_input_snapshot();
        (*result).input_deferred = after.deferred > before.deferred;
        (*result).input_capacity_rejected =
            after.rejected_capacity > before.rejected_capacity;
    } else if constexpr (std::is_same_v<Payload, KeyEvent>) {
        const DeferredInputSnapshot before =
            (*(*session).window_).deferred_input_snapshot();
        (*result).handled =
            (*(*session).window_).dispatch_key(std::move(payload));
        const DeferredInputSnapshot after =
            (*(*session).window_).deferred_input_snapshot();
        (*result).input_deferred = after.deferred > before.deferred;
        (*result).input_capacity_rejected =
            after.rejected_capacity > before.rejected_capacity;
    } else if constexpr (std::is_same_v<Payload, TextInputEvent>) {
        const DeferredInputSnapshot before =
            (*(*session).window_).deferred_input_snapshot();
        (*result).handled =
            (*(*session).window_).dispatch_text(std::move(payload));
        const DeferredInputSnapshot after =
            (*(*session).window_).deferred_input_snapshot();
        (*result).input_deferred = after.deferred > before.deferred;
        (*result).input_capacity_rejected =
            after.rejected_capacity > before.rejected_capacity;
    } else if constexpr (std::is_same_v<Payload, HostCloseRequest>) {
        ++(*session).snapshot_.close_requests;
        const bool hide_on_accept = payload.hide_on_accept;
        (*session).closing_.emit(payload);
        (*result).close_allowed = !payload.cancel;
        (*session).snapshot_.close_cancellations += payload.cancel ? 1U : 0U;
        if (!payload.cancel && !hide_on_accept) {
            (*session).snapshot_.phase = HostLifecyclePhase::close_authorized;
        }
    } else if constexpr (std::is_same_v<Payload, HostClosedEvent>) {
        (*session).snapshot_.phase = HostLifecyclePhase::closed;
        (*session).snapshot_.attached = false;
        (*session).snapshot_.closed = true;
        (*session).snapshot_.active = false;
        (*session).snapshot_.occluded = false;
        // Controls hear of the released capture, and may post work about
        // it, while the dispatcher still accepts; the shutdown then cancels
        // that work. Shutting down first would make the post throw.
        (*(*session).window_).release_pointer();
        (*(*session).window_).cancel_drag();
        (*(*session).window_).cancel_frame_requests();
        (*(*session).window_).shutdown_dispatcher();
    } else if constexpr (std::is_same_v<Payload, HostShutdownEvent>) {
        (*session).shutdown();
    } else if constexpr (std::is_same_v<Payload, HostCompositorEvent>) {
        const bool host_preferences =
            (*session).snapshot_.capabilities.supports(HostCapability::presentation_preferences);
        (*(*session).window_).set_compositor_state(payload.state, host_preferences);
    } else if constexpr (std::is_same_v<Payload, HostInputSettingsEvent>) {
        // Only the reported fields change; the application's own settings
        // stay as they are.
        PresentationSettings settings = (*(*session).window_).presentation_base();
        settings.double_click_milliseconds = payload.double_click_milliseconds;
        settings.double_click_origin = payload.double_click_origin;
        settings.pointer = payload.pointer;
        (*(*session).window_).set_host_presentation(settings);
    } else if constexpr (std::is_same_v<Payload, HostContextRequestEvent>) {
        (*result).handled = (*(*session).window_).dispatch_context_request(payload.origin, payload.position);
    } else if constexpr (std::is_same_v<Payload, HostHoverEvent>) {
        (*(*session).window_).dispatch_host_hover(payload.position);
    } else if constexpr (std::is_same_v<Payload, HostWindowStateEvent>) {
        // Full screen first: a band that hides there gives up its
        // reservation before the corners' strips are damaged.
        (*(*session).window_).report_full_screen(payload.full_screen);
        (*(*session).window_).apply_corner_preference();
    }
}

HostDispatchResult HostSession::dispatch(HostEvent event) {
    HostDispatchResult result;
    if (std::this_thread::get_id() != ui_thread_) {
        result.error = HostDispatchError::wrong_thread;
        return result;
    } else if (event.sequence == 0) {
        result.error = HostDispatchError::sequence_zero;
    } else if (event.sequence <= snapshot_.last_sequence) {
        result.error = HostDispatchError::non_monotonic_sequence;
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
         !((*window_).compositor_state().generation < (*compositor).state.generation))) {
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

    snapshot_.last_sequence = event.sequence;
    ++snapshot_.events_accepted;
    const bool modal_input = snapshot_.modal_depth != 0 &&
        (std::holds_alternative<PointerEvent>(event.payload) ||
         std::holds_alternative<KeyEvent>(event.payload) ||
         std::holds_alternative<TextInputEvent>(event.payload) ||
         std::holds_alternative<DragEvent>(event.payload) ||
         std::holds_alternative<HostContextRequestEvent>(event.payload) ||
         std::holds_alternative<HostHoverEvent>(event.payload));
    if (modal_input) {
        ++snapshot_.modal_input_suppressions;
        observed_.emit(event, result);
        return result;
    }
    try {
        std::visit(DispatchVisitor{this, &result, &event}, event.payload);
        // The native window exists from the attach on (a host binds the
        // session before it creates the window, when a chrome request finds
        // nothing to change): every chrome property the program wrote
        // before then reaches it now, start position included, before the
        // window is first shown. A later write of the same value sends
        // nothing, so this is the only time they are sent.
        if (services_ != nullptr && std::holds_alternative<HostAttachEvent>(event.payload)) {
            (*window_).apply_window_chrome(WindowChromeFields{WindowChromeFields::all_bits});
        }
        observed_.emit(event, result);
    } catch (...) {
        // Event callbacks are ordinary C++ and deliberately propagate through
        // Event::emit. The portable host seam is the foreign-ABI boundary: no
        // application exception may unwind into AppKit, Win32, X11, or
        // Wayland. Retain the consumed sequence, report the fault, and revoke
        // transient input ownership that a partially delivered event may have
        // acquired.
        result.handled = false;
        result.input_deferred = false;
        result.input_capacity_rejected = false;
        result.close_allowed = false;
        result.drag_effect = DragEffect::none;
        result.error = HostDispatchError::callback_fault;
        ++snapshot_.callback_faults;
        if (window_ != nullptr) {
            try {
                (*window_).release_pointer();
                (*window_).cancel_drag();
            } catch (...) {
                // Cleanup is best effort at an exception boundary. The
                // original callback fault remains the observable result.
            }
        }
    }
    return result;
}

void HostSession::shutdown() noexcept {
    if (snapshot_.shutdown || std::this_thread::get_id() != ui_thread_) {
        return;
    }
    snapshot_.active = false;
    snapshot_.attached = false;
    snapshot_.occluded = false;
    if (window_ != nullptr) {
        (*window_).invalidate_cursor_interaction(CursorError::closing);
        // As for the closed event: release, then shut the dispatcher down.
        (*window_).release_pointer();
        (*window_).cancel_drag();
        (*window_).cancel_frame_requests();
        (*window_).shutdown_dispatcher();
        if ((*window_).host_services_ == services_) (*window_).host_services_ = nullptr;
    }
    snapshot_.shutdown = true;
    snapshot_.phase = HostLifecyclePhase::shutdown;
}

HostSessionSnapshot HostSession::snapshot() const {
    return snapshot_;
}

} // namespace gui_forms
