#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/layout/types/layout_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gui_forms {

// The typed layout style of one element (stage L, PLAN sections 2.2 to 2.5):
// what the admitted CSS layout declarations of a form document or a fixture
// mean, read by the layout style reader (phase L2) and consumed by the flex,
// grid and block containers (L3 to L5). Every field starts at its CSS initial
// value, so a default LayoutStyle is an element with no layout declarations.
// Names and keywords are CSS's; lengths are logical units. The reader applies
// declarations on top of the style it is given, so a caller that wants an
// element's own defaults (a fixture `div` is `display: block`) sets them
// first.

// A length and a percentage of the box CSS names for the property, summed:
// `24px` is {24, 0}, `50%` is {0, 50}, `calc(100% - 24px)` is {-24, 100}.
struct GUI_FORMS_API LengthPercentage final {
    LogicalLength length{};
    // Percent, not a fraction: 50 means half.
    double percent{};

    friend constexpr bool operator==(const LengthPercentage& left,
                                     const LengthPercentage& right) noexcept {
        return left.length == right.length && left.percent == right.percent;
    }
};

// `width`, `height`, their minimums and maximums, and `flex-basis`.
enum class SizeKind : std::uint8_t {
    // `auto`: for a minimum, the automatic minimum size (CSS Flexbox 4.5).
    automatic,
    // `none`, a maximum only.
    none,
    length_percentage,
    min_content,
    max_content,
    // `fit-content(<length-percentage>)`.
    fit_content,
    // `content`, `flex-basis` only.
    content,
};

struct GUI_FORMS_API SizeValue final {
    SizeKind kind{SizeKind::automatic};
    // Read for length_percentage and fit_content.
    LengthPercentage value{};

    friend constexpr bool operator==(const SizeValue& left, const SizeValue& right) noexcept {
        return left.kind == right.kind && left.value == right.value;
    }
};

// A margin or an inset: `auto` or a length-percentage, which may be negative.
struct GUI_FORMS_API AutoLengthPercentage final {
    bool automatic{};
    LengthPercentage value{};

    friend constexpr bool operator==(const AutoLengthPercentage& left,
                                     const AutoLengthPercentage& right) noexcept {
        return left.automatic == right.automatic && left.value == right.value;
    }
};

// Top, right, bottom and left, CSS's shorthand order.
template <typename Value> struct BoxSides final {
    Value top{};
    Value right{};
    Value bottom{};
    Value left{};

    friend constexpr bool operator==(const BoxSides& left_sides, const BoxSides& right_sides) noexcept {
        return left_sides.top == right_sides.top && left_sides.right == right_sides.right &&
               left_sides.bottom == right_sides.bottom && left_sides.left == right_sides.left;
    }
};

enum class DisplayKind : std::uint8_t { block, inline_level, inline_block, flex, grid, none };

enum class PositionKind : std::uint8_t { static_position, relative, absolute };

// The keywords of every alignment property. Each property admits its own
// subset (the reader refuses the rest): justify-content and align-content
// the content distribution, align-items and justify-items the item
// alignment, align-self and justify-self those plus `auto`.
enum class LayoutAlignment : std::uint8_t {
    automatic,
    normal,
    stretch,
    start,
    end,
    flex_start,
    flex_end,
    center,
    baseline,
    space_between,
    space_around,
    space_evenly,
};

enum class FlexDirection : std::uint8_t { row, row_reverse, column, column_reverse };
enum class FlexWrap : std::uint8_t { nowrap, wrap, wrap_reverse };

// Flex factors and grid fractions are unitless and never added to lengths.
class GUI_FORMS_API FlexFactor final {
public:
    explicit constexpr FlexFactor(double value = 0.0) noexcept : value_(value) {}
    [[nodiscard]] constexpr double value() const noexcept { return value_; }
    friend constexpr bool operator==(const FlexFactor& left, const FlexFactor& right) noexcept {
        return left.value_ == right.value_;
    }

private:
    double value_{};
};

class GUI_FORMS_API FractionFactor final {
public:
    explicit constexpr FractionFactor(double value = 0.0) noexcept : value_(value) {}
    [[nodiscard]] constexpr double value() const noexcept { return value_; }
    friend constexpr bool operator==(const FractionFactor& left, const FractionFactor& right) noexcept {
        return left.value_ == right.value_;
    }

private:
    double value_{};
};

