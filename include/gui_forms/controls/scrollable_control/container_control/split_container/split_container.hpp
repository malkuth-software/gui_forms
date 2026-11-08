#pragma once

#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/split_container/split_container_values/split_container_values.hpp"
#include "gui_forms/controls/scrollable_control/container_control/split_container/splitter_panel/splitter_panel.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/controls/scrollable_control/container_control/split_container/split_container_capabilities/split_container_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

struct SplitSeamSnapshot final {
    Orientation orientation{Orientation::vertical};
    SplitSeamGeometry geometry;
    SplitSeamState state{SplitSeamState::idle};
    Rect visible_bounds;
    Rect hit_bounds;
    Rect actuator_bounds;
    double device_scale{1.0};
    double visible_device_pixels{3.0};
    double transition_duration_milliseconds{90.0};
    double transition_progress{1.0};
    bool transition_active{};
};

class SplitContainer final : public ContainerControl, public SplitContainerCapabilities<SplitContainer> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit SplitContainer(StableId stable_id);
    void initialize_control_tree();

    [[nodiscard]] std::shared_ptr<SplitterPanel> first_panel() const noexcept {
        return first_panel_;
    }
    [[nodiscard]] std::shared_ptr<SplitterPanel> second_panel() const noexcept {
        return second_panel_;
    }
    [[nodiscard]] Control::Ptr splitter_control() const noexcept {
        return splitter_;
    }

    [[nodiscard]] SplitSeamSnapshot splitter_seam_snapshot() const noexcept;
    [[nodiscard]] bool first_collapsed() const noexcept { return first_collapsed_; }
    [[nodiscard]] SplitCollapseOrigin first_collapse_origin() const noexcept {
        return first_collapse_origin_;
    }
    void set_first_collapsed(
        bool collapsed,
        SplitCollapseOrigin origin = SplitCollapseOrigin::programmatic);
    [[nodiscard]] bool second_collapsed() const noexcept { return second_collapsed_; }
    [[nodiscard]] SplitCollapseOrigin second_collapse_origin() const noexcept {
        return second_collapse_origin_;
    }
    void set_second_collapsed(
        bool collapsed,
        SplitCollapseOrigin origin = SplitCollapseOrigin::programmatic);

    [[nodiscard]] Event<const SplitChangeEvent&>& splitter_changed() noexcept {
        return splitter_changed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_pointer_preview(PointerEvent& event) override;
    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry seam;
        PartGeometry grip;
    };
    [[nodiscard]] Parts parts() const;
    struct States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool hot{};
        bool open{};
        bool closed{};
    };
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    [[nodiscard]] double axis_extent(Rect bounds) const noexcept;
    [[nodiscard]] double pointer_axis(Point point) const noexcept;
    [[nodiscard]] double constrained_distance(double requested,
                                              double total_extent) const noexcept;
    [[nodiscard]] double effective_splitter_width() const noexcept;
    [[nodiscard]] Rect collapse_tab_bounds() const noexcept;
    [[nodiscard]] bool collapse_target_is_collapsed() const noexcept;
    [[nodiscard]] SplitCollapseOrigin collapse_target_origin() const noexcept;
    void toggle_collapse_target(SplitCollapseOrigin origin);
    void reconcile_automatic_collapse(double total_extent);
    void set_distance(double distance, SplitChangeReason reason);
    void cancel_splitter_interaction(bool restore_distance) noexcept;
    void transfer_focus_from(const std::shared_ptr<SplitterPanel>& panel);
    void update_splitter_cursor();

    std::shared_ptr<SplitterPanel> first_panel_;
    std::shared_ptr<SplitterPanel> second_panel_;
    Control::Ptr splitter_;
    double requested_distance_{-1.0};
    double effective_distance_{};
    double remembered_distance_{-1.0};
    double previous_axis_extent_{};
    double previous_second_extent_{};
    double splitter_width_{3.0};
    double splitter_hit_width_{9.0};
    double pointer_offset_{};
    double pointer_start_distance_{};
    bool first_collapsed_{};
    bool second_collapsed_{};
    SplitCollapseOrigin first_collapse_origin_{SplitCollapseOrigin::none};
    SplitCollapseOrigin second_collapse_origin_{SplitCollapseOrigin::none};
    bool pointer_tracking_{};
    bool collapse_tab_tracking_{};
    bool automatic_collapse_suppressed_{};
    bool tree_initialized_{};
    Event<const SplitChangeEvent&> splitter_changed_;
    // property_codemod begin SplitContainer (tools/property_families/split_container.json; generated, do not edit)
