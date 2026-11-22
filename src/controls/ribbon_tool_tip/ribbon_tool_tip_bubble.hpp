#pragma once

#include "gui_forms/control.hpp"

#include <string>
#include <vector>

namespace gui_forms {

// What one enhanced tooltip shows, already resolved from its command.
// `disabled_reason` is empty unless the target is disabled, and `help_line`
// empty unless the mapping has a help topic.
struct RibbonToolTipBubbleContent final {
    std::string title;
    std::string shortcut;
    std::string description;
    std::string disabled_reason;
    std::string help_line;
    ImageId image{};
};

// One wrapped line of the description or the disabled reason.
struct RibbonToolTipLine final {
    Rect bounds{};
    std::string text;
};

// Where every part of the bubble sits, in the bubble's local logical units,
// from one pass: the overlay is sized from it and the bubble paints from it.
// `size` is the face's; `shadow` is the casting surface's rect (the face),
// and the theme's overlay shadow falls outside it, within the bubble's
// visual outsets.
struct RibbonToolTipLayout final {
    Size size{};
    Rect face{};
    Rect shadow{};
    Rect title{};
    Rect shortcut{};
    Rect image{};
    std::vector<RibbonToolTipLine> description;
    std::vector<RibbonToolTipLine> disabled_reason;
    Rect help_rule{};
    Rect help_line{};
    FontSpec title_font{};
    FontSpec body_font{};
};

// Lays out `content` with `measurer`'s effective fonts and text provider,
// wrapping the description (beside the image) at `wrap_width` logical units
// and the reason as wide as the description's column reaches; the face is
// as wide as its widest line, title row included, inside the margins.
[[nodiscard]] RibbonToolTipLayout layout_ribbon_tool_tip(const Control& measurer,
                                                         const RibbonToolTipBubbleContent& content,
                                                         double wrap_width);

// Lays out `content` at the wrap width the tooltip width rule picks
// (tool_tip_width.hpp), as every tooltip is sized: a description and
// reason whose widest paragraph fits the one-line width stay on one line
// per paragraph; longer text wraps to the measured candidate whose face
// comes nearest the golden box. `cap` and `room` bound the face's text
// (room ignored when not positive), in logical units.
[[nodiscard]] RibbonToolTipLayout fit_ribbon_tool_tip(const Control& measurer,
                                                      const RibbonToolTipBubbleContent& content, double cap,
                                                      double room);

// The overlay of one enhanced tooltip: input-transparent, unfocusable, and
// painted by role from its layout.
class RibbonToolTipBubble final : public Control {
public:
    RibbonToolTipBubble(StableId stable_id, RibbonToolTipBubbleContent content, RibbonToolTipLayout layout);

    void on_paint(Painter& painter, Rect local_damage) override;
    // The theme's overlay shadow reach, outside the bubble's bounds.
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] bool hit_test_local(Point point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const RibbonToolTipLayout& layout() const noexcept { return layout_; }

private:
    RibbonToolTipBubbleContent content_;
    RibbonToolTipLayout layout_;
};

} // namespace gui_forms