// CSS Box Alignment's six properties, shared by flex and grid containers
// and their items.
struct GUI_FORMS_API BoxAlignment final {
    LayoutAlignment justify_content{LayoutAlignment::normal};
    LayoutAlignment align_content{LayoutAlignment::normal};
    LayoutAlignment justify_items{LayoutAlignment::normal};
    LayoutAlignment align_items{LayoutAlignment::normal};
    LayoutAlignment justify_self{LayoutAlignment::automatic};
    LayoutAlignment align_self{LayoutAlignment::automatic};

    friend constexpr bool operator==(const BoxAlignment& left, const BoxAlignment& right) noexcept {
        return left.justify_content == right.justify_content &&
               left.align_content == right.align_content && left.justify_items == right.justify_items &&
               left.align_items == right.align_items && left.justify_self == right.justify_self &&
               left.align_self == right.align_self;
    }
};

struct GUI_FORMS_API FlexContainerStyle final {
    FlexDirection direction{FlexDirection::row};
    FlexWrap wrap{FlexWrap::nowrap};

    friend constexpr bool operator==(const FlexContainerStyle& left,
                                     const FlexContainerStyle& right) noexcept {
        return left.direction == right.direction && left.wrap == right.wrap;
    }
};

struct GUI_FORMS_API FlexItemStyle final {
    FlexFactor grow{0.0};
    FlexFactor shrink{1.0};
    SizeValue basis{};
    std::int32_t order{};

    friend constexpr bool operator==(const FlexItemStyle& left, const FlexItemStyle& right) noexcept {
        return left.grow == right.grow && left.shrink == right.shrink && left.basis == right.basis &&
               left.order == right.order;
    }
};

// One end of a grid track's size: a length-percentage, a fraction (`fr`,
// a maximum only), or a content keyword.
enum class TrackBreadthKind : std::uint8_t { length_percentage, fraction, min_content, max_content, automatic };

struct GUI_FORMS_API TrackBreadth final {
    TrackBreadthKind kind{TrackBreadthKind::automatic};
    LengthPercentage length{};
    FractionFactor fraction{};

    friend constexpr bool operator==(const TrackBreadth& left, const TrackBreadth& right) noexcept {
        return left.kind == right.kind && left.length == right.length && left.fraction == right.fraction;
    }
};

// A track size: one breadth (both ends equal, except that a lone `<n>fr` is
// CSS's `minmax(auto, <n>fr)`), `minmax(minimum, maximum)`, or
// `fit-content(<length-percentage>)`, whose limit is `fit_content_limit`
// with minimum `auto` and maximum `max-content`.
struct GUI_FORMS_API GridTrackSize final {
    TrackBreadth minimum{};
    TrackBreadth maximum{};
    bool fit_content{};
    LengthPercentage fit_content_limit{};

    friend constexpr bool operator==(const GridTrackSize& left, const GridTrackSize& right) noexcept {
        return left.minimum == right.minimum && left.maximum == right.maximum &&
               left.fit_content == right.fit_content && left.fit_content_limit == right.fit_content_limit;
    }
};

enum class AutoRepeatKind : std::uint8_t { none, auto_fill, auto_fit };

// The most tracks one grid-template-columns or -rows value may expand to,
// `repeat()` counted, and the most sizes one grid-auto-columns or -rows
// value may list (PLAN section 5).
inline constexpr std::size_t maximum_grid_tracks = 10'000U;

// A grid-template-columns or -rows value. `repeat(<n>, ...)` is expanded
// into `tracks` when read; the one `repeat(auto-fill | auto-fit, ...)` a list
// may hold is kept apart, to be repeated against the container's size, and
// is inserted before `tracks[auto_repeat_index]`.
struct GUI_FORMS_API GridTrackList final {
    std::vector<GridTrackSize> tracks{};
    AutoRepeatKind auto_repeat{AutoRepeatKind::none};
    std::uint32_t auto_repeat_index{};
    std::vector<GridTrackSize> auto_repeat_tracks{};

    friend bool operator==(const GridTrackList& left, const GridTrackList& right) noexcept {
        return left.tracks == right.tracks && left.auto_repeat == right.auto_repeat &&
               left.auto_repeat_index == right.auto_repeat_index &&
               left.auto_repeat_tracks == right.auto_repeat_tracks;
    }
};

// One named area of grid-template-areas, as zero-based half-open line
// ranges of the template's cells.
struct GUI_FORMS_API GridNamedArea final {
    std::string name{};
    std::uint32_t row_start{};
    std::uint32_t row_end{};
    std::uint32_t column_start{};
    std::uint32_t column_end{};

