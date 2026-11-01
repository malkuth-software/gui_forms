#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

enum class HorizontalAlignment : std::uint8_t {
    near,
    center,
    far,
};

enum class VerticalAlignment : std::uint8_t {
    near,
    center,
    far,
};

enum class TextWrapping : std::uint8_t {
    no_wrap,
    word,
};

enum class TextStyleRole : std::uint8_t {
    body,
    control,
    caption,
    heading,
    title,
    monospace,
};

enum class TextCaseTransform : std::uint8_t {
    none,
    uppercase_ascii,
    lowercase_ascii,
};

class Label : public Control {
public:
    explicit Label(StableId stable_id, std::string text = {});

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    virtual void set_text(GUI_FORMS_LOCALIZABLE(text) std::string text);
    [[nodiscard]] FontSpec font() const noexcept;
    [[nodiscard]] bool has_font_override() const noexcept {
        return font_override_.has_value();
    }
    void set_font(FontSpec font);
    void clear_font();
    [[nodiscard]] TextStyleRole text_style_role() const noexcept {
        return text_style_role_;
    }
    void set_text_style_role(TextStyleRole role);
    [[nodiscard]] Color foreground() const noexcept;
    [[nodiscard]] bool has_foreground_override() const noexcept {
        return foreground_override_.has_value();
    }
    void set_foreground(Color color);
    void clear_foreground();
    [[nodiscard]] HorizontalAlignment alignment() const noexcept { return alignment_; }
    void set_alignment(HorizontalAlignment alignment);
    [[nodiscard]] VerticalAlignment vertical_alignment() const noexcept {
        return vertical_alignment_;
    }
    void set_vertical_alignment(VerticalAlignment alignment);
    [[nodiscard]] TextWrapping text_wrapping() const noexcept { return text_wrapping_; }
    void set_text_wrapping(TextWrapping wrapping);
    [[nodiscard]] double line_spacing() const noexcept { return line_spacing_; }
    void set_line_spacing(double spacing);
    [[nodiscard]] TextCaseTransform text_case_transform() const noexcept {
        return text_case_transform_;
    }
    void set_text_case_transform(TextCaseTransform transform);
    // Zero keeps all produced lines. A positive value bounds both desired
    // height and painting while semantics retain the complete authored text.
    [[nodiscard]] std::size_t maximum_lines() const noexcept {
        return maximum_lines_;
    }
    void set_maximum_lines(std::size_t maximum_lines);
    [[nodiscard]] bool use_mnemonic() const noexcept { return use_mnemonic_; }
    void set_use_mnemonic(bool value);
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
    void paint_label_text(Painter& painter, std::string_view text) const;
    // Measures with `painter` when one is given, otherwise with the window's
    // text metrics, which the host's raster painter provides.
    [[nodiscard]] TextLayout layout_label_text(std::string_view text,
                                               Painter* painter) const;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // Measure with the text wrapped at `content_width_limit` (the width
    // inside the 4 px inset), clamped to `available`.
    [[nodiscard]] Size measure_wrapped_text(double content_width_limit,
                                            Size available) const;
    [[nodiscard]] double widest_unbreakable_segment(std::string_view text,
                                                    FontSpec font) const;
    [[nodiscard]] PropertyValueOrigin font_property_origin() const noexcept;
    [[nodiscard]] PropertyValueOrigin foreground_property_origin() const noexcept;
    [[nodiscard]] BindingValue maximum_lines_property_value() const;
    void set_maximum_lines_property_value(const BindingValue& value);
    [[nodiscard]] bool mnemonic_matches(
        char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;
    std::string text_;
    std::optional<FontSpec> font_override_;
    std::optional<Color> foreground_override_;
    TextStyleRole text_style_role_{TextStyleRole::body};
    HorizontalAlignment alignment_{HorizontalAlignment::near};
    VerticalAlignment vertical_alignment_{VerticalAlignment::center};
    TextWrapping text_wrapping_{TextWrapping::no_wrap};
    double line_spacing_{1.25};
    TextCaseTransform text_case_transform_{TextCaseTransform::none};
    std::size_t maximum_lines_{};
    bool use_mnemonic_{true};
    Event<const std::string&> text_changed_;
};

} // namespace gui_forms
