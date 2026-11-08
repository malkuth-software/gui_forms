#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/text_block/text_block_capabilities/text_block_capabilities.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

// A block of text laid out as CSS lays out the text of a block (stage L,
// phase L8; notes:planning/stages/L-layout/PLAN.md section 12):
// - white space collapses as `white-space: normal` collapses it;
// - lines break at the window's line-break opportunities (UAX #14 with the
//   bundled text engine, white space otherwise), and a word wider than the
//   line overflows on a line of its own, as `overflow-wrap: normal` does;
// - each line is CSS's `line-height: normal` tall unless a line height is
//   set, and the text has no inset.
// It publishes its first and last baselines to the layout containers. Use it
// for text inside FlexPanel, GridPanel and BlockPanel; Label keeps Windows
// Forms' label model (its inset and line spacing).
class TextBlock final : public Control, public TextBlockCapabilities<TextBlock> {
public:
    explicit TextBlock(StableId stable_id, std::string initial_text = {});

    // The font the block lays out and paints with: its own `font` when set,
    // otherwise the theme's field typography. Layout and paint use it at the
    // effective text scale.
    [[nodiscard]] FontSpec resolved_font() const noexcept;
    // The colour the text paints in: Control's `fore_color` when set,
    // otherwise the theme's panel text colour.
    [[nodiscard]] Color foreground() const noexcept;
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept { return text_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    // The first line's baseline, from the top of the border box; none for a
    // block with no text, which has no line box.
    [[nodiscard]] std::optional<double> first_baseline(double width) override;
    // The last line's baseline at `width` (the border box's width).
    [[nodiscard]] std::optional<double> last_baseline(double width) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts); its states are
    // Control's. `text` is the box the lines fill: the content box's width
    // and the lines' height, absent with no text.
    struct Parts final {
        PartGeometry region;
        PartGeometry text;
    };
    [[nodiscard]] Parts parts() const;

protected:
    // Min-content is the widest run of text between two break
    // opportunities, max-content the whole text on one line; a definite or
    // at-most width wraps the text at that width less the padding. Each
    // adds the padding, and a definite axis reports exactly its extent.
    [[nodiscard]] Size measure_intrinsic(const LayoutConstraint& constraint) override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // One line: a byte range of the collapsed text, its trailing space
    // excluded, and the range's shaped width.
    struct TextLine final {
        std::size_t begin_byte{};
        std::size_t end_byte{};
        double width{};
    };
    // The text as laid out at one content width: the collapsed text, the
    // break opportunity after each of its bytes (live length equal to its
    // byte count) and its lines. Each pass refills it; the storage is kept
    // from pass to pass so a steady text allocates nothing.
    struct Workspace final {
        std::string collapsed;
        std::vector<LineBreakOpportunity> opportunities;
        std::vector<TextLine> lines;
    };

    // The font entry's operations: the read is the resolved font, a reset
    // clears the block's own, and it serializes while the block has one.
    [[nodiscard]] BindingValue font_property_value() const;
    void reset_font();
    [[nodiscard]] bool font_serialized() const;
    [[nodiscard]] PropertyValueOrigin font_property_origin() const noexcept;
    // The fore_color entry, which replaces Control's in TextBlock's table
    // (the schema keeps TextBlock's: the resolved colour, neither bindable
    // nor nullable). The write and reset go to Control's fore_color cell.
    [[nodiscard]] BindingValue fore_color_property_value() const;
    void write_fore_color_property(const BindingValue& value);
    void reset_fore_color_property();
    [[nodiscard]] bool fore_color_serialized() const;
    [[nodiscard]] PropertyValueOrigin fore_color_property_origin() const noexcept;
    // The line_height entry: zero reads and writes as `normal` (empty).
    [[nodiscard]] BindingValue line_height_property_value() const;
    void write_line_height_property(const BindingValue& value);
    // Fills workspace.collapsed and workspace.opportunities from text.
    void prepare_text(Workspace& workspace) const;
    // Fills workspace.lines with the prepared text wrapped at
    // `content_width`.
    void break_lines(double content_width, FontSpec face, Workspace& workspace) const;
    [[nodiscard]] double run_width(std::string_view run, FontSpec face) const;
    // The widest run between two break opportunities of the prepared text.
    [[nodiscard]] double widest_unbreakable_run(const Workspace& workspace, FontSpec face) const;
    // The line box every line shares (line_box_metrics in `face`).
    [[nodiscard]] LineBoxMetrics line_metrics(FontSpec face) const;
    // Prepares and wraps the text at `content_width` into `workspace` and
    // returns the lines' size: the widest line by the lines' total height.
    [[nodiscard]] Size content_size(double content_width, Workspace& workspace) const;
    [[nodiscard]] double line_x(const TextLine& line, double content_width) const noexcept;

    // Owned by this control and used on its UI thread only; measure, the
    // baselines and paint each refill it before reading it.
    Workspace workspace_{};
    Event<const std::string&> text_changed_;
    // property_codemod begin TextBlock (tools/property_families/text_block.json; generated, do not edit)
public:
    // TextBlock's property table: Control's entries, then its own.
    static constexpr std::size_t text_block_property_count = Control::control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, text_block_property_count>& text_block_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_text_changed();
    virtual void on_font_changed();

private:
    friend struct TextCapability<TextBlock>;
    friend struct TextBlockFontCapability<TextBlock>;
    friend struct TextBlockTextAlignCapability<TextBlock>;
    friend struct TextBlockLineHeightCapability<TextBlock>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(font)& cell, std::optional<FontSpec>& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(text_align)& cell, TextAlignment& incoming);
    void written(const decltype(text_align)& cell);
    void prepare_write(const decltype(line_height)& cell, std::optional<double>& incoming);
    void written(const decltype(line_height)& cell);
    // property_codemod end TextBlock
};

} // namespace gui_forms
