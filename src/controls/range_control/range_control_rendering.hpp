#pragma once

#include "gui_forms/controls/range_control/range_control.hpp"

namespace gui_forms {

struct ProgressBarAnimationAppearance;

namespace range_control_detail {

inline constexpr double track_inset = 10.0;

void require_finite(double value, const char* message);
void paint_sunken(Painter& painter, Rect bounds,
                  const BasicControlStyle& style, Color fill);
void paint_thumb(Painter& painter, Rect bounds,
                 const BasicControlStyle& style, bool focused);
void paint_progress_stripes(Painter& painter, Rect fill, Color color,
                            double stripe_width, double phase);
// The luminance pulse's band at `phase`: `extent` of the fill's width (at
// least 8 pixels), travelling from just before the fill to just past it.
[[nodiscard]] Rect progress_luminance_band(Rect fill, double extent, double phase) noexcept;
void paint_progress_luminance(Painter& painter, Rect fill, Color color,
                              double extent, double phase);
void paint_progress_laser(Painter& painter, Rect fill, Rect interior,
                          Orientation orientation,
                          const ProgressBarAnimationAppearance& appearance,
                          double phase, bool reduced);

} // namespace range_control_detail
} // namespace gui_forms
