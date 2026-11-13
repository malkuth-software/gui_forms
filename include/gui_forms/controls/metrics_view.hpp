#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/metrics_view_capabilities.hpp"

#include <span>
#include <string>

namespace gui_forms {

// Renderer-neutral view over the structured Window metrics snapshot.
class GUI_FORMS_API MetricsView final : public Control, public MetricsViewCapabilities<MetricsView> {
public:
    explicit MetricsView(StableId stable_id,
                         std::string initial_title = "Runtime metrics");

    [[nodiscard]] const BasicControlStyle& style() const noexcept {
        return style_;
    }
    void set_style(BasicControlStyle style);

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry accent_strip;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
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
    [[nodiscard]] std::string metrics_text() const;
    // The counters as the card last painted them, and its accessible value.
    // Hosts keep a published tree until the window's semantic generation
    // changes, so a value read live at each query (it changes with every
    // paint) would reach them a paint late or never. A paint that changes
    // this text invalidates semantics, which advances the generation; until
    // the first paint it is empty.
    std::string painted_metrics_;
    BasicControlStyle style_;
    // property_codemod begin MetricsView (tools/property_families/metrics_view.json; generated, do not edit)
private:
    friend struct MetricsViewTitleCapability<MetricsView>;
    friend struct MetricsViewAccentWidthCapability<MetricsView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(accent_width)& cell, double& incoming);
    void written(const decltype(accent_width)& cell);
    // property_codemod end MetricsView
};

} // namespace gui_forms
