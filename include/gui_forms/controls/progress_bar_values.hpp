#pragma once

#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstdint>

namespace gui_forms {

// ProgressBar's styles and animation appearance, apart from the class so
// its capabilities can name them.

enum class ProgressBarVisualStyle : std::uint8_t {
    blocks,
    continuous,
    marquee,
    pulse,
    luminance_pulse = pulse,
    marching_stripes,
    laser_etch,
};

enum class ProgressBarOverlayStyle : std::uint8_t {
    none,
    moving_stripes,
    marching_stripes = moving_stripes,
};

struct ProgressBarAnimationAppearance final {
    Color luminance_color{Color::rgba(255, 255, 255, 92)};
    Color stripe_color{Color::rgba(224, 241, 252, 116)};
    Color laser_phase_color{Color::rgba(130, 222, 247, 108)};
    Color laser_edge_color{Color::rgba(196, 246, 255, 226)};
    Color laser_spark_color{Color::rgba(238, 253, 255, 236)};
    double pulse_extent{0.32};
    double laser_edge_extent{13.0};
    double laser_phase_pitch{10.0};

    friend constexpr bool operator==(
        const ProgressBarAnimationAppearance& left,
        const ProgressBarAnimationAppearance& right) noexcept {
        return left.luminance_color == right.luminance_color &&
               left.stripe_color == right.stripe_color &&
               left.laser_phase_color == right.laser_phase_color &&
               left.laser_edge_color == right.laser_edge_color &&
               left.laser_spark_color == right.laser_spark_color &&
               left.pulse_extent == right.pulse_extent &&
               left.laser_edge_extent == right.laser_edge_extent &&
               left.laser_phase_pitch == right.laser_phase_pitch;
    }
};

} // namespace gui_forms
