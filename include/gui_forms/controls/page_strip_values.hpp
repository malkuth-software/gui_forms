#pragma once

// PageStrip's setting types, apart from the control so its capabilities
// header can name them.

#include <cstdint>

namespace gui_forms {

// The edge of its content a strip sits on. The tabs touch that edge. A side
// strip (left or right) stacks its tabs from the top down and turns their
// labels (PartPaint::orientation): on the left edge they read from bottom to
// top (owner decision DK-42, interview PT8-1); on the right edge from top to
// bottom. The interview decided only the left edge; top to bottom on the
// right is Visual Studio's convention and keeps the glyph tops towards the
// content on both sides.
enum class PageStripPlacement : std::uint8_t { top, bottom, left, right };

// How wide each tab is. `content`: its label (and icon) plus padding, within
// the minimum and maximum. `fixed`: the fixed width, whatever the label.
// `fill`: the strip's width shared equally, within the minimum and maximum.
enum class PageItemSizing : std::uint8_t { content, fixed, fill };

// Whether moving the keyboard between tabs selects (automatic, the default:
// owner decision DK-37) or only moves the focus, Space or Enter then
// selecting (manual, for pages whose content is slow to show).
enum class PageStripActivation : std::uint8_t { automatic, manual };

// What the strip does when its tabs do not fit (owner decisions DK-31 and
// DK-37). `scroll`, the strip policy: scroll buttons and a list button at the
// strip's end; the tabs keep their widths. `shrink_then_list`, the document
// policy: ordinary tabs shrink towards the minimum width, the widest first;
// when even that does not fit, the tabs from the end of the order that do
// not fit are hidden behind a list button showing their count. Pinned tabs
// never shrink and are never hidden under `shrink_then_list`.
enum class PageStripOverflow : std::uint8_t { scroll, shrink_then_list };

// Which closable tabs show their close button (owner decision DK-38). Space
// for it is reserved on every closable tab whatever the rule, so a tab's
// width does not change when the button appears. `none` reserves nothing.
enum class PageCloseButtons : std::uint8_t { none, selected_and_hot, all };

// Tab widths in logical units at text scale 1. 0 < minimum <= maximum <=
// `maximum_width`, and 0 < fixed <= `maximum_width`.
struct PageItemWidths final {
    static constexpr double maximum_width = 1024.0;

    double minimum{40.0};
    double maximum{240.0};
    double fixed{120.0};

    friend bool operator==(const PageItemWidths& left, const PageItemWidths& right) noexcept {
        return left.minimum == right.minimum && left.maximum == right.maximum && left.fixed == right.fixed;
    }
};

} // namespace gui_forms
