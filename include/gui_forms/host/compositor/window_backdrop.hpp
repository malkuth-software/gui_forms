#pragma once

#include "gui_forms/window/presentation/colour_scheme.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gui_forms {

// What a window shows behind its clear pixels (stage T5, owner decision
// T5-Q1): a closed set of purposes, never platform names. Each host maps
// them to its own mechanism (notes:planning/stages/T5-compositor/PLAN.md §2.2).
enum class WindowBackdrop : std::uint8_t {
    // Today's window: every presented pixel is opaque. Always available.
    opaque,
    // Clear pixels show what lies behind the window, unblurred.
    transparent,
    // The system's material for a main window.
    window_material,
    // The system's material for popups and transient panels.
    transient_material,
};

inline constexpr std::size_t window_backdrop_count = 4U;

[[nodiscard]] const char* window_backdrop_name(WindowBackdrop backdrop) noexcept;

// What the host can give for one backdrop now, before the user's settings
// are applied. A host reports one of these per backdrop.
enum class WindowBackdropSupport : std::uint8_t {
    supported,
    // The adapter does not implement it.
    host_unsupported,
    // The adapter implements it, but this operating system build lacks it.
    os_version,
    // It needs a running compositor and none is running.
    no_compositor,
};

[[nodiscard]] const char* window_backdrop_support_name(WindowBackdropSupport support) noexcept;

// Whether a compositing manager is running. Windows and macOS always
// composite; X11 owns a _NET_WM_CM_Sn selection while one runs.
enum class CompositorPresence : std::uint8_t {
    unknown,
    present,
    absent,
};

[[nodiscard]] const char* compositor_presence_name(CompositorPresence presence) noexcept;

// The order of compositor state reports within one host run. Its own type,
// never mixed with a window's backplane damage generation. The first report
// a host makes has generation 1; 0 is the state before any report.
struct CompositorGeneration final {
    std::uint64_t value{};

    friend bool operator==(CompositorGeneration left, CompositorGeneration right) noexcept {
        return left.value == right.value;
    }
    friend bool operator!=(CompositorGeneration left, CompositorGeneration right) noexcept {
        return left.value != right.value;
    }
    friend bool operator<(CompositorGeneration left, CompositorGeneration right) noexcept {
        return left.value < right.value;
    }
};

// The host's live compositor and preference state (PLAN.md §2.1). Hosts
// report it after attach and whenever it changes, as HostCompositorEvent;
// the window resolves its backdrop grant from it. `support` is indexed by
// WindowBackdrop; `opaque` is always supported, and a report saying
// otherwise is refused.
struct CompositorState final {
    CompositorGeneration generation{};
    CompositorPresence compositing{CompositorPresence::unknown};
    std::array<WindowBackdropSupport, window_backdrop_count> support{
        WindowBackdropSupport::supported, WindowBackdropSupport::host_unsupported,
        WindowBackdropSupport::host_unsupported, WindowBackdropSupport::host_unsupported};
    // The user's transparency-effects setting (Windows); true where the
    // host has no such setting.
    bool transparency_enabled{true};
    // The host's reduced-transparency and high-contrast settings, as the
    // host reports them. A window's own PresentationSettings say what is in
    // force; the host's changes reach them (capability
    // presentation_preferences).
    bool reduced_transparency{};
    bool high_contrast{};
    // The host's colour-scheme preference (T4 theme variants): dark where
    // the user chose dark applications; false where the host has no such
    // setting. A window follows it only while its application's automatic
    // colour scheme is on (ApplicationResources::set_automatic_colour_scheme).
    bool dark_scheme{};
    // The host's accent and text-selection colours, empty where it reports
    // none: hints a theme's env() may read (SystemColourHints).
    SystemColourHints system_colours{};

    [[nodiscard]] WindowBackdropSupport support_of(WindowBackdrop backdrop) const noexcept {
        const std::size_t index = static_cast<std::size_t>(backdrop);
        return support[index];
    }
    [[nodiscard]] std::string to_json() const;

    friend bool operator==(const CompositorState& left, const CompositorState& right) noexcept {
        return left.generation == right.generation && left.compositing == right.compositing &&
               left.support == right.support && left.transparency_enabled == right.transparency_enabled &&
               left.reduced_transparency == right.reduced_transparency &&
               left.high_contrast == right.high_contrast && left.dark_scheme == right.dark_scheme &&
               left.system_colours == right.system_colours;
    }
};

