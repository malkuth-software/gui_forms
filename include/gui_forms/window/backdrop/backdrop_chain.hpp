#pragma once

#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// A window's backdrop (owner, 2026-11-05, revised the same day): an ordered
// chain of operations, in the manner of CSS backdrop-filter, for what lies
// behind the window's content, under everything the window draws. Only a
// window has one (a popup and a floating panel in its own window count); a
// container never does. GUI.Forms never reads or blurs what lies outside a
// window's own surface: a blur is the system material granted to the
// native window (Mica, Acrylic, vibrancy, a compositor's blur), and the
// CPU paints only the chain's tint with its alpha. No host material takes
// a saturation, so saturate is dropped and reported
// (BackdropChainResolution). Content is opaque by default and occludes the
// tint. Under reduced transparency, or with no compositor, the chain
// collapses to its tint as one opaque colour; under high contrast there is
// no backdrop.
//
// Renderer-neutral: the theme engine lowers the theme's `backdrop:`
// property into a chain and sets it on the window
// (SurfaceRoot::set_backdrop_chain).
namespace gui_forms {

enum class BackdropOperationKind : std::uint8_t {
    // Gaussian blur; `amount` is the radius in logical units.
    blur,
    // Saturation; `amount` is the factor, 1 unchanged, 0 grey.
    saturate,
    // A colour composited source-over; `colour` in straight alpha.
    tint,
};

[[nodiscard]] const char* backdrop_operation_name(BackdropOperationKind kind) noexcept;

// The largest blur radius, in logical units, and the largest saturation
// factor, that a valid chain may carry.
inline constexpr double backdrop_maximum_blur = 32.0;
inline constexpr double backdrop_maximum_saturation = 4.0;

struct BackdropOperation final {
    BackdropOperationKind kind{BackdropOperationKind::tint};
    // blur: radius, 0 to backdrop_maximum_blur; saturate: factor, 0 to
    // backdrop_maximum_saturation; unused for tint (0).
    double amount{};
    // tint only (transparent black otherwise).
    Color colour{};

    [[nodiscard]] static BackdropOperation blur(double radius) noexcept;
    [[nodiscard]] static BackdropOperation saturate(double factor) noexcept;
    [[nodiscard]] static BackdropOperation tint(Color colour) noexcept;

    friend bool operator==(const BackdropOperation& left, const BackdropOperation& right) noexcept {
        return left.kind == right.kind && left.amount == right.amount && left.colour == right.colour;
    }
    friend bool operator!=(const BackdropOperation& left, const BackdropOperation& right) noexcept {
        return !(left == right);
    }
};

// The chain: `length` live operations, applied in order, in a fixed array.
struct BackdropChain final {
    static constexpr std::size_t maximum_operations = 8U;

    std::array<BackdropOperation, maximum_operations> operations{};
    std::size_t length{};

    // Appends `operation`; false, changing nothing, when the chain is full.
    bool append(const BackdropOperation& operation) noexcept;

    friend bool operator==(const BackdropChain& left, const BackdropChain& right) noexcept {
        if (left.length != right.length) return false;
        for (std::size_t index = 0U; index < left.length && index < maximum_operations; ++index) {
            if (left.operations[index] != right.operations[index]) return false;
        }
        return true;
    }
    friend bool operator!=(const BackdropChain& left, const BackdropChain& right) noexcept {
        return !(left == right);
    }
};

// Valid: at most maximum_operations live, every amount finite and within its
// range, a tint's amount 0, at most one blur.
[[nodiscard]] bool backdrop_chain_valid(const BackdropChain& chain) noexcept;

// The chain's fallback: its tints composited in order over clear, as one
// solid colour in straight alpha; transparent black when it has none.
[[nodiscard]] Color backdrop_chain_solid(const BackdropChain& chain) noexcept;

// The largest blur radius in the chain, in logical units; 0 without one.
[[nodiscard]] double backdrop_chain_blur(const BackdropChain& chain) noexcept;

// How a window carries its chain under its grant
// (SurfaceRoot::backdrop_chain_resolution).
struct BackdropChainResolution final {
    // The chain's blur is the system material granted (window_material or
    // transient_material).
    bool blur_by_material{};
    // The chain has a blur but no material was granted: it is dropped.
    bool blur_dropped{};
    // The chain has a saturation, which no host material applies: dropped.
    bool saturate_dropped{};
    // The tint GUI.Forms paints (backdrop_chain_solid), straight alpha.
    Color tint{};
    // The tint is painted opaque: the grant is opaque (reduced
    // transparency, no compositor, or any other refusal).
    bool solid{};
    // No backdrop at all: high contrast.
    bool withdrawn{};
};

// The chain's resolution under `grant`, with the window's high contrast.
[[nodiscard]] BackdropChainResolution resolve_backdrop_chain(const BackdropChain& chain,
                                                             const WindowBackdropGrant& grant,
                                                             bool high_contrast) noexcept;

// What a window's in-surface popup tints have cost
// (SurfaceRoot::backdrop_statistics), counted from the window's creation.
struct BackdropStatistics final {
    // Exposed rectangles tinted, and their device pixels.
    std::uint64_t draws{};
    std::uint64_t drawn_pixels{};
    // Painted rectangles in which the popup's opaque content covered all of
    // it, so nothing was drawn.
    std::uint64_t occluded{};
    // Of the draws, those of the opaque fallback (reduced transparency or
    // no compositor).
    std::uint64_t solid_fallbacks{};
};

} // namespace gui_forms
