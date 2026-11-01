#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
class TextBlock final : public Control {
public:
    explicit TextBlock(StableId stable_id, std::string text = {});

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void set_text(GUI_FORMS_LOCALIZABLE(text) std::string text);
    // The authored font, or the theme's field typography when none is set.
    // Layout and paint use it at the effective text scale. set_font throws
    // std::invalid_argument for an invalid FontSpec.
    [[nodiscard]] FontSpec font() const noexcept;
    [[nodiscard]] bool has_font_override() const noexcept { return font_override_.has_value(); }
    void set_font(FontSpec font);
    void clear_font();
    [[nodiscard]] Color foreground() const noexcept;
    [[nodiscard]] bool has_foreground_override() const noexcept {
        return foreground_override_.has_value();
    }
    void set_foreground(Color color);
    void clear_foreground();
    // CSS text-align. `start` and `end` follow the effective layout
    // direction. Throws std::invalid_argument outside the enumeration.
    [[nodiscard]] TextAlignment text_align() const noexcept { return text_align_; }
    void set_text_align(TextAlignment alignment);
    // CSS line-height as a length, in logical pixels at text scale 1; none
    // is `normal`. Throws std::invalid_argument unless finite and positive.
    [[nodiscard]] std::optional<double> line_height() const noexcept { return line_height_; }
    void set_line_height(double line_height);
    void clear_line_height();
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

    [[nodiscard]] PropertyValueOrigin font_property_origin() const noexcept;
    [[nodiscard]] PropertyValueOrigin foreground_property_origin() const noexcept;
    [[nodiscard]] BindingValue text_align_property_value() const;
    void set_text_align_property_value(const BindingValue& value);
    [[nodiscard]] BindingValue line_height_property_value() const;
    void set_line_height_property_value(const BindingValue& value);
    // Fills workspace.collapsed and workspace.opportunities from text_.
    void prepare_text(Workspace& workspace) const;
    // Fills workspace.lines with the prepared text wrapped at
    // `content_width`.
    void break_lines(double content_width, FontSpec font, Workspace& workspace) const;
    [[nodiscard]] double run_width(std::string_view run, FontSpec font) const;
    // The widest run between two break opportunities of the prepared text.
    [[nodiscard]] double widest_unbreakable_run(const Workspace& workspace, FontSpec font) const;
    // The line box every line shares (line_box_metrics in `font`).
    [[nodiscard]] LineBoxMetrics line_metrics(FontSpec font) const;
    // Prepares and wraps the text at `content_width` into `workspace` and
    // returns the lines' size: the widest line by the lines' total height.
    [[nodiscard]] Size content_size(double content_width, Workspace& workspace) const;
    [[nodiscard]] double line_x(const TextLine& line, double content_width) const noexcept;

    std::string text_;
    std::optional<FontSpec> font_override_;
    std::optional<Color> foreground_override_;
    TextAlignment text_align_{TextAlignment::start};
    std::optional<double> line_height_;
    // Owned by this control and used on its UI thread only; measure, the
    // baselines and paint each refill it before reading it.
    Workspace workspace_{};
    Event<const std::string&> text_changed_;
};

} // namespace gui_forms
