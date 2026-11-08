#pragma once

// A CSS layout container's settings written as one inline style (the
// panel's `style` property) and each child's item settings as another (the
// `item_style` child property), read by the core layout style reader. A
// panel admits only the declarations it carries: the rest are refused, and
// so are the reader's warnings, so a declaration is never dropped silently.
// Internal to the controls library.
//
// One source of truth per fact: a child's margin lengths stay its own
// `margin` property (an item style admits only `auto` margins), and a grid
// child's placement stays its `area` and `cell` child properties with
// spans written as span blocks (a grid's style refuses
// grid-template-areas and an item style the placement lines).

#include "gui_forms/layout/style/layout_style.hpp"

#include <string>
#include <string_view>

namespace gui_forms::layout_style_property {

// The declarations of `text` applied on top of `base`. Throws
// std::invalid_argument, naming `property` ("GridPanel.style") and each
// diagnostic, when the reader reports any error or warning; the caller's
// state is untouched then.
[[nodiscard]] LayoutStyle read(std::string_view text, const LayoutStyle& base, std::string_view property);

// Refuses what `read` declares beyond what the panel kept: `kept` is the
// base with the admitted fields copied from `read`. Throws
// std::invalid_argument naming `property`, the refused declarations and
// `guidance` when the two differ.
void require_admitted(const LayoutStyle& read, const LayoutStyle& kept, std::string_view property,
                      std::string_view guidance);

// The declarations of `style` that differ from `base`, as the reader
// spells them ("" when none).
[[nodiscard]] std::string describe(const LayoutStyle& style, const LayoutStyle& base);

// Copies the sides of `read`'s margin that are `auto` into `kept` and
// answers them; a side with a length is left for require_admitted to
// refuse unless it is zero.
[[nodiscard]] BoxSides<bool> keep_auto_margins(const LayoutStyle& read, LayoutStyle& kept);

// The margin a style describes for `auto_margin`: `auto` on the marked
// sides, zero elsewhere.
[[nodiscard]] BoxSides<AutoLengthPercentage> auto_margin_sides(const BoxSides<bool>& auto_margin) noexcept;

// The box fields every item layout shares (FlexItemLayout, GridItemLayout,
// BlockFlowItemLayout): width, height, their minimums and maximums, the
// `auto` margins, `position` and the insets. keep_item_box copies them from
// `read` into `item` and `kept`; describe_item_box writes `item`'s into
// `style`.
template <typename ItemLayout>
void keep_item_box(const LayoutStyle& read, LayoutStyle& kept, ItemLayout& item) {
    item.width = read.width;
    item.height = read.height;
    item.min_width = read.min_width;
    item.min_height = read.min_height;
    item.max_width = read.max_width;
    item.max_height = read.max_height;
    item.auto_margin = keep_auto_margins(read, kept);
    item.positioning.position = read.position;
    item.positioning.inset = read.inset;
    kept.width = read.width;
    kept.height = read.height;
    kept.min_width = read.min_width;
    kept.min_height = read.min_height;
    kept.max_width = read.max_width;
    kept.max_height = read.max_height;
    kept.position = read.position;
    kept.inset = read.inset;
}

template <typename ItemLayout>
void describe_item_box(const ItemLayout& item, LayoutStyle& style) {
    style.width = item.width;
    style.height = item.height;
    style.min_width = item.min_width;
    style.min_height = item.min_height;
    style.max_width = item.max_width;
    style.max_height = item.max_height;
    style.margin = auto_margin_sides(item.auto_margin);
    style.position = item.positioning.position;
    style.inset = item.positioning.inset;
}

} // namespace gui_forms::layout_style_property