// Whether a host report can be used: `opaque` supported, and each support
// value and the presence within their enumerations.
[[nodiscard]] bool compositor_state_valid(const CompositorState& state) noexcept;

// A window's backdrop request (owner decisions T5-Q3 and T5-Q5): the
// preferred backdrop, then up to two fallbacks, tried in order; `opaque`
// always follows them. Built with the factories; a request is valid when
// it has one to three entries, none repeated, and `opaque` only as the last.
struct WindowBackdropRequest final {
    static constexpr std::size_t maximum_entries = 3U;

    // Value-initialized: every entry `opaque`, the enumeration's first.
    std::array<WindowBackdrop, maximum_entries> entries{};
    std::size_t length{1U};

    [[nodiscard]] static WindowBackdropRequest preferring(WindowBackdrop preferred) noexcept;
    [[nodiscard]] static WindowBackdropRequest with_fallback(WindowBackdrop preferred,
                                                             WindowBackdrop fallback) noexcept;
    [[nodiscard]] static WindowBackdropRequest with_fallbacks(WindowBackdrop preferred, WindowBackdrop first,
                                                              WindowBackdrop second) noexcept;
    [[nodiscard]] WindowBackdrop preferred() const noexcept { return entries[0]; }

    friend bool operator==(const WindowBackdropRequest& left, const WindowBackdropRequest& right) noexcept {
        if (left.length != right.length) return false;
        for (std::size_t index = 0; index < left.length && index < maximum_entries; ++index) {
            if (left.entries[index] != right.entries[index]) return false;
        }
        return true;
    }
};

[[nodiscard]] bool window_backdrop_request_valid(const WindowBackdropRequest& request) noexcept;

// Why the preferred backdrop was or was not granted.
enum class WindowBackdropReason : std::uint8_t {
    granted,
    // The window was not declared non-opaque at creation (T5-Q4).
    not_declared,
    host_unsupported,
    os_version,
    no_compositor,
    high_contrast,
    reduced_transparency,
    // The user turned transparency effects off.
    transparency_disabled,
    // The adapter failed to apply it; the window is opaque.
    backend_failure,
};

[[nodiscard]] const char* window_backdrop_reason_name(WindowBackdropReason reason) noexcept;

// What a window was given: the first entry of its request the host can
// honour now, or `opaque`. `reason` explains the preferred entry; it is
// `granted` only when the preferred entry was granted.
struct WindowBackdropGrant final {
    WindowBackdrop requested{WindowBackdrop::opaque};
    WindowBackdrop granted{WindowBackdrop::opaque};
    WindowBackdropReason reason{WindowBackdropReason::granted};
    // The compositor state it was decided at.
    CompositorGeneration generation{};

    friend bool operator==(const WindowBackdropGrant& left, const WindowBackdropGrant& right) noexcept {
        return left.requested == right.requested && left.granted == right.granted &&
               left.reason == right.reason && left.generation == right.generation;
    }
    friend bool operator!=(const WindowBackdropGrant& left, const WindowBackdropGrant& right) noexcept {
        return !(left == right);
    }
};

// The window-side conditions a grant depends on besides the host's state.
// `high_contrast` and `reduced_transparency` are the ones in force (the
// window's PresentationSettings), which the host's reports feed and the
// application may also set.
struct WindowBackdropConditions final {
    bool declared{};
    bool high_contrast{};
    bool reduced_transparency{};
};

// Why `backdrop` cannot be given now, or `granted`. The one decision point
// (PLAN.md §2.3): adapters report state and apply grants, never decide.
// `opaque` is always granted. For any other backdrop the refusals are
// checked in this order: not declared; high contrast; the host's support
// for it; no compositor running; reduced transparency; transparency
// effects off.
[[nodiscard]] WindowBackdropReason window_backdrop_refusal(WindowBackdrop backdrop, const CompositorState& state,
                                                           const WindowBackdropConditions& conditions) noexcept;

// The grant for a valid `request`: its first entry that is granted, else
// `opaque`, with the preferred entry's reason and the state's generation.
[[nodiscard]] WindowBackdropGrant resolve_window_backdrop(const WindowBackdropRequest& request,
                                                          const CompositorState& state,
                                                          const WindowBackdropConditions& conditions) noexcept;

} // namespace gui_forms
