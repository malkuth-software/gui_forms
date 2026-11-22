#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control/runtime_id/runtime_id.hpp"
#include "gui_forms/controls/button_content_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/scale_frame/scale_frame.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms {

// The visible-area guarantee (stage VA, owner decisions of 2026-11-22; notes
// planning/stages/VA-visible-area/STUDY.md). No content is outside its
// viewport unless the viewport's owner answers that overflow, and every
// overflow is recorded by the window.
//
// A viewport owner is any control that places content inside a rectangle it
// answers for: a container (its children against child_viewport_rectangle),
// or a control showing content of a fixed design size (Control::design_size)
// against its client rectangle. Layout reflows first; the answer is what
// happens when reflow is not enough.

// What a viewport owner does when its content, after layout, does not fit.
// The numbers are the C ABI's gf_overflow_answer values.
enum class OverflowAnswer : std::uint8_t {
    // The content keeps its size and the owner's scroll range reaches all
    // of it. Carried out only by an owner that scrolls its content
    // (Control::content_scrolls); an owner that scrolls answers by
    // scrolling whatever its viewport_fit says.
    scroll = 0,
    // One uniform scale, the smaller of the two axis ratios, kept within
    // [minimum_scale, maximum_scale], and the content aligned in the
    // leftover space.
    contain = 1,
    // One uniform scale, the larger of the two axis ratios, kept within the
    // same bounds: the viewport is filled and the excess is cropped. An
    // explicit, recorded crop.
    cover = 2,
    // The content keeps its size and is cut at the viewport. Allowed only by
    // explicit choice and always recorded.
    clip = 3,
};

[[nodiscard]] constexpr bool is_valid_overflow_answer(OverflowAnswer answer) noexcept {
    const bool valid = answer == OverflowAnswer::scroll || answer == OverflowAnswer::contain ||
                       answer == OverflowAnswer::cover || answer == OverflowAnswer::clip;
    return valid;
}

// The answer's snake_case name ("scroll", "contain", "cover", "clip"), or
// "unknown" for a value outside the enumeration.
[[nodiscard]] GUI_FORMS_API const char* overflow_answer_name(OverflowAnswer answer) noexcept;

// The largest scale a ViewportFit may allow: content shown up to 1024 times
// its design size.
inline constexpr double maximum_viewport_scale = 1024.0;

// A viewport owner's policy (Control::viewport_fit). Independent options
// with valid defaults; valid_viewport_fit states the rules.
struct GUI_FORMS_API ViewportFit final {
    OverflowAnswer answer{OverflowAnswer::contain};
    // Bounds on the effective scale of contain and cover: the product of
    // every applied scale from this owner up to its root. A contain whose
    // ratio falls below minimum_scale takes below_minimum instead, at
    // minimum_scale.
    double minimum_scale{0.0};
    double maximum_scale{1.0};
    OverflowAnswer below_minimum{OverflowAnswer::scroll};
    // Where contained or covered content sits in the leftover space.
    ContentAlignment alignment{ContentAlignment::middle_center};