    friend bool operator==(const GridNamedArea& left, const GridNamedArea& right) {
        return left.name == right.name && left.row_start == right.row_start &&
               left.row_end == right.row_end && left.column_start == right.column_start &&
               left.column_end == right.column_end;
    }
};

// grid-template-areas: its dimensions in cells and its areas in the order
// their first cells are written. Zero rows is `none`.
struct GUI_FORMS_API GridTemplateAreas final {
    std::uint32_t rows{};
    std::uint32_t columns{};
    std::vector<GridNamedArea> areas{};

    friend bool operator==(const GridTemplateAreas& left, const GridTemplateAreas& right) {
        return left.rows == right.rows && left.columns == right.columns && left.areas == right.areas;
    }
};

enum class GridAutoFlow : std::uint8_t { row, column, row_dense, column_dense };

struct GUI_FORMS_API GridContainerStyle final {
    GridTrackList template_columns{};
    GridTrackList template_rows{};
    GridTemplateAreas template_areas{};
    GridAutoFlow auto_flow{GridAutoFlow::row};
    // grid-auto-columns and grid-auto-rows; CSS's initial value is one `auto`.
    std::vector<GridTrackSize> auto_columns{GridTrackSize{}};
    std::vector<GridTrackSize> auto_rows{GridTrackSize{}};

    friend bool operator==(const GridContainerStyle& left, const GridContainerStyle& right) {
        return left.template_columns == right.template_columns &&
               left.template_rows == right.template_rows && left.template_areas == right.template_areas &&
               left.auto_flow == right.auto_flow && left.auto_columns == right.auto_columns &&
               left.auto_rows == right.auto_rows;
    }
};

// One grid line reference: `auto`, a line number (one-based, negative from
// the end, never zero), `span <n>`, or a named area's edge.
enum class GridLineKind : std::uint8_t { automatic, line, span, area };

struct GUI_FORMS_API GridLine final {
    GridLineKind kind{GridLineKind::automatic};
    std::int32_t number{};
    std::string area{};

    friend bool operator==(const GridLine& left, const GridLine& right) {
        return left.kind == right.kind && left.number == right.number && left.area == right.area;
    }
};

struct GUI_FORMS_API GridItemStyle final {
    GridLine column_start{};
    GridLine column_end{};
    GridLine row_start{};
    GridLine row_end{};

    friend bool operator==(const GridItemStyle& left, const GridItemStyle& right) {
        return left.column_start == right.column_start && left.column_end == right.column_end &&
               left.row_start == right.row_start && left.row_end == right.row_end;
    }
};

enum class TextAlignment : std::uint8_t { start, end, left, right, center };
enum class InlineVerticalAlignment : std::uint8_t { baseline, middle, top, bottom };

struct GUI_FORMS_API LayoutStyle final {
    DisplayKind display{DisplayKind::inline_level};
    PositionKind position{PositionKind::static_position};
    BoxSides<AutoLengthPercentage> inset{
        AutoLengthPercentage{true, {}}, AutoLengthPercentage{true, {}},
        AutoLengthPercentage{true, {}}, AutoLengthPercentage{true, {}}};
    SizeValue width{};
    SizeValue height{};
    SizeValue min_width{};
    SizeValue min_height{};
    SizeValue max_width{SizeKind::none, {}};
    SizeValue max_height{SizeKind::none, {}};
    BoxSides<AutoLengthPercentage> margin{};
    BoxSides<LengthPercentage> padding{};
    // row-gap and column-gap; `normal` is zero.
    LengthPercentage row_gap{};
    LengthPercentage column_gap{};
    BoxAlignment alignment{};
    FlexContainerStyle flex{};
    FlexItemStyle flex_item{};
    GridContainerStyle grid{};
    GridItemStyle grid_item{};
    TextAlignment text_align{TextAlignment::start};
    InlineVerticalAlignment vertical_align{InlineVerticalAlignment::baseline};

    friend bool operator==(const LayoutStyle& left, const LayoutStyle& right) {
        return left.display == right.display && left.position == right.position &&
               left.inset == right.inset && left.width == right.width && left.height == right.height &&
               left.min_width == right.min_width && left.min_height == right.min_height &&
               left.max_width == right.max_width && left.max_height == right.max_height &&
               left.margin == right.margin && left.padding == right.padding &&
               left.row_gap == right.row_gap && left.column_gap == right.column_gap &&
               left.alignment == right.alignment && left.flex == right.flex &&
               left.flex_item == right.flex_item && left.grid == right.grid &&
               left.grid_item == right.grid_item && left.text_align == right.text_align &&
               left.vertical_align == right.vertical_align;
    }
};

} // namespace gui_forms
