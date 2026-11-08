#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/controls/typography_specimen/typography_specimen_capabilities/typography_specimen_capabilities.hpp"

#include <span>
#include <string>
#include <utility>

namespace gui_forms {

// Renderer-neutral diagnostic control for typography review. It draws one
// committed text run with its logical line box, resolved baseline, and exact
// provider status. It is a review instrument, not a File Manager control.
class TypographySpecimen final : public Control, public TypographySpecimenCapabilities<TypographySpecimen> {
public:
    explicit TypographySpecimen(StableId stable_id, std::string initial_text = {});


    [[nodiscard]] ResolvedTextLayout resolved_layout() const;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry focus;
    };
    struct States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // property_codemod begin TypographySpecimen (tools/property_families/typography_specimen.json; generated, do not edit)
public:
    // TypographySpecimen's property table: Control's entries, then its own.
    static constexpr std::size_t typography_specimen_property_count = Control::control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, typography_specimen_property_count>& typography_specimen_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct TextCapability<TypographySpecimen>;
    friend struct TypographySpecimenFontCapability<TypographySpecimen>;
    friend struct TypographySpecimenCaptionCapability<TypographySpecimen>;
    friend struct TypographySpecimenDiagnosticsVisibleCapability<TypographySpecimen>;
    friend struct TypographySpecimenWrapTextCapability<TypographySpecimen>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(caption)& cell, std::string& incoming);
    void written(const decltype(caption)& cell);
    void prepare_write(const decltype(diagnostics_visible)& cell, bool& incoming);
    void written(const decltype(diagnostics_visible)& cell);
    void prepare_write(const decltype(wrap_text)& cell, bool& incoming);
    void written(const decltype(wrap_text)& cell);
    // property_codemod end TypographySpecimen
};

} // namespace gui_forms
