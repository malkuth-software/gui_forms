#pragma once

#include "gui_forms/control.hpp"

#include <string>
#include <vector>

namespace gui_forms::detail {

// The full-window layer a tooltip's bubble sits in: drawn on the overlay
// plane, never hit by the pointer, never focused.
class ToolTipLayer final : public Control {
public:
    explicit ToolTipLayer(StableId stable_id);
    [[nodiscard]] bool hit_test_local(Point point) const override;
};

// The text of one tooltip broken into lines, measured with `font`, the
// effective font the bubble draws with. `text_width` is the widest line's
// width and `line_height` each line's height, in logical units.
struct ToolTipLines final {
    std::vector<std::string> lines{};
    FontSpec font{};
    double text_width{};
    double line_height{};
};

// Breaks `text` into lines no wider than `maximum_width` where it can: at
// line feeds, then between words (runs of characters other than space and
// tab), and inside a word wider than the width between graphemes. Measures
// with `measurer`'s attached text engine, so the bubble breaks as its
// window draws. Writes `lines` (its storage reused) and returns nothing.
void break_tool_tip_lines(const Control& measurer, std::string_view text, double maximum_width,
                          ToolTipLines& lines);

// One tooltip's bubble: the `tool_tip` family's face casting the theme's
// overlay shadow (owner decisions RB-37, RB-38), with its text drawn line
// by line as label parts. Its bounds are the face; the shadow falls outside
// them, within its visual outsets. It is never hit and publishes one
// tool_tip semantic node named by its text.
class ToolTipBubble final : public Control {
public:
    // The space between the face's edge and the text block.
    static constexpr double horizontal_margin = 9.0;
    static constexpr double vertical_margin = 5.0;

    ToolTipBubble(StableId stable_id, std::string text, ToolTipLines lines);

    // Shows `text` with `lines` (swapped in: `lines` receives the old lines'
    // storage for reuse) and asks for layout, paint and semantics again.
    void set_content(std::string_view text, ToolTipLines& lines);
    // The face's size for these lines.
    [[nodiscard]] Size face_size() const noexcept;
    // Whether the bubble casts its shadow (true until changed). An opaque
    // Splash window has no room for one: the bubble then paints its face
    // only and reports no outsets.
    void set_shadow(bool cast) noexcept;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] bool hit_test_local(Point point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

private:
    std::string text_;
    ToolTipLines lines_;
    bool shadow_{true};
};

} // namespace gui_forms::detail
