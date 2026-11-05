#pragma once

#include "gui_forms/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// A window's backdrop (owner, 2026-11-05): an ordered chain of operations,
// in the manner of CSS backdrop-filter, applied once to what lies behind the
// window's content, under everything the window draws. Only a window has
// one (a popup and a floating panel in its own window count); a container
// never does, and a backdrop never samples another backdrop. Content is
// opaque by default and occludes it: the backdrop is computed only where
// clear content exposes it. Under reduced transparency, or with no
// compositor, the chain collapses to its tint as one solid colour
// (backdrop_chain_solid); under high contrast there is no backdrop.
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

// What a window's backdrops have cost (SurfaceRoot::backdrop_statistics),
// counted from the window's creation.
struct BackdropStatistics final {
    // Computations of the chain over a strip of cache tiles, and the device
    // pixels they wrote.
    std::uint64_t computations{};
    std::uint64_t computed_pixels{};
    // Exposed rectangles drawn from the cache, and their device pixels.
    std::uint64_t draws{};
    std::uint64_t drawn_pixels{};
    // Painted rectangles in which the backdrop was wholly covered by
    // opaque content, so nothing was computed or drawn.
    std::uint64_t occluded{};
    // Draws of the solid fallback (reduced transparency, or no source).
    std::uint64_t solid_fallbacks{};
    // Cache tiles invalidated by damage under them.
    std::uint64_t invalidated_tiles{};
};

} // namespace gui_forms
