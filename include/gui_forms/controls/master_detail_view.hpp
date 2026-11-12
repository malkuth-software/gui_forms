#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/controls/master_detail_view_values.hpp"
#include "gui_forms/controls/master_detail_view_capabilities.hpp"

#include <cstdint>
#include <memory>

namespace gui_forms {

struct GUI_FORMS_API MasterDetailLayout final {
    Orientation orientation{Orientation::vertical};
    double master_extent{260.0};
    double splitter_width{3.0};
    double splitter_hit_width{9.0};
    double master_minimum{120.0};
    double detail_minimum{240.0};
    double compact_threshold{720.0};
    bool resizable{true};

    friend constexpr bool operator==(const MasterDetailLayout& left,
                                     const MasterDetailLayout& right) noexcept {
        return left.orientation == right.orientation &&
               left.master_extent == right.master_extent &&
               left.splitter_width == right.splitter_width &&
               left.splitter_hit_width == right.splitter_hit_width &&
               left.master_minimum == right.master_minimum &&
               left.detail_minimum == right.detail_minimum &&
               left.compact_threshold == right.compact_threshold &&
               left.resizable == right.resizable;
    }
};

struct GUI_FORMS_API MasterDetailPresentationChange final {
    MasterDetailDisplayMode previous{MasterDetailDisplayMode::side_by_side};
    MasterDetailDisplayMode current{MasterDetailDisplayMode::side_by_side};
    bool automatic{};
};

// A renderer-neutral retained master/detail shell. Consumers provide ordinary
// controls for both roles; GUI.Forms owns responsive presentation, a genuine
// draggable/keyboard splitter, focus transfer on collapse, and compact
// master/detail navigation. This is intentionally content-agnostic so decision
// browsers, settings panes, inspectors, and navigation stacks share one law.
class GUI_FORMS_API MasterDetailView final : public ContainerControl, public MasterDetailViewCapabilities<MasterDetailView> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit MasterDetailView(StableId stable_id);
    void initialize_control_tree();

    [[nodiscard]] Control::Ptr master() const noexcept { return master_; }
    [[nodiscard]] Control::Ptr detail() const noexcept { return detail_; }
    [[nodiscard]] Control::Ptr set_master(Control::Ptr control);
    [[nodiscard]] Control::Ptr set_detail(Control::Ptr control);
    [[nodiscard]] std::shared_ptr<SplitContainer> split_container() const noexcept {
        return split_;
    }

    [[nodiscard]] const MasterDetailLayout& master_detail_layout() const noexcept {
        return layout_;
    }
    [[nodiscard]] bool uses_theme_layout() const noexcept {
        return uses_theme_layout_;
    }
    [[nodiscard]] MasterDetailLayout effective_master_detail_layout() const noexcept;
    void set_master_detail_layout(MasterDetailLayout layout);
    void reset_master_detail_layout_to_theme();
    [[nodiscard]] MasterDetailDisplayMode effective_display_mode() const noexcept {
        return effective_display_mode_;
    }
    void show_master();
    void show_detail();

    [[nodiscard]] Event<const MasterDetailPresentationChange&>&
    presentation_changed() noexcept { return presentation_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    [[nodiscard]] Control::Ptr replace_role(Control::Ptr& slot,
                                            const std::shared_ptr<SplitterPanel>& panel,
                                            Control::Ptr replacement);
    [[nodiscard]] MasterDetailDisplayMode resolve_display_mode(
        Size available) const noexcept;
    void apply_display_mode(MasterDetailDisplayMode mode, bool automatic);
    void configure_split();

    std::shared_ptr<SplitContainer> split_;
    Control::Ptr master_;
    Control::Ptr detail_;
    MasterDetailLayout layout_;
    Event<const MasterDetailPresentationChange&> presentation_changed_;
    MasterDetailDisplayMode effective_display_mode_{
        MasterDetailDisplayMode::side_by_side};
    bool initialized_{};
    bool uses_theme_layout_{true};
    // property_codemod begin MasterDetailView (tools/property_families/master_detail_view.json; generated, do not edit)
private:
    friend struct MasterDetailViewDisplayModeCapability<MasterDetailView>;
    friend struct MasterDetailViewCompactDetailVisibleCapability<MasterDetailView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(display_mode)& cell, MasterDetailDisplayMode& incoming);
    void written(const decltype(display_mode)& cell);
    void prepare_write(const decltype(compact_detail_visible)& cell, bool& incoming);
    void written(const decltype(compact_detail_visible)& cell);
    // property_codemod end MasterDetailView
};

} // namespace gui_forms
