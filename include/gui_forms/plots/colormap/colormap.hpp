#pragma once

// Colormap: values to colours (IMGUI_IMPLOT_STUDY B.3, P8; owner decision
// 10: the CC0 perceptual maps of the viridis family ship under their names,
// beside our own default). A colormap is a value:
// - qualitative: a list of colours, value k taking colour k (wrapping);
// - continuous: 2 to 16 stops at increasing offsets from 0 to 1,
//   interpolated in a stated space.
//
// Interpolation space: linear sRGB by default (colours mix as light
// does); `srgb_encoded` mixes the encoded bytes, which the built-in viridis
// family uses because its stops were sampled from maps designed to be read
// that way (viridis, magma, inferno and plasma by Stéfan van der Walt and
// Nathaniel Smith, released CC0; ten stops each, the published colours at
// 0, 1/9, ..., 1).
//
// The sRGB transfer uses a 256-entry decode table built once from
// engine_power, and encodes by nearest search in it, so a colour is the
// same bytes on every host.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/scale/scale.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms::plots {

inline constexpr std::size_t maximum_colormap_stops = 16U;

enum class ColormapKind : std::uint8_t { qualitative, continuous };
enum class ColormapSpace : std::uint8_t { linear_srgb, srgb_encoded };

struct GUI_FORMS_API ColormapStop final {
    double offset{};
    Color color{};

    friend bool operator==(const ColormapStop& left, const ColormapStop& right) noexcept {
        return left.offset == right.offset && left.color == right.color;
    }
};

struct GUI_FORMS_API Colormap final {
    ColormapKind kind{ColormapKind::continuous};
    ColormapSpace space{ColormapSpace::linear_srgb};
    // `stop_count` of `stops` are live. A qualitative map ignores offsets.
    std::array<ColormapStop, maximum_colormap_stops> stops{};
    std::size_t stop_count{};
    // The colour of NaN values.
    Color gap_color{Color::rgba(0, 0, 0, 0)};

    friend bool operator==(const Colormap& left, const Colormap& right) noexcept {
        if (left.kind != right.kind || left.space != right.space || left.stop_count != right.stop_count ||
            !(left.gap_color == right.gap_color)) {
            return false;
        }
        for (std::size_t index = 0U; index < left.stop_count; ++index) {
            if (!(left.stops[index] == right.stops[index])) return false;
        }
        return true;
    }
};

// Whether `map` can sample: 1 to 16 stops (2 or more for a continuous map),
// a continuous map's offsets finite, from 0 to 1 and non-decreasing,
// starting at 0 and ending at 1.
[[nodiscard]] GUI_FORMS_API bool valid_colormap(const Colormap& map) noexcept;

// The colour at `position` in [0, 1] (clamped) of a continuous map, or of
// stop floor(position) (wrapping) of a qualitative one; NaN gives the gap
// colour. An invalid map gives the gap colour.
[[nodiscard]] GUI_FORMS_API Color sample_colormap(const Colormap& map, double position) noexcept;

// Colours for `values` through `scale` (its domain to positions 0..1: the
// scale's device range is ignored and taken as [0, 1]) into `out`
// (values.size() entries; nothing written and false when it is shorter or
// the map or scale is invalid). Values outside the domain clamp.
GUI_FORMS_API bool sample_colormap_span(const Colormap& map, const Scale& scale, std::span<const double> values,
                          std::span<Color> out) noexcept;

enum class BuiltInColormap : std::uint8_t { viridis, magma, inferno, plasma, gui_forms_default };

// The built-in maps. gui_forms_default is our own: a cool-to-warm
// continuous map (deep blue, teal, sand, brick) interpolated in linear
// sRGB.
[[nodiscard]] GUI_FORMS_API const Colormap& built_in_colormap(BuiltInColormap which) noexcept;
// The built-in named `name` ("viridis", "magma", "inferno", "plasma",
// "default"), or null.
[[nodiscard]] GUI_FORMS_API const Colormap* find_built_in_colormap(std::string_view name) noexcept;

// sRGB transfer: an encoded byte's linear value in [0, 1], and the nearest
// encoded byte of a linear value (clamped).
[[nodiscard]] GUI_FORMS_API double srgb_to_linear(std::uint8_t encoded) noexcept;
[[nodiscard]] GUI_FORMS_API std::uint8_t linear_to_srgb(double linear) noexcept;

} // namespace gui_forms::plots
