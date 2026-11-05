#pragma once

#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/split_container/splitter_panel/splitter_panel.hpp"
#include "gui_forms/range_controls.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

enum class SplitFixedPanel : std::uint8_t {
    none,
    first,
    second,
};

enum class SplitChangeReason : std::uint8_t {
    programmatic,
    pointer,
    keyboard,
    semantic,
    cancel,
    collapse,
    container_resize,
};

enum class SplitCollapseOrigin : std::uint8_t {
    none,
    programmatic,
    user,
    automatic_accommodation,
};

struct SplitChangeEvent final {
    double old_distance{};
    double new_distance{};
    SplitChangeReason reason{SplitChangeReason::programmatic};
    SplitCollapseOrigin collapse_origin{SplitCollapseOrigin::none};
};

// A split seam may retain an authored logical thickness or collapse its paint
// to one physical device pixel. Hit extents remain logical in both modes so a
// crisp line never becomes the only usable pointer target.
enum class SplitSeamThicknessPolicy : std::uint8_t {
    logical,
    device_pixel_hairline,
};

enum class SplitSeamState : std::uint8_t {
    idle,
    near,
    hot,
    dragging,
    focused,
    disabled,
    collapsed,
};

struct SplitSeamGeometry final {
    static constexpr double maximum_visible_thickness = 64.0;
    static constexpr double maximum_hit_extension = 128.0;
    static constexpr double maximum_hit_target = 256.0;

    double visible_thickness{3.0};
    SplitSeamThicknessPolicy thickness_policy{
        SplitSeamThicknessPolicy::logical};
    double hit_before{3.0};
    double hit_after{3.0};
    double minimum_hit_target{9.0};

    friend constexpr bool operator==(const SplitSeamGeometry& left,
                                     const SplitSeamGeometry& right) noexcept {
        return left.visible_thickness == right.visible_thickness &&
            left.thickness_policy == right.thickness_policy &&
            left.hit_before == right.hit_before &&
            left.hit_after == right.hit_after &&
            left.minimum_hit_target == right.minimum_hit_target;
    }
};

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

class SplitContainer final : public ContainerControl {
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

    [[nodiscard]] Orientation orientation() const noexcept { return orientation_; }
    void set_orientation(Orientation orientation);
    [[nodiscard]] double splitter_distance() const noexcept {
        return effective_distance_;
    }
    void set_splitter_distance(double distance);
    [[nodiscard]] double splitter_width() const noexcept { return splitter_width_; }
    void set_splitter_width(double width);
    [[nodiscard]] double splitter_hit_width() const noexcept {
        return splitter_hit_width_;
    }
    void set_splitter_hit_width(double width);
    [[nodiscard]] const SplitSeamGeometry& splitter_geometry() const noexcept {
        return splitter_geometry_;
    }
    // Validates the whole geometry before publishing any part of it. This is
    // the preferred API when asymmetric hit extents or a physical hairline are
    // required; the width-only setters above remain compatibility shorthands.
    void set_splitter_geometry(SplitSeamGeometry geometry);
    [[nodiscard]] FrameInterval splitter_transition_duration() const noexcept;
    void set_splitter_transition_duration(FrameInterval duration);
    [[nodiscard]] SplitSeamSnapshot splitter_seam_snapshot() const noexcept;
    [[nodiscard]] double first_minimum() const noexcept { return first_minimum_; }
    void set_first_minimum(double extent);
    [[nodiscard]] double second_minimum() const noexcept { return second_minimum_; }
    void set_second_minimum(double extent);
    [[nodiscard]] std::optional<double> first_maximum() const noexcept {
        return first_maximum_;
    }
    void set_first_maximum(std::optional<double> extent);
    [[nodiscard]] std::optional<double> second_maximum() const noexcept {
        return second_maximum_;
    }
    void set_second_maximum(std::optional<double> extent);
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
    [[nodiscard]] bool splitter_fixed() const noexcept { return splitter_fixed_; }
    void set_splitter_fixed(bool fixed);
    [[nodiscard]] SplitFixedPanel fixed_panel() const noexcept { return fixed_panel_; }
    void set_fixed_panel(SplitFixedPanel panel);
    // Optional seam-tab authority. Unlike fixed_panel(), this identifies the
    // pane toggled by a compact splitter tab and may remain operable while
    // ordinary splitter resizing is fixed.
    [[nodiscard]] SplitFixedPanel collapse_panel() const noexcept {
        return collapse_panel_;
    }
    void set_collapse_panel(SplitFixedPanel panel);
    // Consumer-authored content threshold. Crossing below it automatically
    // collapses collapse_panel(); crossing back restores only an automatically
    // collapsed pane. A user restore below the threshold is honored until the
    // composition next crosses above it.
    [[nodiscard]] double automatic_collapse_threshold() const noexcept {
        return automatic_collapse_threshold_;
    }
    void set_automatic_collapse_threshold(double extent);
    [[nodiscard]] double keyboard_increment() const noexcept {
        return keyboard_increment_;
    }
    void set_keyboard_increment(double increment);

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
    Orientation orientation_{Orientation::vertical};
    SplitFixedPanel fixed_panel_{SplitFixedPanel::none};
    SplitFixedPanel collapse_panel_{SplitFixedPanel::none};
    double requested_distance_{-1.0};
    double effective_distance_{};
    double remembered_distance_{-1.0};
    double previous_axis_extent_{};
    double previous_second_extent_{};
    double splitter_width_{3.0};
    double splitter_hit_width_{9.0};
    SplitSeamGeometry splitter_geometry_{};
    double first_minimum_{25.0};
    double second_minimum_{25.0};
    std::optional<double> first_maximum_;
    std::optional<double> second_maximum_;
    double keyboard_increment_{4.0};
    double automatic_collapse_threshold_{};
    double pointer_offset_{};
    double pointer_start_distance_{};
    bool first_collapsed_{};
    bool second_collapsed_{};
    SplitCollapseOrigin first_collapse_origin_{SplitCollapseOrigin::none};
    SplitCollapseOrigin second_collapse_origin_{SplitCollapseOrigin::none};
    bool splitter_fixed_{};
    bool pointer_tracking_{};
    bool collapse_tab_tracking_{};
    bool automatic_collapse_suppressed_{};
    bool tree_initialized_{};
    Event<const SplitChangeEvent&> splitter_changed_;
};

} // namespace gui_forms