private:
    friend struct SplitContainerOrientationCapability<SplitContainer>;
    friend struct SplitContainerSplitterDistanceCapability<SplitContainer>;
    friend struct SplitContainerSplitterWidthCapability<SplitContainer>;
    friend struct SplitContainerSplitterHitWidthCapability<SplitContainer>;
    friend struct SplitContainerSplitterGeometryCapability<SplitContainer>;
    friend struct SplitContainerSplitterTransitionDurationCapability<SplitContainer>;
    friend struct SplitContainerFirstMinimumCapability<SplitContainer>;
    friend struct SplitContainerSecondMinimumCapability<SplitContainer>;
    friend struct SplitContainerFirstMaximumCapability<SplitContainer>;
    friend struct SplitContainerSecondMaximumCapability<SplitContainer>;
    friend struct SplitContainerSplitterFixedCapability<SplitContainer>;
    friend struct SplitContainerFixedPanelCapability<SplitContainer>;
    friend struct SplitContainerCollapsePanelCapability<SplitContainer>;
    friend struct SplitContainerAutomaticCollapseThresholdCapability<SplitContainer>;
    friend struct SplitContainerKeyboardIncrementCapability<SplitContainer>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(orientation)& cell, Orientation& incoming);
    void written(const decltype(orientation)& cell);
    void write_view(const decltype(splitter_distance)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(splitter_distance)& cell) const;
    void write_view(const decltype(splitter_width)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(splitter_width)& cell) const;
    void write_view(const decltype(splitter_hit_width)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(splitter_hit_width)& cell) const;
    void prepare_write(const decltype(splitter_geometry)& cell, SplitSeamGeometry& incoming);
    void written(const decltype(splitter_geometry)& cell);
    void write_view(const decltype(splitter_transition_duration)& cell, FrameInterval incoming);
    [[nodiscard]] FrameInterval read_view(const decltype(splitter_transition_duration)& cell) const;
    void prepare_write(const decltype(first_minimum)& cell, double& incoming);
    void written(const decltype(first_minimum)& cell);
    void prepare_write(const decltype(second_minimum)& cell, double& incoming);
    void written(const decltype(second_minimum)& cell);
    void prepare_write(const decltype(first_maximum)& cell, std::optional<double>& incoming);
    void written(const decltype(first_maximum)& cell);
    void prepare_write(const decltype(second_maximum)& cell, std::optional<double>& incoming);
    void written(const decltype(second_maximum)& cell);
    void prepare_write(const decltype(splitter_fixed)& cell, bool& incoming);
    void written(const decltype(splitter_fixed)& cell);
    void prepare_write(const decltype(fixed_panel)& cell, SplitFixedPanel& incoming);
    void written(const decltype(fixed_panel)& cell);
    void prepare_write(const decltype(collapse_panel)& cell, SplitFixedPanel& incoming);
    void written(const decltype(collapse_panel)& cell);
    void prepare_write(const decltype(automatic_collapse_threshold)& cell, double& incoming);
    void written(const decltype(automatic_collapse_threshold)& cell);
    void prepare_write(const decltype(keyboard_increment)& cell, double& incoming);
    void written(const decltype(keyboard_increment)& cell);
    // property_codemod end SplitContainer
};

} // namespace gui_forms
