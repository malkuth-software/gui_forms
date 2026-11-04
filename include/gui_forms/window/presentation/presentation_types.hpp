#pragma once

#include "gui_forms/types.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"
#include "gui_forms/window/presentation/input_settings.hpp"

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms {

class LiveSurface;

// Presentation preferences are logical UI policy and remain independent from
// the native/device pixel scale.
// A window showing GUI.Forms' own theme because its theme has no variant
// for the window's colour scheme (Window::theme_fallback, T4 theme
// variants): the application's theme, the scheme it lacks, and the theme
// shown instead (`gui-forms` in that scheme). Colours the application set
// on its controls are suspended while it holds (Control::
// application_colours_suspended).
struct ThemeFallback final {
    std::string theme_id{};
    ColourScheme missing{ColourScheme::dark};
    std::string fallback_id{};

    friend bool operator==(const ThemeFallback& left, const ThemeFallback& right) noexcept {
        return left.theme_id == right.theme_id && left.missing == right.missing &&
               left.fallback_id == right.fallback_id;
    }
};

struct PresentationSettings final {
    double text_scale{1.0};
    bool high_contrast{};
    bool reduced_motion{};
    bool sound_enabled{true};
    // The application's motion switch (design language §9.4 item 3, T2 E7):
    // off, the theme engine's ambient effects stop and their parts rest at
    // their static poses. Transitions are not affected; reduced motion,
    // the host's preference, ends them.
    bool ambient_motion{true};
    // Reduced transparency (stage T5, owner decision T5-Q7): the host's
    // setting where the host reports it (capability
    // presentation_preferences), or the application's. With it, or with
    // high_contrast, a window's backdrop is opaque (WindowBackdropReason).
    bool reduced_transparency{};
    // The system's double-click interval, a perceptual duration in
    // milliseconds: the longest gap between two clicks that the platform
    // counts as one double-click. A control that tells a slow second click
    // from a double-click (a list's label edit) compares against it. The
    // host reports it at attach and again when the system setting changes
    // (HostInputSettingsEvent); each host's source is listed under "Input
    // settings" in docs/user/hosts-and-capabilities.md.
    // `double_click_origin` says whether it is that host value or the stated
    // default. Between 1 and maximum_double_click_milliseconds.
    std::uint32_t double_click_milliseconds{default_double_click_milliseconds};
    SettingOrigin double_click_origin{SettingOrigin::default_value};
    // The preferred colour scheme (EC3). A change re-resolves every part:
    // a theme's rules may hold for one scheme only.
    ColourScheme colour_scheme{ColourScheme::light};
    friend constexpr bool operator==(const PresentationSettings& left,
                                     const PresentationSettings& right) noexcept {
        return left.text_scale == right.text_scale &&
               left.high_contrast == right.high_contrast &&
               left.reduced_motion == right.reduced_motion &&
               left.sound_enabled == right.sound_enabled &&
               left.ambient_motion == right.ambient_motion &&
               left.reduced_transparency == right.reduced_transparency &&
               left.double_click_milliseconds == right.double_click_milliseconds &&
               left.double_click_origin == right.double_click_origin &&
               left.colour_scheme == right.colour_scheme;
    }
};

// The screen area one presentation copied (stage DP): device pixels, the
// rectangles they were copied in, and the time spent only copying them
// (pixel conversion and the native copy, not painting).
struct PresentedArea final {
    std::uint64_t device_pixels{};
    std::uint64_t rectangles{};
    std::uint64_t copy_nanoseconds{};
};

// Immutable host placement for the newest generation of one live surface.
struct LiveSurfacePresentation final {
    RuntimeId control{};
    std::shared_ptr<LiveSurface> surface;
    Rect destination{};
    Rect clip{};
    // True when this window last presented the surface at the same
    // destination, clip and overlays. A host whose raster still holds that
    // presentation may then copy only the surface's presentation region
    // (LiveSurface::acquire_for_presentation). False means copy all of clip.
    bool damage_limited{};
};

enum class PaintLeaseState : std::uint8_t {
    clean,
    dirty_queued,
    dirty = dirty_queued,
    rendering,
    ready,
    occluded_dirty,
    retired,
    rendering_dirty,
};

struct PaintLeaseSnapshot final {
    PaintLeaseState state{PaintLeaseState::dirty_queued};
    std::uint64_t content_revision{1U};
    std::uint64_t rendered_revision{};
    std::uint64_t presented_revision{};
    std::uint64_t surface_epoch{1U};
    std::uint64_t leases_started{};
    std::uint64_t leases_completed{};
    std::uint64_t leases_abandoned{};
    std::uint64_t reentrant_requests_deferred{};
    std::uint64_t render_wakes_queued{};
    std::uint64_t render_wakes_coalesced{};
    std::uint64_t presentation_receipts_accepted{};
    std::uint64_t presentation_receipts_rejected{};
    bool render_wake_queued{};
    bool dirty_after_render{};
};

// The presentation availability record (stage DP; notes:planning/
// PAINT_PIPELINE_AVAILABILITY.md, M11h-P4 / M12-P4). The baseline contract
// (coherent latest-state presentation: mutation may outpace rendering and
// rendering may outpace presentation, with no unbounded queue and no
// incomplete frame visible) is reported as a version, never as a capability:
// a host that cannot meet it fails attachment. Only optional extensions are
// reported as available or not, and only truthfully. Version 2 (2026-10-13)
// adds damage-preserving partial present to the contract: the host presents
// the bounded rectangle set it paints, not their union.
inline constexpr std::uint32_t paint_contract_version = 2U;

struct PresentationExtensions final {
    // The host copies a bounded rectangle set to the screen, not its union.
    // Required since contract version 2, and reported for readers of
    // version 1.
    bool partial_present{};
    // Completed private pixels reach the screen without a CPU copy.
    bool zero_copy_import{};
    // The host can tell submission apart from compositor acknowledgement.
    bool presentation_fence{};
    // An opaque platform surface lease exists; no native handle enters the
    // portable control API.
    bool native_surface_lease{};

    friend constexpr bool operator==(const PresentationExtensions& left,
                                     const PresentationExtensions& right) noexcept {
        return left.partial_present == right.partial_present &&
               left.zero_copy_import == right.zero_copy_import &&
               left.presentation_fence == right.presentation_fence &&
               left.native_surface_lease == right.native_surface_lease;
    }
};

// What a host declares when it attaches a window.
struct PresentationDeclaration final {
    std::uint32_t contract_version{};
    PresentationExtensions extensions;
};

// The record a window publishes: the host's declaration and the live
// paint-lease snapshot. A contract version of zero means no host has
// declared one (a window that is not attached, or a test painter).
struct PresentationAvailability final {
    std::uint32_t contract_version{};
    PresentationExtensions extensions;
    PaintLeaseSnapshot lease;

    [[nodiscard]] std::string to_json() const;
};

// Proof that one coherent retained transaction finished replaying into a
// host-owned backing surface at a still-current surface epoch.
struct PaintReceipt final {
    std::uint64_t rendered_revision{};
    std::uint64_t surface_epoch{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return rendered_revision != 0U && surface_epoch != 0U;
    }
    friend constexpr bool operator==(const PaintReceipt& left,
                                     const PaintReceipt& right) noexcept {
        return left.rendered_revision == right.rendered_revision &&
               left.surface_epoch == right.surface_epoch;
    }
    friend constexpr std::strong_ordering operator<=>(
        const PaintReceipt& left, const PaintReceipt& right) noexcept {
        const std::strong_ordering revision_order =
            left.rendered_revision <=> right.rendered_revision;
        return revision_order != 0
            ? revision_order : left.surface_epoch <=> right.surface_epoch;
    }
};

} // namespace gui_forms
