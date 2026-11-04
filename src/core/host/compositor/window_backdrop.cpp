#include "gui_forms/host/compositor/window_backdrop.hpp"

#include <sstream>

namespace gui_forms {

const char* window_backdrop_name(WindowBackdrop backdrop) noexcept {
    switch (backdrop) {
    case WindowBackdrop::opaque: return "opaque";
    case WindowBackdrop::transparent: return "transparent";
    case WindowBackdrop::window_material: return "window_material";
    case WindowBackdrop::transient_material: return "transient_material";
    }
    // A value outside the enumeration.
    return "unknown";
}

const char* window_backdrop_support_name(WindowBackdropSupport support) noexcept {
    switch (support) {
    case WindowBackdropSupport::supported: return "supported";
    case WindowBackdropSupport::host_unsupported: return "host_unsupported";
    case WindowBackdropSupport::os_version: return "os_version";
    case WindowBackdropSupport::no_compositor: return "no_compositor";
    }
    return "unknown";
}

const char* compositor_presence_name(CompositorPresence presence) noexcept {
    switch (presence) {
    case CompositorPresence::unknown: return "unknown";
    case CompositorPresence::present: return "present";
    case CompositorPresence::absent: return "absent";
    }
    return "invalid";
}

const char* window_backdrop_reason_name(WindowBackdropReason reason) noexcept {
    switch (reason) {
    case WindowBackdropReason::granted: return "granted";
    case WindowBackdropReason::not_declared: return "not_declared";
    case WindowBackdropReason::host_unsupported: return "host_unsupported";
    case WindowBackdropReason::os_version: return "os_version";
    case WindowBackdropReason::no_compositor: return "no_compositor";
    case WindowBackdropReason::high_contrast: return "high_contrast";
    case WindowBackdropReason::reduced_transparency: return "reduced_transparency";
    case WindowBackdropReason::transparency_disabled: return "transparency_disabled";
    case WindowBackdropReason::backend_failure: return "backend_failure";
    }
    return "unknown";
}

std::string CompositorState::to_json() const {
    std::ostringstream output;
    output << "{\"generation\":" << generation.value << ",\"compositing\":\""
           << compositor_presence_name(compositing) << "\",\"support\":{";
    for (std::size_t index = 0; index < window_backdrop_count; ++index) {
        const WindowBackdrop backdrop = static_cast<WindowBackdrop>(index);
        output << (index == 0U ? "\"" : ",\"") << window_backdrop_name(backdrop) << "\":\""
               << window_backdrop_support_name(support[index]) << '"';
    }
    output << "},\"transparency_enabled\":" << (transparency_enabled ? "true" : "false")
           << ",\"reduced_transparency\":" << (reduced_transparency ? "true" : "false")
           << ",\"high_contrast\":" << (high_contrast ? "true" : "false")
           << ",\"dark_scheme\":" << (dark_scheme ? "true" : "false") << '}';
    return output.str();
}

namespace {

bool support_known(WindowBackdropSupport support) noexcept {
    switch (support) {
    case WindowBackdropSupport::supported:
    case WindowBackdropSupport::host_unsupported:
    case WindowBackdropSupport::os_version:
    case WindowBackdropSupport::no_compositor:
        return true;
    }
    return false;
}

bool backdrop_known(WindowBackdrop backdrop) noexcept {
    const bool known = static_cast<std::size_t>(backdrop) < window_backdrop_count;
    return known;
}

WindowBackdropReason support_refusal(WindowBackdropSupport support) noexcept {
    switch (support) {
    case WindowBackdropSupport::supported: return WindowBackdropReason::granted;
    case WindowBackdropSupport::host_unsupported: return WindowBackdropReason::host_unsupported;
    case WindowBackdropSupport::os_version: return WindowBackdropReason::os_version;
    case WindowBackdropSupport::no_compositor: return WindowBackdropReason::no_compositor;
    }
    // A support value outside the enumeration is refused as unsupported.
    return WindowBackdropReason::host_unsupported;
}

} // namespace

bool compositor_state_valid(const CompositorState& state) noexcept {
    switch (state.compositing) {
    case CompositorPresence::unknown:
    case CompositorPresence::present:
    case CompositorPresence::absent:
        break;
    default:
        return false;
    }
    for (std::size_t index = 0; index < window_backdrop_count; ++index) {
        if (!support_known(state.support[index])) return false;
    }
    const bool opaque_supported = state.support_of(WindowBackdrop::opaque) == WindowBackdropSupport::supported;
    return opaque_supported;
}

WindowBackdropRequest WindowBackdropRequest::preferring(WindowBackdrop preferred) noexcept {
    WindowBackdropRequest request{};
    request.entries[0] = preferred;
    request.length = 1U;
    return request;
}

WindowBackdropRequest WindowBackdropRequest::with_fallback(WindowBackdrop preferred,
                                                           WindowBackdrop fallback) noexcept {
    WindowBackdropRequest request{};
    request.entries[0] = preferred;
    request.entries[1] = fallback;
    request.length = 2U;
    return request;
}

WindowBackdropRequest WindowBackdropRequest::with_fallbacks(WindowBackdrop preferred, WindowBackdrop first,
                                                            WindowBackdrop second) noexcept {
    WindowBackdropRequest request{};
    request.entries[0] = preferred;
    request.entries[1] = first;
    request.entries[2] = second;
    request.length = 3U;
    return request;
}

bool window_backdrop_request_valid(const WindowBackdropRequest& request) noexcept {
    if (request.length == 0U || request.length > WindowBackdropRequest::maximum_entries) return false;
    for (std::size_t index = 0; index < request.length; ++index) {
        const WindowBackdrop entry = request.entries[index];
        if (!backdrop_known(entry)) return false;
        const bool last = index + 1U == request.length;
        if (entry == WindowBackdrop::opaque && !last) return false;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (request.entries[earlier] == entry) return false;
        }
    }
    return true;
}