    friend bool operator==(const ViewportFit& left, const ViewportFit& right) noexcept {
        const bool equal = left.answer == right.answer && left.minimum_scale == right.minimum_scale &&
                           left.maximum_scale == right.maximum_scale &&
                           left.below_minimum == right.below_minimum && left.alignment == right.alignment;
        return equal;
    }
    friend bool operator!=(const ViewportFit& left, const ViewportFit& right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

// The defaults per kind of owner (section 8; owner decision 4). Control's
// own default is clip, recorded: a control that places parts of its own
// answers its overflow itself (strips, the ribbon, navigation).
// Windows Forms panels (Panel and its kinds, UserControl, SplitterPanel)
// contain: content outside them is scaled to fit, a deliberate difference
// from Windows Forms, which clips; never enlarged.
inline constexpr ViewportFit panel_viewport_fit{OverflowAnswer::contain, 0.0, 1.0};
// Media (PictureBox, RasterCanvas, a SubSurfaceHost with a design size)
// contains with no floor and up to maximum_viewport_scale.
inline constexpr ViewportFit media_viewport_fit{OverflowAnswer::contain, 0.0, maximum_viewport_scale};
// CSS panels (FlexPanel, GridPanel, BlockPanel) reflow, then scroll: an
// overflow they cannot scroll (auto_scroll off) is recorded unanswered.
inline constexpr ViewportFit reflowing_viewport_fit{OverflowAnswer::scroll, 0.0, 1.0};

// Whether `fit` is one a viewport owner accepts: both answers in the
// enumeration, below_minimum scroll or clip, minimum_scale finite and not
// negative, maximum_scale finite, positive, at least minimum_scale and at
// most maximum_viewport_scale, and the alignment in its enumeration.
[[nodiscard]] GUI_FORMS_API bool valid_viewport_fit(const ViewportFit& fit) noexcept;

// Where content of extent `content` goes in `viewport` under `fit`: the
// pure computation every owner that carries out contain or cover uses.
struct GUI_FORMS_API ViewportPlacement final {
    // The answer carried out: fit.answer, or fit.below_minimum for a
    // contain whose ratio fell below minimum_scale.
    OverflowAnswer applied{OverflowAnswer::contain};
    // Content units to viewport units.
    double scale{1.0};
    // The scaled content's rectangle in the viewport's coordinates. It lies
    // inside the viewport when `fits`; a cover, a clip or a scroll of larger
    // content reaches past it.
    Rect destination{};
    bool fits{true};

    friend bool operator==(const ViewportPlacement& left, const ViewportPlacement& right) noexcept {
        const bool equal = left.applied == right.applied && left.scale == right.scale &&
                           left.destination == right.destination && left.fits == right.fits;
        return equal;
    }
    friend bool operator!=(const ViewportPlacement& left, const ViewportPlacement& right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

// Places `content` in `viewport`:
//   - contain: scale = min(w_v / w_c, h_v / h_c), kept at most maximum_scale;
//     below minimum_scale the placement takes below_minimum at minimum_scale;
//   - cover: scale = max(w_v / w_c, h_v / h_c), kept within both bounds;
//   - scroll and clip: scale 1;
// then aligns the scaled extent in the leftover space by fit.alignment
// (scroll and clip by the viewport's top left). Content with an empty or
// non-finite extent, or an empty viewport, is placed at scale 1 at the
// viewport's origin and fits. `fit` must be valid (valid_viewport_fit); an
// invalid one is placed as the default ViewportFit.
[[nodiscard]] GUI_FORMS_API ViewportPlacement place_viewport_content(Size content, Rect viewport,
                                                                     const ViewportFit& fit) noexcept;

// Why content overflowed its viewport. The numbers are the C ABI's
// gf_overflow_cause values.
enum class OverflowCause : std::uint8_t {
    // A layout container's children did not fit after reflow: their
    // minimum content exceeds the viewport.
    minimum_content_exceeds_viewport = 0,
    // The owner's design_size exceeds its viewport.
    design_size_exceeds_viewport = 1,
    // A child placed by position, dock or anchor lies outside the viewport.
    placed_child_outside_viewport = 2,
    // A window's minimum size exceeds the work area of its monitor.
    minimum_size_exceeds_work_area = 3,
};

[[nodiscard]] constexpr bool is_valid_overflow_cause(OverflowCause cause) noexcept {
    const bool valid = cause == OverflowCause::minimum_content_exceeds_viewport ||
                       cause == OverflowCause::design_size_exceeds_viewport ||
                       cause == OverflowCause::placed_child_outside_viewport ||
                       cause == OverflowCause::minimum_size_exceeds_work_area;
    return valid;
}

[[nodiscard]] GUI_FORMS_API const char* overflow_cause_name(OverflowCause cause) noexcept;

// One overflow a window knows of now (SurfaceRoot::overflow_records). A
// record exists while its overflow lasts; it is replaced when its answer or
// cause changes and removed when the content fits again or its owner leaves
// the window.
struct GUI_FORMS_API OverflowRecord final {
    // The viewport owner, a control of the window (SurfaceRoot::
    // overflow_owner finds it).
    RuntimeId owner{};
    OverflowCause cause{OverflowCause::minimum_content_exceeds_viewport};
    // The content's extent and the viewport's, in the owner's logical units.
    Size content_extent{};
    Size viewport_extent{};
    // The answer the owner gives: scroll for an owner that scrolls its
    // content, otherwise its viewport_fit's answer.
    OverflowAnswer answer{OverflowAnswer::contain};
    // The scale carried out (1 where none is).
    double effective_scale{1.0};
    // False only for an unanswered overflow: the owner's answer is not
    // carried out (a scroll by an owner that does not scroll, or a contain
    // or cover where none is applied yet). A healthy window has none.
    bool answered{};

    friend bool operator==(const OverflowRecord& left, const OverflowRecord& right) noexcept {
        const bool equal = left.owner == right.owner && left.cause == right.cause &&
                           left.content_extent == right.content_extent &&
                           left.viewport_extent == right.viewport_extent && left.answer == right.answer &&
                           left.effective_scale == right.effective_scale && left.answered == right.answered;
        return equal;
    }
    friend bool operator!=(const OverflowRecord& left, const OverflowRecord& right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

// The most overflow records a window keeps; later ones are counted
// (SurfaceRoot::dropped_overflow_records).
inline constexpr std::size_t maximum_overflow_records = 1000U;

// The edges of a window's client area that the host or the device hides
// (a notch, rounded corners, a television's overscan, a browser's toolbar
// over the canvas), in logical units. Zero on a host that reports none.
struct GUI_FORMS_API SafeInsets final {
    double left{};
    double top{};
    double right{};
    double bottom{};

    friend bool operator==(const SafeInsets& left, const SafeInsets& right) noexcept {
        const bool equal = left.left == right.left && left.top == right.top && left.right == right.right &&
                           left.bottom == right.bottom;
        return equal;
    }
    friend bool operator!=(const SafeInsets& left, const SafeInsets& right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

// Whether every edge is finite and not negative.
[[nodiscard]] GUI_FORMS_API bool valid_safe_insets(const SafeInsets& insets) noexcept;

// The frame that carries out `placement` for content occupying `content`
// in its own coordinates: the content's top left goes to the destination's
// top left, at the placement's scale.
[[nodiscard]] GUI_FORMS_API ScaleFrame placement_frame(Rect content, const ViewportPlacement& placement) noexcept;
// `fit` with its scale bounds expressed for an owner whose ancestors
// already scale by `outer_scale` (positive): the bounds of a ViewportFit
// are on the effective scale, so a nested contain cannot escape the floor
// by nesting. Both bounds are divided by `outer_scale` and kept at most
// maximum_viewport_scale, the maximum at least the minimum.
[[nodiscard]] GUI_FORMS_API ViewportFit fit_within_outer_scale(const ViewportFit& fit, double outer_scale) noexcept;

} // namespace gui_forms
