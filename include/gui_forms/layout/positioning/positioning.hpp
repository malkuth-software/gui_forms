#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <limits>

namespace gui_forms {

// Positioning (stage L, phase L5; PLAN section 2.5) over named records,
// with no control dependency: the offset of `position: relative`, and the
// size and place of an absolutely positioned box in its containing block
// (CSS 2.1 sections 10.3.7 and 10.6.4). FlexPanel, GridPanel and
// BlockPanel apply it to their children; which box is the containing
// block, and the conversion into the parent's coordinates, are theirs.

// The inset `auto`, every side's default.
inline constexpr AutoLengthPercentage automatic_inset{true, {}};

// An item's `position` and its insets, `auto` by default. A positioned item
// (relative or absolute) is a containing block for its absolutely
// positioned descendants.
struct GUI_FORMS_API ItemPositioning final {
    PositionKind position{PositionKind::static_position};
    BoxSides<AutoLengthPercentage> inset{automatic_inset, automatic_inset, automatic_inset, automatic_inset};

    friend bool operator==(const ItemPositioning& left, const ItemPositioning& right) noexcept {
        return left.position == right.position && left.inset == right.inset;
    }
};

// `position: relative`'s offset (CSS 2.1 9.4.3), with percentages of the
// containing block's width and height. Of left and right, the start side
// wins when both are set (left for left to right, right for right to left);
// of top and bottom, top. A static or absolute item has none.
[[nodiscard]] GUI_FORMS_API Point relative_position_offset(const ItemPositioning& positioning, Size containing_block,
                                             LayoutDirection direction) noexcept;

// One axis of an absolutely positioned box in its containing block's
// padding box: left and right with the width, or top and bottom with the
// height. Lengths are resolved by the caller; the insets keep `auto`.
struct GUI_FORMS_API AbsoluteAxisInput final {
    double containing_extent{};
    // left or top, and right or bottom.
    AutoLengthPercentage start_inset{true, {}};
    AutoLengthPercentage end_inset{true, {}};
    // The item's own size when it is definite.
    bool definite_size{};
    double size{};
    // The content's size range: for a width the min-content and
    // max-content widths (shrink-to-fit takes a width between them); for a
    // height the content's height at the used width, twice.
    double content_minimum{};
    double content_maximum{};
    // The used minimum and maximum (a minimum wins).
    double minimum{};
    double maximum{std::numeric_limits<double>::infinity()};
    double margin_start{};
    double margin_end{};
    bool auto_margin_start{};
    bool auto_margin_end{};
    // Where the box's start margin edge would be with both insets `auto`
    // (its static position), or with static_from_end its end margin edge
    // (the horizontal axis of a right-to-left container).
    double static_position{};
    bool static_from_end{};
    // The horizontal axis of a right-to-left containing block: an
    // over-constrained box keeps its end inset, and auto margins too large
    // to share leave the end one.
    bool end_wins{};
    // The vertical axis: auto margins share what is left even when it is
    // negative (10.6.4).
    bool vertical{};
};

struct GUI_FORMS_API AbsoluteAxisResult final {
    // The border box's start edge and size in the containing block.
    double position{};
    double size{};
};

[[nodiscard]] GUI_FORMS_API AbsoluteAxisResult resolve_absolute_axis(const AbsoluteAxisInput& input) noexcept;

// Whether the axis's size comes from the content: neither the item's own
// size nor both insets fix it. A caller measures the content only then.
[[nodiscard]] GUI_FORMS_API bool absolute_axis_reads_content(const AbsoluteAxisInput& input) noexcept;

} // namespace gui_forms
