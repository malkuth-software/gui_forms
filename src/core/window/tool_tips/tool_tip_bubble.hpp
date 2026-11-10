#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"

#include <memory>
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
    // The line being built while breaking, kept with the lines so that a
    // warm break reuses its storage (libstdc++'s short-string buffer holds
    // 15 bytes, libc++'s 22: a line between them allocates only on Linux).
    std::string scratch{};
    FontSpec font{};
    double text_width{};
    double line_height{};
};

// Breaks `text` into lines no wider than `maximum_width` where it can: at
// line feeds, then between words (runs of characters other than space and
// tab), and inside a word wider than the width between graphemes. Measures
// with `measurer`'s attached text engine, so the bubble breaks as its
// window draws. Writes `lines` (its storage reused: lines it already holds
// are assigned in place, never destroyed and rebuilt) and returns nothing.
void break_tool_tip_lines(const Control& measurer, std::string_view text, double maximum_width,
                          ToolTipLines& lines);

// The face's size for `lines`: the text block inside the margins.
[[nodiscard]] Size tool_tip_face_size(const ToolTipLines& lines) noexcept;

// Paints one bubble of `face` with `lines` at the origin, its shadow (when
// `shadow`) falling outside the face: the bubble's own painting, shared
// with the Splash baker's recording (ToolTipPresenter::prebake).
// `theme_source` is the control whose theme and text engine the parts use.
void paint_tool_tip_bubble(Painter& painter, const Control& theme_source, Size face, const ToolTipLines& lines,
                           bool shadow);

// What a baked bubble was painted from (APP_WINDOW_RULES_DESIGN Part S,
// phase 2). A baked raster stands for the bubble's own painting only while
// every field matches what the bubble would paint now.
struct ToolTipBakeKey final {
    std::string text{};
    Size face{};
    FontSpec font{};
    bool shadow{};
    // The display scale of the window it was baked for.
    double scale{};

    friend bool operator==(const ToolTipBakeKey& left, const ToolTipBakeKey& right) noexcept {
        return left.text == right.text && left.face == right.face && left.font == right.font &&
               left.shadow == right.shadow && left.scale == right.scale;
    }
};

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
    // Offers `raster`, baked off the UI thread from `key` with the face's
    // top left at the visual outsets' offset, for the next paint. That paint
    // blits it when `key` still matches the bubble (its text, arranged face,
    // font, shadow and window scale) and paints itself otherwise; either way
    // the offer is used once, so a later repaint (a theme change) paints
    // live. set_content withdraws it.
    void set_baked(std::shared_ptr<const PartRaster> raster, ToolTipBakeKey key);
    [[nodiscard]] bool baked() const noexcept { return baked_ != nullptr; }
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] bool hit_test_local(Point point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

private:
    std::string text_;
    ToolTipLines lines_;
    bool shadow_{true};
    // The offered bake and what it was painted from (set_baked).
    std::shared_ptr<const PartRaster> baked_{};
    ToolTipBakeKey baked_key_{};
};

} // namespace gui_forms::detail