WindowBackdropReason window_backdrop_refusal(WindowBackdrop backdrop, const CompositorState& state,
                                             const WindowBackdropConditions& conditions) noexcept {
    if (backdrop == WindowBackdrop::opaque) return WindowBackdropReason::granted;
    if (!backdrop_known(backdrop)) return WindowBackdropReason::host_unsupported;
    if (!conditions.declared) return WindowBackdropReason::not_declared;
    // High contrast withdraws every effect whatever the host offers, and
    // says so: a host that misreads its compositor under a contrast theme
    // still reports the user's setting (MEASUREMENTS.md, the owner's C2 run).
    if (conditions.high_contrast) return WindowBackdropReason::high_contrast;
    const WindowBackdropReason support = support_refusal(state.support_of(backdrop));
    if (support != WindowBackdropReason::granted) return support;
    if (state.compositing == CompositorPresence::absent) return WindowBackdropReason::no_compositor;
    if (conditions.reduced_transparency) return WindowBackdropReason::reduced_transparency;
    if (!state.transparency_enabled) return WindowBackdropReason::transparency_disabled;
    return WindowBackdropReason::granted;
}

WindowBackdropGrant resolve_window_backdrop(const WindowBackdropRequest& request, const CompositorState& state,
                                            const WindowBackdropConditions& conditions) noexcept {
    WindowBackdropGrant grant{};
    grant.requested = request.preferred();
    grant.granted = WindowBackdrop::opaque;
    grant.generation = state.generation;
    grant.reason = window_backdrop_refusal(request.preferred(), state, conditions);
    for (std::size_t index = 0; index < request.length && index < WindowBackdropRequest::maximum_entries;
         ++index) {
        const WindowBackdrop entry = request.entries[index];
        const WindowBackdropReason refusal = window_backdrop_refusal(entry, state, conditions);
        if (refusal != WindowBackdropReason::granted) continue;
        grant.granted = entry;
        break;
    }
    return grant;
}

} // namespace gui_forms
