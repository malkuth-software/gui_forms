#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/controls/label/label_values/label_values.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/label/label_capabilities/label_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

class Label : public Control, public LabelCapabilities<Label> {
public:
    explicit Label(StableId stable_id, std::string initial_text = {});

    // M-S transient forwarders: the integrator's rewrite replaces every call
    // with an assignment of the cell and deletes them in the same batch.
    void set_text(std::string value) { text = std::move(value); } // M-S transient forwarder
    void set_font(FontSpec value) { font = value; } // M-S transient forwarder

    // The font the label measures and paints with: its own `font` when set,
    // otherwise the theme typography of `text_style_role`.
    [[nodiscard]] FontSpec resolved_font() const noexcept;
    // The colour the text paints in: Control's `fore_color` when set,
    // otherwise the theme's text colour for a label.
    [[nodiscard]] Color foreground() const;
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept {
        return text_changed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts); its states are Control's.
    struct Parts final {
        PartGeometry region;
        PartGeometry label;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    // Min-content is the widest unbreakable segment: the widest
    // whitespace-delimited word of any paragraph, so word wrapping never has
    // to break a word (CSS `overflow-wrap: break-word`). Max-content is the
    // widest paragraph unwrapped. Both include the 4 px text inset, and the
    // height is the wrapped height at that width. A label that does not wrap,
    // or has a requested width, answers both from its preferred size. The
    // label does not wrap within 4 px of content width, so when the widest
    // word is at most 4 px wide min-content equals max-content.
    [[nodiscard]] Size measure_intrinsic(const LayoutConstraint& constraint) override;
    // The lines of a label's text placed in its arranged bounds (stage T1):
    // each line's drawing origin (left edge, snapped baseline) and width, and
    // the block the lines fill. Paint and published geometry share it.
    struct TextLayout final {
        std::vector<std::string> lines;
        std::vector<Point> origins;
        std::vector<double> widths;
        Rect block{};
    };

    [[nodiscard]] virtual std::string display_text() const;
    void paint_label_text(Painter& painter, std::string_view shown) const;
    // Measures with `painter` when one is given, otherwise with the window's
    // text metrics, which the host's raster painter provides.
    [[nodiscard]] TextLayout layout_label_text(std::string_view shown,
                                               Painter* painter) const;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // Measure with the text wrapped at `content_width_limit` (the width
    // inside the 4 px inset), clamped to `available`.
    [[nodiscard]] Size measure_wrapped_text(double content_width_limit,
                                            Size available) const;
    [[nodiscard]] double widest_unbreakable_segment(std::string_view shown,
                                                    FontSpec face) const;
    // The font entry's operations: the read is the resolved font, a reset
    // clears the label's own, and it serializes while the label has one.
    [[nodiscard]] BindingValue font_property_value() const;
    void reset_font();
    [[nodiscard]] bool font_serialized() const;
    [[nodiscard]] PropertyValueOrigin font_property_origin() const noexcept;
    // The fore_color entry, which replaces Control's in Label's table (the
    // schema keeps Label's: the resolved colour, neither bindable nor
    // nullable). The write and reset go to Control's fore_color cell.
    [[nodiscard]] BindingValue fore_color_property_value() const;
    void write_fore_color_property(const BindingValue& value);
    void reset_fore_color_property();
    [[nodiscard]] bool fore_color_serialized() const;
    [[nodiscard]] PropertyValueOrigin fore_color_property_origin() const noexcept;
    [[nodiscard]] bool mnemonic_matches(
        char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;
    Event<const std::string&> text_changed_;
    // property_codemod begin Label (tools/property_families/label.json; generated, do not edit)
public:
    // Label's property table: Control's entries, then its own.
    static constexpr std::size_t label_property_count = Control::control_property_count + 6U;
    [[nodiscard]] static const std::array<PropertyEntry, label_property_count>& label_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's touch and raises the
    // change event; an override calls the base. coerce_<property> runs last
    // before the compare and may rewrite the incoming value.
    virtual void on_text_changed();
    virtual void coerce_text(std::string& incoming);

private:
    friend struct TextCapability<Label>;
    friend struct LabelFontCapability<Label>;
    friend struct LabelAlignmentCapability<Label>;
    friend struct LabelVerticalAlignmentCapability<Label>;
    friend struct LabelTextWrappingCapability<Label>;
    friend struct LabelMaximumLinesCapability<Label>;
    friend struct LabelTextStyleRoleCapability<Label>;
    friend struct LabelLineSpacingCapability<Label>;
    friend struct LabelTextCaseTransformCapability<Label>;
    friend struct LabelUseMnemonicCapability<Label>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(font)& cell, std::optional<FontSpec>& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(alignment)& cell, HorizontalAlignment& incoming);
    void written(const decltype(alignment)& cell);
    void prepare_write(const decltype(vertical_alignment)& cell, VerticalAlignment& incoming);
    void written(const decltype(vertical_alignment)& cell);
    void prepare_write(const decltype(text_wrapping)& cell, TextWrapping& incoming);
    void written(const decltype(text_wrapping)& cell);
    void prepare_write(const decltype(maximum_lines)& cell, std::uint32_t& incoming);
    void written(const decltype(maximum_lines)& cell);
    void prepare_write(const decltype(text_style_role)& cell, TextStyleRole& incoming);
    void written(const decltype(text_style_role)& cell);
    void prepare_write(const decltype(line_spacing)& cell, double& incoming);
    void written(const decltype(line_spacing)& cell);
    void prepare_write(const decltype(text_case_transform)& cell, TextCaseTransform& incoming);
    void written(const decltype(text_case_transform)& cell);
    void prepare_write(const decltype(use_mnemonic)& cell, bool& incoming);
    void written(const decltype(use_mnemonic)& cell);
    // property_codemod end Label
};

} // namespace gui_forms
